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

# Mapnik stuff

execute_process(COMMAND mapnik-config --input-plugins OUTPUT_VARIABLE MAPNIK_PLUGIN_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND mapnik-config --defines OUTPUT_VARIABLE MAPNIK_DEFINES OUTPUT_STRIP_TRAILING_WHITESPACE)

add_definitions(${MAPNIK_DEFINES})
add_definitions(-DMAPNIK_PLUGIN_PATH="${MAPNIK_PLUGIN_PATH}")

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
include_directories(${MAPNIK_INCLUDE_DIR})

add_executable(tileserver src/server.cpp src/grid_utils.cpp)
target_link_libraries(tileserver ${MAPNIK_JSON_LIBRARY} ${MAPNIK_WKT_LIBRARY} ${MAPNIK_LIBRARY} ${ICU_LIBRARY} ${Boost_LIBRARIES} ${PTHREAD_LIBRARY})

set_property(TARGET tileserver PROPERTY CXX_STANDARD 14)
set_property(TARGET tileserver PROPERTY CXX_STANDARD_REQUIRED ON)
