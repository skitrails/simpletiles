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

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "grid_utils.hpp"

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
    TileCache() : cache(1000, 100){};

    TileCache(const std::vector<std::shared_ptr<mapnik::Map>> &maps_) : cache(1000, 100)
    {
        for (auto &map : maps_)
        {
            map_pool.push(map);
        }
        mapcount = maps_.size();
    }

    TileCache(TileCache &&other) : cache(1000, 100)
    {
        mapcount = other.mapcount;
        map_pool = std::move(other.map_pool);
    }

    TileCache &operator=(TileCache &&other)
    {
        mapcount = other.mapcount;
        map_pool = std::move(other.map_pool);
        return *this;
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
            const auto interactivity_layer_id = getInteractiveLayerID(*map);
            map->resize(256 * tile.scale, 256 * tile.scale);
            map->zoom_to_box(bbox);
            std::vector<std::string> fields = getInteractiveFields(*map);
            mapnik::grid grid(256, 256, fields.front());
            mapnik::render_layer_for_grid(*map, grid, interactivity_layer_id, fields, 1, 0, 0);
            map_pool.push(std::move(map));

            auto document = mapnik::grid_encode(grid, "utf", false, 4);

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

namespace
{
std::condition_variable reload_condition;
}

// This function is declared `extern "C"` because the OS signal handler
// registration expects C-style linkage.
extern "C" void hup_handler(int signal) { reload_condition.notify_one(); }

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
    std::shared_ptr<TileCache> cache;

  public:
    Server(const std::string &mapnik_file_, const int port = 8080, const int threads_ = 1)
        : server(port, threads_), mapnik_file(mapnik_file_), threads(threads_),
          cache(std::make_shared<TileCache>(makeMaps(mapnik_file, threads)))
    {

        server.resource["^/([0-9]+)/([0-9]+)/([0-9]+)(@([123])x)?.(png|grid)$"]
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
                *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << content.length()
                          << "\r\n\r\n"
                          << content;
                return;
            }

            // We found a handler, let's use it
            if ("png" == format)
            {
                std::string buffer = cache->getTile({x, y, z, scale});
                *response << "HTTP/1.1 200 OK\r\n"
                          << "Content-Type: image/png\r\n"
                          << "Content-Length: " << buffer.length() << "\r\n"
                          << "\r\n"
                          << buffer;
            }
            else
            {
                std::string buffer = cache->getGrid({x, y, z, scale});
                *response << "HTTP/1.1 200 OK\r\n"
                          << "Content-Type: text/json\r\n"
                          << "Content-Length: " << buffer.length() << "\r\n"
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
            std::mutex reload_mutex;
            std::unique_lock<std::mutex> lck(reload_mutex);
            reload_condition.wait(lck);
            std::clog << "SIGHUP received, reloading cache" << std::endl;
            cache = std::make_shared<TileCache>(makeMaps(mapnik_file, threads));
        }
    }

    int port() { return server.config.port; }
};

int main(int argc, char *argv[])
{

    int port = 8080;
    int threads = 1;
    std::string mapnik_file = "";

    namespace po = boost::program_options;
    // Declare the supported options.
    po::options_description desc("Allowed options");

    desc.add_options()("help", "produce help message")(
        "port", po::value<int>(&port)->default_value(8080), "TCP port to listen on")(
        "threads", po::value<int>(&threads)->default_value(1), "Number of threads")(
        "mapnik-file", po::value<std::string>(&mapnik_file), "Mapnik XML file to load");

    po::positional_options_description p;
    p.add("mapnik_file", 1);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (!vm.count("mapnik-file"))
    {
        std::cout << "ERROR: no mapnik file supplied" << std::endl;
        std::cout << "Usage: " << argv[0] << "[options] [--mapnik-file] filename.xml" << std::endl;
        std::cout << desc << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.count("help"))
    {
        std::cout << "Usage: " << argv[0] << "[options] [--mapnik-file] filename.xml" << std::endl;
        std::cout << desc << std::endl;
        return EXIT_SUCCESS;
    }

    mapnik::datasource_cache::instance().register_datasources(MAPNIK_PLUGIN_PATH);

    Server server(mapnik_file, port, threads);

    std::thread server_thread([&server]() { server.start(); });

    std::thread reload_thread([&server]() { server.reload_wait(); });

    std::signal(SIGHUP, hup_handler);
    std::clog << "Server started, waiting for requests on port " << server.port()
              << ".  Send SIGHUP to reload config." << std::endl;

    server_thread.join();

    return EXIT_SUCCESS;
}