cmake_minimum_required (VERSION 3.0)
project (tileserver)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(Boost_USE_STATIC_LIBS        ON)
find_package(Boost 1.56 REQUIRED COMPONENTS filesystem system chrono regex program_options)

if(APPLE)
  find_library(ICU_LIBRARY icuuc /usr/local/opt/icu4c/lib)
  find_path(MAPNIK_INCLUDE_DIR unicode/uversion.h /usr/local/opt/icu4c/include)
else()
  find_package(ICU 4.4 REQUIRED)
endif()

find_library(PTHREAD_LIBRARY pthread)

# Mapnik stuff
find_path(MAPNIK_INCLUDE_DIR mapnik/map.hpp )
find_library(MAPNIK_LIBRARY mapnik)
include( FindPackageHandleStandardArgs )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( Mapnik DEFAULT_MSG MAPNIK_LIBRARY MAPNIK_INCLUDE_DIR )
mark_as_advanced( MAPNIK_INCLUDE_DIR MAPNIK_LIBRARY )

include_directories(${Boost_INCLUDE_DIRS})

include_directories(${ICU_INCLUDE_DIRS})
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/deps/rapidjson-master/include")
include_directories("${PROJECT_SOURCE_DIR}/deps/variant/include")
include_directories(${MAPNIK_INCLUDE_DIR})

add_executable(tileserver src/server.cpp)
target_link_libraries(tileserver ${ICU_LIBRARY} ${Boost_LIBRARIES} ${MAPNIK_LIBRARY} ${PTHREAD_LIBRARY})

set_property(TARGET tileserver PROPERTY CXX_STANDARD 11)
set_property(TARGET tileserver PROPERTY CXX_STANDARD_REQUIRED ON)