# `simpletiles` - a simple tileserver

This is a `mapnik`-based tileserver.

# Fetures

  - fast - all in-memory, 0.1ms retrieval for cached tiles
  - simple - not a lot of features

# Requirements

  - Boost (dev done with 1.60)
  - C++ compiler with C++14 support (dev done with clang from XCode)
  - Mapnik (dev done with 3.0.12)

# Usage

`./simpletiles`

# Configuration

Example `config.json` file:

```
[
  {
    "urlprefix" : "tile",
    "mapnikconfig" : "map.xml"
  }
]
```

The config file contains an array of objects with `urlprefix` and `mapnikconfig` properties.
`urlprefix` is a simple name matching the regex `[a-z]+`.
`mapnikconfig` is a filename for a `mapnik` XML file.

Tiles become available at `http://localhost:8080/urlprefix/z/x/y.png`

# Reloading

You can send `SIGHUP` to the process and it will re-load the config files and re-initialize
the cached `mapnik` instances.  There will be a slight pause in request handling (a few milliseconds),
but no requests should be dropped.

# Caches and processing pools

TODO - configurable cache and pool sizes
