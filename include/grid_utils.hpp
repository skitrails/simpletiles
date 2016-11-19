/*****************************************************************************
 *
 * This file is adapted from part of Mapnik (c++ mapping toolkit)
 *
 * Original Copyright (C) 2015 Artem Pavlenko
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/
#ifndef MAPNIK_JSON_BINDING_GRID_UTILS_INCLUDED
#define MAPNIK_JSON_BINDING_GRID_UTILS_INCLUDED

#if defined _WIN32 || defined __CYGWIN__
#ifdef BUILDING_DLL
#ifdef __GNUC__
#define DLL_PUBLIC __attribute__((dllexport))
#else
#define DLL_PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
#endif
#else
#ifdef __GNUC__
#define DLL_PUBLIC __attribute__((dllimport))
#else
#define DLL_PUBLIC __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
#endif
#endif
#define DLL_LOCAL
#else
#if __GNUC__ >= 4
#define DLL_PUBLIC __attribute__((visibility("default")))
#define DLL_LOCAL __attribute__((visibility("hidden")))
#else
#define DLL_PUBLIC
#define DLL_LOCAL
#endif
#endif

// mapnik
#include <mapnik/map.hpp>
#include <mapnik/grid/grid.hpp>

#pragma GCC diagnostic push
#include <mapnik/warning_ignore.hpp>
#pragma GCC diagnostic pop
#include "rapidjson/document.h"

namespace mapnik
{

template <typename T>
void DLL_PUBLIC grid2utf(T const &grid_type,
                         rapidjson::Value &l,
                         std::vector<typename T::lookup_type> &key_order);

template <typename T>
void DLL_PUBLIC grid2utf(T const &grid_type,
                         rapidjson::Value &l,
                         std::vector<typename T::lookup_type> &key_order,
                         unsigned int resolution);

template <typename T>
void DLL_PUBLIC write_features(T const &grid_type,
                               rapidjson::Value &feature_data,
                               std::vector<typename T::lookup_type> const &key_order);

template <typename T>
void DLL_PUBLIC grid_encode_utf(T const &grid_type,
                                rapidjson::Value &json,
                                bool add_features,
                                unsigned int resolution);

template <typename T>
rapidjson::Document DLL_PUBLIC
grid_encode(T const &grid, std::string const &format, bool add_features, unsigned int resolution);

void DLL_PUBLIC render_layer_for_grid(const mapnik::Map &map,
                                      mapnik::grid &grid,
                                      unsigned layer_idx, // TODO - layer by name or index
                                      std::vector<std::string> const &fields,
                                      double scale_factor,
                                      unsigned offset_x,
                                      unsigned offset_y);
}

#endif // MAPNIK_JSON_BINDING_GRID_UTILS_INCLUDED
