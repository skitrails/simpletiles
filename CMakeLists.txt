cmake_minimum_required (VERSION 3.0)
project (tileserver)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

#set(Boost_USE_STATIC_LIBS        ON)
find_package(Boost 1.56 REQUIRED COMPONENTS filesystem system chrono regex program_options thread)

if(APPLE)
  find_library(ICU_LIBRARY icuuc /usr/local/opt/icu4c/lib)
  find_path(MAPNIK_INCLUDE_DIR unicode/uversion.h /usr/local/opt/icu4c/include)
else()
  find_package(ICU 4.4 REQUIRED)
endif()

find_library(PTHREAD_LIBRARY pthread)
find_library(SYSTEMD_LIBRARY systemd)

# Mapnik stuff

find_program(MAPNIK_CONFIG NAMES mapnik-config HINTS /usr/local/bin /usr/local/mapnik/bin /opt/local/bin)
if (NOT MAPNIK_CONFIG)
    message(FATAL_ERROR "mapnik-config not found, cannot continue.  Make sure it's in your path.")
endif()
execute_process(COMMAND ${MAPNIK_CONFIG} --input-plugins OUTPUT_VARIABLE MAPNIK_PLUGIN_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${MAPNIK_CONFIG} --defines OUTPUT_VARIABLE MAPNIK_DEFINES OUTPUT_STRIP_TRAILING_WHITESPACE)
#execute_process(COMMAND ${MAPNIK_CONFIG} --libs OUTPUT_VARIABLE MAPNIK_DEFINES OUTPUT_STRIP_TRAILING_WHITESPACE)

add_custom_target(
   mapnik_vector_tile
   COMMAND make libvtile
   WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/deps/mapnik-vector-tile
)

add_definitions(${MAPNIK_DEFINES})
add_definitions(-DMAPNIK_PLUGIN_PATH="${MAPNIK_PLUGIN_PATH}")

add_definitions(-DMAPNIK_VECTOR_TILE_LIBRARY=1)
add_definitions("-DCLIPPER_INTPOINT_IMPL=mapnik::geometry::point<cInt>")
add_definitions("-DCLIPPER_PATH_IMPL=mapnik::geometry::line_string<cInt>")
add_definitions("-DCLIPPER_PATHS_IMPL=mapnik::geometry::multi_line_string<cInt>")
add_definitions("-DCLIPPER_IMPL_INCLUDE=<mapnik/geometry.hpp>")

find_path(MAPNIK_INCLUDE_DIR mapnik/map.hpp )
find_library(MAPNIK_LIBRARY mapnik)
find_library(MAPNIK_JSON_LIBRARY libmapnik-json.a)
find_library(MAPNIK_WKT_LIBRARY libmapnik-wkt.a)
include( FindPackageHandleStandardArgs )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( Mapnik DEFAULT_MSG MAPNIK_LIBRARY MAPNIK_INCLUDE_DIR )
mark_as_advanced( MAPNIK_INCLUDE_DIR MAPNIK_LIBRARY )

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${ICU_INCLUDE_DIRS})
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/deps/rapidjson-master/include")
include_directories("${PROJECT_SOURCE_DIR}/deps/variant/include")
include_directories("${PROJECT_SOURCE_DIR}/deps/Simple-Web-Server")
include_directories("${PROJECT_SOURCE_DIR}/deps/rapidxml-1.13")
include_directories("${PROJECT_SOURCE_DIR}/deps/mapnik-vector-tile/src")
include_directories("${PROJECT_SOURCE_DIR}/deps/mapnik-vector-tile/deps/protozero/include")
include_directories("${PROJECT_SOURCE_DIR}/deps/mapnik-vector-tile/deps/clipper/cpp")
include_directories(${MAPNIK_INCLUDE_DIR})

# Enable -fvisibility=hidden on object libraries
cmake_policy(SET CMP0063 NEW)

add_library(gridutils OBJECT src/grid_utils.cpp)
set_target_properties(gridutils PROPERTIES CXX_VISIBILITY_PRESET hidden)

if (APPLE)
  set(MAPNIK_VECTOR_TILE_LIB ${PROJECT_SOURCE_DIR}/deps/mapnik-vector-tile/build/Release/libmapnik_vector_tile_impl.a)
else()
  set(MAPNIK_VECTOR_TILE_LIB ${PROJECT_SOURCE_DIR}/deps/mapnik-vector-tile/build/Release/obj.target/gyp/libmapnik_vector_tile_impl.a)
endif()

add_executable(tileserver src/server.cpp $<TARGET_OBJECTS:gridutils>)
target_link_libraries(tileserver ${MAPNIK_JSON_LIBRARY} ${MAPNIK_WKT_LIBRARY} ${MAPNIK_LIBRARY} ${ICU_LIBRARY} ${Boost_LIBRARIES} ${PTHREAD_LIBRARY} ${MAPNIK_VECTOR_TILE_LIB} ${SYSTEMD_LIBRARY})
add_dependencies(tileserver mapnik_vector_tile)

set_property(TARGET tileserver PROPERTY CXX_STANDARD 14)
set_property(TARGET tileserver PROPERTY CXX_STANDARD_REQUIRED ON)

set_property(TARGET gridutils PROPERTY CXX_STANDARD 14)
set_property(TARGET gridutils PROPERTY CXX_STANDARD_REQUIRED ON)
