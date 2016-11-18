#include "server_http.hpp"

// Added for the default_resource example
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <algorithm>
#include <exception>
#include <condition_variable>
#include <sstream>
#include <string>
#include <shared_mutex>

#include <mapnik/box2d.hpp>
#include <mapnik/map.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/image.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/unicode.hpp>
#include <mapnik/layer.hpp>

#include <cmath>
#include <mutex>
#include <csignal>

#include "LRUCache11.hpp"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "rapidxml.hpp"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "grid_utils.hpp"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// Added for the json-example:

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;

constexpr unsigned TILE_SIZE = 256;
constexpr unsigned TILE_BUFFER = 128;
constexpr unsigned METATILE_SIZE = 2;

constexpr unsigned EARTH_RADIUS = 6378137;
constexpr unsigned EARTH_DIAMETER = EARTH_RADIUS * 2;
constexpr double EARTH_CIRCUMFERENCE = EARTH_DIAMETER * M_PI;
constexpr double MAX_RES = EARTH_CIRCUMFERENCE / 256;
constexpr double ORIGIN_SHIFT = EARTH_CIRCUMFERENCE / 2;

struct TileCoordinate
{
    unsigned long x, y, z, scale;

    bool operator==(const TileCoordinate &other) const
    {
        return (x == other.x && y == other.y && z == other.z && scale == other.scale);
    }
};

namespace std
{
template <> struct hash<TileCoordinate>
{
    size_t operator()(const TileCoordinate &coord) const
    {
        // x,y,z are never very big values
        return (static_cast<std::uint64_t>(coord.x) << 32) +
               (static_cast<std::uint64_t>(coord.y) << 16) +
               (static_cast<std::uint64_t>(coord.z) << 8) + coord.scale;
    }
};
}

template <typename T> class BlockingQueue
{
  private:
    std::mutex d_mutex;
    std::condition_variable d_condition;
    std::deque<T> d_queue;

  public:
    BlockingQueue<T> &operator=(BlockingQueue<T> &&other)
    {
        d_queue = std::move(other.d_queue);
        return *this;
    }

    void push(T const &value)
    {
        {
            std::unique_lock<std::mutex> lock(this->d_mutex);
            d_queue.push_front(value);
        }
        this->d_condition.notify_one();
    }
    T pop()
    {
        std::unique_lock<std::mutex> lock(this->d_mutex);
        this->d_condition.wait(lock, [=] { return !this->d_queue.empty(); });
        T rc(std::move(this->d_queue.back()));
        this->d_queue.pop_back();
        return rc;
    }
    bool tryPop(T &v, std::chrono::milliseconds dur)
    {
        std::unique_lock<std::mutex> lock(this->d_mutex);
        if (!this->d_condition.wait_for(lock, dur, [=] { return !this->d_queue.empty(); }))
        {
            return false;
        }
        v = std::move(this->d_queue.back());
        this->d_queue.pop_back();
        return true;
    }
    int size() { return d_queue.size(); }
};

class TileCache
{

    int mapcount = 0;
    BlockingQueue<std::shared_ptr<mapnik::Map>> map_pool;
    lru11::Cache<TileCoordinate, std::string, std::mutex> cache;

  public:
    TileCache(const std::vector<std::shared_ptr<mapnik::Map>> &maps_, const int cache_size = 1000)
        : cache(cache_size, 100)
    {
        for (auto &map : maps_)
        {
            map_pool.push(map);
        }
        mapcount = maps_.size();
    }

    mapnik::box2d<double> getTileBox(const TileCoordinate &coord)
    {
        const auto total_width_in_tiles = 1 << coord.z;
        const auto tile_resolution = MAX_RES / total_width_in_tiles;

        constexpr int metaHeight = 1;
        constexpr int metaWidth = 1;

        const auto minx = -ORIGIN_SHIFT + (coord.x * 256) * tile_resolution;
        const auto miny = ORIGIN_SHIFT - ((coord.y + 1) * 256) * tile_resolution;
        const auto maxx = -ORIGIN_SHIFT + ((coord.x + 1) * 256) * tile_resolution;
        const auto maxy = ORIGIN_SHIFT - (coord.y * 256) * tile_resolution;

        return {minx, miny, maxx, maxy};
    }

    int getInteractiveLayerID(const mapnik::Map &map) const
    {
        int result = 0;

        const auto ilayer = map.get_extra_parameters().get<std::string>("interactivity_layer");

        if (ilayer)
        {
            int idx = 0;
            const auto &layers = map.layers();
            for (const auto &layer : layers)
            {
                if (*ilayer == layer.name())
                {
                    result = idx;
                    break;
                }
                idx++;
            }
        }

        return result;
    }

    std::vector<std::string> getInteractiveFields(const mapnik::Map &map) const
    {
        std::vector<std::string> results;
        const auto fields = map.get_extra_parameters().get<std::string>("interactivity_fields");
        if (fields)
        {
            std::istringstream ss(*fields);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                results.push_back(item);
            }
        }
        return results;
    }

    std::string getGrid(const TileCoordinate &tile)
    {
        const auto bbox = getTileBox(tile);
        {
            // Grab a map from the pool
            auto map = map_pool.pop();
            map->resize(256 * tile.scale, 256 * tile.scale);
            map->zoom_to_box(bbox);
            const auto interactivity_layer_id = getInteractiveLayerID(*map);
            std::vector<std::string> fields = getInteractiveFields(*map);
            mapnik::grid grid(256, 256, fields.front());
            mapnik::render_layer_for_grid(
                *map, grid, interactivity_layer_id, fields, tile.scale, 0, 0);
            map_pool.push(std::move(map));

            auto document = mapnik::grid_encode(grid, "utf", true, 4);

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);

            return std::string(buffer.GetString());
        }
    }

    std::string getTile(const TileCoordinate &tile)
    {
        try
        {
            return cache.get(tile);
        }
        catch (const lru11::KeyNotFound &e)
        {
            mapnik::image_rgba8 im(256 * tile.scale, 256 * tile.scale);
            const auto bbox = getTileBox(tile);

            {
                // Grab a map from the pool
                auto map = map_pool.pop();
                map->resize(256 * tile.scale, 256 * tile.scale);
                map->zoom_to_box(bbox);
                mapnik::agg_renderer<mapnik::image_rgba8> renderer(*map, im);
                renderer.apply();
                // return the map to the pool
                map_pool.push(std::move(map));
            }

            auto buffer = mapnik::save_to_string(im, "png");
            cache.insert(tile, buffer);
            return buffer;
        }
    }
};

struct Server
{
    HttpServer server;

    std::vector<std::shared_ptr<mapnik::Map>> makeMaps(const std::string &mapnik_file,
                                                       const int num)
    {
        std::vector<std::shared_ptr<mapnik::Map>> maps;
        for (int i = 0; i < num; i++)
        {
            auto map = std::make_shared<mapnik::Map>(256, 256);
            mapnik::load_map(*map, mapnik_file);
            maps.push_back(std::move(map));
        }
        return maps;
    }

    const std::string &mapnik_file;
    const int threads;
    const int cache_size;
    const std::string &prefix;
    std::shared_ptr<TileCache> cache;

    // Data needed for auto reloads
    boost::shared_mutex reload_mutex;
    struct stat mapnik_file_timestamp;
    std::unordered_map<std::string, struct stat> dependent_timestamps;

    std::vector<std::string> dependentFilenames(const std::string &mapnik_xml_file)
    {
        std::vector<std::string> result;
        std::ifstream t(mapnik_xml_file);
        if (!t)
            return result;
        std::vector<char> buffer((std::istreambuf_iterator<char>(t)),
                                 std::istreambuf_iterator<char>());
        buffer.push_back('\0');

        rapidxml::xml_document<> doc;
        doc.parse<0>(buffer.data());
        auto mapnode = doc.first_node("Map");
        if (mapnode)
        {
            auto layernode = mapnode->first_node("Layer");
            while (layernode)
            {
                auto datasourcenode = layernode->first_node("Datasource");
                if (datasourcenode)
                {
                    auto parameter = datasourcenode->first_node("Parameter");
                    while (parameter)
                    {
                        auto name = parameter->first_attribute("name");
                        if (name && std::strcmp(name->value(), "file") == 0)
                        {
                            boost::filesystem::path base(mapnik_xml_file);
                            result.push_back((base.parent_path() /=
                                              std::string(parameter->first_node()->value()))
                                                 .string());
                            break;
                        }
                        parameter = parameter->next_sibling("Parameter");
                    }
                }
                layernode = layernode->next_sibling("Layer");
            }
        }
        std::sort(result.begin(), result.end());
        auto last = std::unique(result.begin(), result.end());
        result.erase(last, result.end());

        return result;
    }

  public:
    Server(const std::string &mapnik_file_,
           const std::string &prefix_,
           const int port = 8080,
           const int threads_ = 1,
           const int cache_size_ = 1000)
        : server(port, threads_), mapnik_file(mapnik_file_), threads(threads_),
          cache_size(cache_size_), prefix(prefix_),
          cache(std::make_shared<TileCache>(makeMaps(mapnik_file, threads), cache_size))
    {

        // file_modify_times = getFileTimes(mapnik_file);
        // Save file info so we can check for reloads
        ::stat(mapnik_file.c_str(), &mapnik_file_timestamp);
        auto dependents = dependentFilenames(mapnik_file);
        for (const auto &fname : dependents)
        {
            struct stat tmp_stat;
            ::stat(fname.c_str(), &tmp_stat);
            dependent_timestamps[fname] = tmp_stat;
        }

        server.resource["^" + (prefix.empty() ? "" : ("/" + prefix)) +
                        "/([0-9]+)/([0-9]+)/([0-9]+)(@([123])x)?.(png|grid.json)$"]
                       ["GET"] = [this](std::shared_ptr<HttpServer::Response> response,
                                        std::shared_ptr<HttpServer::Request> request) {

            // std::clog << request->path_match[0] << std::endl;
            const auto z = std::stoul(request->path_match[1]);
            const auto x = std::stoul(request->path_match[2]);
            const auto y = std::stoul(request->path_match[3]);
            const auto scale = [&]() {
                if (request->path_match.length(5) > 0)
                    return std::stoul(request->path_match[5]);
                return 1ul;
            }();
            const auto format = request->path_match[6];

            bool out_of_bounds = (z < 1 || z > 21);
            if (!out_of_bounds)
            {
                int limit = (1 << z);
                // out_of_bounds =  (x < 0 || x > (limit * 1 - 1) || y < 0 || y > (limit *
                // 1 - 1));
                out_of_bounds = (x > (limit * 1 - 1) || y > (limit * 1 - 1));
            }

            if (out_of_bounds)
            {
                std::string content = "Invalid tile coordinates";
                *response << "HTTP/1.1 400 Bad Request\r\n"
                          << "Content-Length: " << content.length() << "\r\n"
                          << "Access-Control-Allow-Origin: *\r\n"
                          << "Access-Control-Allow-Credentials: true\r\n"
                          << "Access-Control-Allow-Methods: GET\r\n"
                          << "Access-Control-Allow-Headers: "
                             "DNT,X-CustomHeader,Keep-Alive,User-Agent,X-Requested-With,If-"
                             "Modified-Since,Cache-Control,Content-Type\r\n"
                          << "\r\n"
                          << content;
                return;
            }

            // We found a handler, let's use it
            std::string buffer;
            if ("png" == format)
            {
                {
                    boost::shared_lock<boost::shared_mutex> lock(reload_mutex);
                    buffer = cache->getTile({x, y, z, scale});
                }
                *response << "HTTP/1.1 200 OK\r\n"
                          << "Content-Type: image/png\r\n"
                          << "Content-Length: " << buffer.length() << "\r\n"
                          << "Access-Control-Allow-Origin: *\r\n"
                          << "Access-Control-Allow-Credentials: true\r\n"
                          << "Access-Control-Allow-Methods: GET\r\n"
                          << "Access-Control-Allow-Headers: "
                             "DNT,X-CustomHeader,Keep-Alive,User-Agent,X-Requested-With,If-"
                             "Modified-Since,Cache-Control,Content-Type\r\n"
                          << "\r\n"
                          << buffer;
            }
            else
            {
                {
                    boost::shared_lock<boost::shared_mutex> lock(reload_mutex);
                    buffer = cache->getGrid({x, y, z, scale});
                }
                *response << "HTTP/1.1 200 OK\r\n"
                          << "Content-Type: text/json\r\n"
                          << "Content-Length: " << buffer.length() << "\r\n"
                          << "Access-Control-Allow-Origin: *\r\n"
                          << "Access-Control-Allow-Credentials: true\r\n"
                          << "Access-Control-Allow-Methods: GET\r\n"
                          << "Access-Control-Allow-Headers: "
                             "DNT,X-CustomHeader,Keep-Alive,User-Agent,X-Requested-With,If-"
                             "Modified-Since,Cache-Control,Content-Type\r\n"
                          << "\r\n"
                          << buffer;
            }
        };

        server.default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response,
                                            std::shared_ptr<HttpServer::Request> request) {
            std::string content = "Not found";
            *response << "HTTP/1.1 404 Not Found\r\nContent-Length: " << content.length()
                      << "\r\n\r\n"
                      << content;
        };
    }

    void start() { server.start(); }

    void reload_wait()
    {
        while (true)
        {
            bool change_detected = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            struct stat fileinfo;
            ::stat(mapnik_file.c_str(), &fileinfo);

            // Check to see if any of our files have changed
            if (fileinfo.st_mtime > mapnik_file_timestamp.st_mtime)
            {
                std::clog << mapnik_file << " changed, reloading...";
                change_detected = true;
            }
            else
            {
                for (const auto &finfo : dependent_timestamps)
                {
                    struct stat tmpinfo;
                    ::stat(finfo.first.c_str(), &tmpinfo);
                    if (tmpinfo.st_mtime > finfo.second.st_mtime)
                    {
                        std::clog << finfo.first << " changed, reloading...";
                        change_detected = true;
                        break;
                    }
                }
            }

            // If they have, the process is basically:
            //   - wait until the mapnik file stops changing
            //   - parse it, get the list of dependent files
            //   - update their timestamps
            //   - wait until they stop changing
            //   - reload the mapnik objects
            if (change_detected)
            {
                // After noticing a change, keep looking until the file stops
                // changing - *then* do a reload
                // This is simple pause to give any file writers a chance to complete.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::clog << std::flush;

                // Now, re-initialize mapnik
                try
                {
                    auto tmp =
                        std::make_shared<TileCache>(makeMaps(mapnik_file, threads), cache_size);
                    boost::unique_lock<boost::shared_mutex> lock(reload_mutex);
                    cache = tmp;
                    std::clog << "success." << std::endl;
                }
                catch (const std::exception &e)
                {
                    std::clog << "ERROR: " << e.what() << std::endl;
                }

                // Get a fresh list of dependents from the mapnik file, in case
                // it changed.
                dependent_timestamps.clear();
                for (const auto &fname : dependentFilenames(mapnik_file))
                {
                    struct stat tmpinfo;
                    ::stat(fname.c_str(), &tmpinfo);
                    dependent_timestamps[fname] = tmpinfo;
                }
                // Even if we fail, we'll update the last filestamp thingy so
                // that we won't keep re-trying a broken file until it updates
                // It's safe to not lock-on-modify here, we're the only writer
                // and there is just one writer thread.
                mapnik_file_timestamp = fileinfo;
            }
        }
    }

    int port() { return server.config.port; }
};

int main(int argc, char *argv[])
{

    int port = 8080;
    int threads = 1;
    int cache_size = 1000;
    std::string prefix = "";
    std::string mapnik_file = "";

    namespace po = boost::program_options;
    // Declare the supported options.
    po::options_description desc("Allowed options");

    desc.add_options()("help", "produce help message")(
        "port", po::value<int>(&port)->default_value(8080), "TCP port to listen on")(
        "prefix",
        po::value<std::string>(&prefix)->default_value(""),
        "URL prefix to match [/prefix]/z/x/y.png")(
        "threads", po::value<int>(&threads)->default_value(1), "Number of threads")(
        "cache-size",
        po::value<int>(&cache_size)->default_value(1000),
        "Maximum number of tiles to cache")(
        "mapnik-file", po::value<std::string>(&mapnik_file), "Mapnik XML file to load");

    po::positional_options_description p;
    p.add("mapnik-file", 1);

    po::variables_map vm;
    try
    {
        po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    }
    catch (boost::program_options::error &e)
    {
        std::cout << "ERROR: " << e.what() << std::endl;
        std::cout << "Usage: " << argv[0] << " [options] [--mapnik-file] filename.xml" << std::endl;
        std::cout << desc << std::endl;
        return EXIT_FAILURE;
    }
    po::notify(vm);

    if (!vm.count("mapnik-file"))
    {
        std::cout << "ERROR: no mapnik file supplied" << std::endl;
        std::cout << "Usage: " << argv[0] << " [options] [--mapnik-file] filename.xml" << std::endl;
        std::cout << desc << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.count("help"))
    {
        std::cout << "Usage: " << argv[0] << " [options] [--mapnik-file] filename.xml" << std::endl;
        std::cout << desc << std::endl;
        return EXIT_SUCCESS;
    }

    mapnik::datasource_cache::instance().register_datasources(MAPNIK_PLUGIN_PATH);

    Server server(mapnik_file, prefix, port, threads, cache_size);

    std::thread server_thread([&server]() { server.start(); });

    // Give the main server a chance to start before starting the reload thread
    std::thread reload_thread([&server]() { server.reload_wait(); });

    std::clog << "Server started, waiting for requests on port " << server.port() << " at  "
              << (prefix.empty() ? "" : ("/" + prefix)) + "/{z}/{x}/{y}.(png|grid.json)"
              << std::endl;

    server_thread.join();

    return EXIT_SUCCESS;
}
