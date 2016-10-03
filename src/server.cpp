#include "server_http.hpp"

//Added for the default_resource example
#include <iostream>
#include <iomanip>
#include <fstream>
#include <boost/filesystem.hpp>
#include <vector>
#include <algorithm>
#include <exception>

#include <mapnik/box2d.hpp>
#include <mapnik/map.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/image.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/datasource_cache.hpp>

#include <cmath>
#include <mutex>
#include <csignal>

#include "LRUCache11.hpp"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"


//Added for the json-example:

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;

constexpr unsigned TILE_SIZE = 256;
constexpr unsigned TILE_BUFFER = 128;
constexpr unsigned METATILE_SIZE = 2;

constexpr unsigned EARTH_RADIUS = 6378137;
constexpr unsigned EARTH_DIAMETER = EARTH_RADIUS * 2;
constexpr double EARTH_CIRCUMFERENCE = EARTH_DIAMETER * M_PI;
constexpr double MAX_RES = EARTH_CIRCUMFERENCE / 256;
constexpr double ORIGIN_SHIFT = EARTH_CIRCUMFERENCE/2;

struct TileCoordinate {
    unsigned long x,y,z;

    bool operator==(const TileCoordinate &other) const
    { return (x == other.x
              && y == other.y
              && z == other.z);
    }
};

namespace std
{
    template<> struct hash<TileCoordinate>
    {
        size_t operator()(const TileCoordinate &coord) const
        {
            // x,y,z are never very big values
            return (static_cast<size_t>(coord.x) << 32) + (static_cast<size_t>(coord.y) << 16) + coord.z;
        }
    };
}

template <typename T>
class BlockingQueue
{
private:
    std::mutex              d_mutex;
    std::condition_variable d_condition;
    std::deque<T>           d_queue;
public:
    BlockingQueue<T>& operator=(BlockingQueue<T> &&other)
    {
        d_queue = std::move(other.d_queue);
        return *this;
    }

    void push(T const& value) {
        {
            std::unique_lock<std::mutex> lock(this->d_mutex);
            d_queue.push_front(value);
        }
        this->d_condition.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(this->d_mutex);
        this->d_condition.wait(lock, [=]{ return !this->d_queue.empty(); });
        T rc(std::move(this->d_queue.back()));
        this->d_queue.pop_back();
        return rc;
    }
    bool tryPop (T & v, std::chrono::milliseconds dur) {
        std::unique_lock<std::mutex> lock(this->d_mutex);
        if (!this->d_condition.wait_for(lock, dur, [=]{ return !this->d_queue.empty(); })) {
            return false;
        }
        v = std::move (this->d_queue.back());
        this->d_queue.pop_back();
        return true;
    }
    int size() {
        return d_queue.size();
    }
};

class TileCache {

    int mapcount = 0;
    BlockingQueue<std::shared_ptr<mapnik::Map>> maps;
    lru11::Cache<TileCoordinate, std::string, std::mutex> cache;

    public:

    TileCache() : cache(1000,100) {};

    TileCache(std::vector<std::shared_ptr<mapnik::Map>> &maps_)  : cache(1000,100)
    {
        for (auto & map : maps_)
        {
            maps.push(map);
        }
        mapcount = maps_.size();
    }

    TileCache(TileCache &&other)  : cache(1000,100)
    {
        mapcount = other.mapcount;
        maps = std::move(other.maps);
    }

    TileCache& operator=(TileCache &&other)
    {
        mapcount = other.mapcount;
        maps = std::move(other.maps);
        return *this;
    }

    void replace(std::vector<std::shared_ptr<mapnik::Map>> &maps_) 
    {
        std::clog << "Removing " << mapcount << " mapnik instances from cache pool" << std::endl;
        // Remove all the old renderers
        for (int i=0; i<mapcount; i++)
        {
            auto m = maps.pop();
            m.reset();
        }

        // Clear the cached tiles
        std::clog << "Clearing " << cache.size() << " cached tiles" << std::endl;
        cache.clear();

        std::clog << "Re-adding " << maps_.size() << " mapnik instances to cache pool" << std::endl;
        // Add all the new renderers in there
        for (auto &map : maps_) {
            maps.push(std::move(map));
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

    std::string getTile(const TileCoordinate &tile) 
    {
        try
        {
            return cache.get(tile);
        }
        catch (const lru11::KeyNotFound &e)
        {
            mapnik::image_rgba8 im(256,256);
            const auto bbox = getTileBox(tile);

            {
                // Grab a map from the pool
                auto map = maps.pop();
                map->zoom_to_box(bbox);
                mapnik::agg_renderer<mapnik::image_rgba8> renderer(*map, im);
                renderer.apply();
                // return the map to the pool
                maps.push(std::move(map));
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
extern "C" void hup_handler(int signal) {
    reload_condition.notify_one();
}

struct Server
{
    HttpServer server;

    std::unordered_map<std::string, TileCache> handlers;

    Server() : server(8080,1)
    {
        server.resource["^/([a-z]+)/([0-9]+)/([0-9]+)/([0-9]+).png$"]["GET"]=[this](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
            auto prefix = request->path_match[1];
            auto z = std::stoul(request->path_match[2]);
            auto x = std::stoul(request->path_match[3]);
            auto y = std::stoul(request->path_match[4]);

            bool out_of_bounds = (z < 1 || z > 21);
            if (!out_of_bounds) {
                int limit = (1 << z);
                //out_of_bounds =  (x < 0 || x > (limit * 1 - 1) || y < 0 || y > (limit * 1 - 1));
                out_of_bounds =  (x > (limit * 1 - 1) || y > (limit * 1 - 1));
            }

            if (out_of_bounds) {
                *response << "HTTP/1.1 400 Bad Request\r\n\r\nInvalid tile coordinates";
                return;
            }

            // Check that we've got something registered at this prefix
            auto handler = handlers.find(prefix);
            if (handler == handlers.end())
            {
                std::string content = "Not found";
                *response << "HTTP/1.1 404 Not Found\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
                return;
            }

            // We found a handler, let's use it
            std::string buffer = handler->second.getTile({x,y,z});
            *response << "HTTP/1.1 200 OK\r\n"
                      << "Content-Type: image/png\r\n"
                      << "Content-Length: " << buffer.length() << "\r\n"
                      << "\r\n" 
                      << buffer;
        };
        server.default_resource["GET"]=[](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
            std::string content = "Not found";
            *response << "HTTP/1.1 404 Not Found\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
        };

        loadConfig();

    }

    void reloadConfig()
    {
        FILE* pFile = fopen("config.json", "rb");
        char buffer[65536];
        rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
        rapidjson::Document document;
        document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);

        assert(document.IsArray());

        for (auto &v : document.GetArray())
        {
            assert(v.IsObject());
            assert(v.HasMember("urlprefix"));
            assert(v.HasMember("mapnikconfig"));
            assert(v["urlprefix"].IsString());
            assert(v["mapnikconfig"].IsString());

            std::vector<std::shared_ptr<mapnik::Map>> maps;

            std::cout << "mapnikconfig = " << v["mapnikconfig"].GetString() << ", urlprefix=" << v["urlprefix"].GetString() << std::endl;

            for (int i=0; i<4; i++) {
                auto map = std::make_shared<mapnik::Map>(256,256);
                mapnik::load_map(*map, v["mapnikconfig"].GetString());
                maps.push_back(std::move(map));
            }
            assert(handlers.find(v["urlprefix"].GetString()) != handlers.end());
            handlers[v["urlprefix"].GetString()].replace(maps);
        }

    }

    void loadConfig()
    {

        FILE* pFile = fopen("config.json", "rb");
        char buffer[65536];
        rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
        rapidjson::Document document;
        document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);

        assert(document.IsArray());

        for (auto &v : document.GetArray())
        {
            assert(v.IsObject());
            assert(v.HasMember("urlprefix"));
            assert(v.HasMember("mapnikconfig"));
            assert(v["urlprefix"].IsString());
            assert(v["mapnikconfig"].IsString());

            std::vector<std::shared_ptr<mapnik::Map>> maps;

            std::cout << "mapnikconfig = " << v["mapnikconfig"].GetString() << ", urlprefix=" << v["urlprefix"].GetString() << std::endl;

            for (int i=0; i<4; i++) {
                auto map = std::make_shared<mapnik::Map>(256,256);
                mapnik::load_map(*map, v["mapnikconfig"].GetString());
                maps.push_back(std::move(map));
            }
            handlers[v["urlprefix"].GetString()] = std::move(TileCache(maps));
        }

    }

    void start()
    {
        server.start();
    }

    void reload_wait()
    {
        while (true)
        {
            std::mutex reload_mutex;
            std::unique_lock<std::mutex> lck(reload_mutex);
            reload_condition.wait(lck);
            reloadConfig();
        }
    }

};


int main() {

    mapnik::datasource_cache::instance().register_datasources("/usr/local/lib/mapnik/input/");

    Server server;

    std::thread server_thread([&server](){
        server.start();
    });

    std::thread reload_thread([&server]() {
        server.reload_wait();
    });

    std::signal(SIGHUP, hup_handler);
    std::clog << "Server started, waiting for requests.  Send SIGHUP to reload config." << std::endl;

    server_thread.join();

    return EXIT_SUCCESS;
}