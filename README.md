# `simpletiles` - a simple tileserver

This is a `mapnik`-based tileserver.  It's designed to serve up a single mapnik XML file.
Put it behind a reverse proxy and run multiple instances in order to serve multiple maps.

# Fetures

  - fast - all in-memory, 0.1ms retrieval for cached tiles
  - simple - not a lot of features

# Requirements

  - Boost (dev done with 1.60)
  - C++ compiler with C++14 support (dev done with clang from XCode)
  - Mapnik (dev done with 3.0.12)

# Usage

`./tileserver yourmap.xml`

Tiles become available at `http://localhost:8080/z/x/y.png`

# Reloading

You can send `SIGHUP` to the process and it will re-load the config files and re-initialize
the cached `mapnik` instances.  There will be a slight pause in request handling (a few milliseconds),
but no requests should be dropped.
