/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2015 Artem Pavlenko
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

#include <mapnik/config.hpp>

#pragma GCC diagnostic push
#include <mapnik/warning_ignore.hpp>
#pragma GCC diagnostic pop

// mapnik
#include <mapnik/map.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/debug.hpp>
#include <mapnik/grid/grid_renderer.hpp>
#include <mapnik/grid/grid.hpp>
#include <mapnik/grid/grid_view.hpp>
#include <mapnik/value_error.hpp>
#include <mapnik/feature.hpp>
#include <mapnik/feature_kv_iterator.hpp>
#include "grid_utils.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <mapnik/value_types.hpp>

// stl
#include <stdexcept>
#include <memory>

namespace mapnik
{

template <typename T>
void DLL_PUBLIC grid2utf(T const &grid_type,
                         rapidjson::Value &l, // result array
                         std::vector<typename T::lookup_type> &key_order,
                         rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> &allocator)
{
    using keys_type = std::map<typename T::lookup_type, typename T::value_type>;
    using keys_iterator = typename keys_type::iterator;

    typename T::data_type const &data = grid_type.data();
    typename T::feature_key_type const &feature_keys = grid_type.get_feature_keys();
    typename T::feature_key_type::const_iterator feature_pos;

    keys_type keys;
    // start counting at utf8 codepoint 32, aka space character
    char codepoint = 32;

    std::size_t array_size = data.width();
    for (std::size_t y = 0; y < data.height(); ++y)
    {
        std::uint16_t idx = 0;
        const std::unique_ptr<char[]> line = std::make_unique<char[]>(array_size);
        typename T::value_type const *row = data.get_row(y);
        for (std::size_t x = 0; x < data.width(); ++x)
        {
            typename T::value_type feature_id = row[x];
            feature_pos = feature_keys.find(feature_id);
            if (feature_pos != feature_keys.end())
            {
                mapnik::grid::lookup_type val = feature_pos->second;
                keys_iterator key_pos = keys.find(val);
                if (key_pos == keys.end())
                {
                    // Create a new entry for this key. Skip the codepoints that
                    // can't be encoded directly in JSON.
                    if (codepoint == 34)
                        ++codepoint; // Skip "
                    else if (codepoint == 92)
                        ++codepoint; // Skip backslash
                    if (feature_id == mapnik::grid::base_mask)
                    {
                        keys[""] = codepoint;
                        key_order.push_back("");
                    }
                    else
                    {
                        keys[val] = codepoint;
                        key_order.push_back(val);
                    }
                    line[idx++] = codepoint;
                    ++codepoint;
                }
                else
                {
                    line[idx++] = key_pos->second;
                }
            }
            // else, shouldn't get here...
        }
        rapidjson::Value v;
        v.SetString(std::string(line.get(), array_size).c_str(), allocator);
        l.PushBack(v, allocator);
    }
}

template <typename T>
void DLL_PUBLIC grid2utf(T const &grid_type,
                         rapidjson::Value &l, // result array
                         std::vector<typename T::lookup_type> &key_order,
                         unsigned int resolution,
                         rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> &allocator)
{
    using keys_type = std::map<typename T::lookup_type, typename T::value_type>;
    using keys_iterator = typename keys_type::iterator;

    typename T::feature_key_type const &feature_keys = grid_type.get_feature_keys();
    typename T::feature_key_type::const_iterator feature_pos;

    keys_type keys;
    // start counting at utf8 codepoint 32, aka space character
    char codepoint = 32;

    unsigned array_size = std::ceil(grid_type.width() / static_cast<float>(resolution));
    for (unsigned y = 0; y < grid_type.height(); y = y + resolution)
    {
        std::uint16_t idx = 0;
        const std::unique_ptr<char[]> line = std::make_unique<char[]>(array_size);
        auto const *row = grid_type.get_row(y);
        for (unsigned x = 0; x < grid_type.width(); x = x + resolution)
        {
            typename T::value_type feature_id = row[x];
            feature_pos = feature_keys.find(feature_id);
            if (feature_pos != feature_keys.end())
            {
                auto val = feature_pos->second;
                keys_iterator key_pos = keys.find(val);
                if (key_pos == keys.end())
                {
                    // Create a new entry for this key. Skip the codepoints that
                    // can't be encoded directly in JSON.
                    if (codepoint == 34)
                        ++codepoint; // Skip "
                    else if (codepoint == 92)
                        ++codepoint; // Skip backslash
                    if (feature_id == mapnik::grid::base_mask)
                    {
                        keys[""] = codepoint;
                        key_order.push_back("");
                    }
                    else
                    {
                        keys[val] = codepoint;
                        key_order.push_back(val);
                    }
                    line[idx++] = codepoint;
                    ++codepoint;
                }
                else
                {
                    line[idx++] = key_pos->second;
                }
            }
            // else, shouldn't get here...
        }
        rapidjson::Value v;
        v.SetString(std::string(line.get(), array_size).c_str(), allocator);
        l.PushBack(v, allocator);
    }
}

template <typename T>
void DLL_PUBLIC write_features(T const &grid_type,
                               rapidjson::Value &feature_data, // result object
                               std::vector<typename T::lookup_type> const &key_order,
                               rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> &allocator)
{
    typename T::feature_type const &g_features = grid_type.get_grid_features();
    if (g_features.size() <= 0)
    {
        return;
    }

    std::set<std::string> const &attributes = grid_type.get_fields();
    typename T::feature_type::const_iterator feat_end = g_features.end();
    for (std::string const &key_item : key_order)
    {
        if (key_item.empty())
        {
            continue;
        }

        typename T::feature_type::const_iterator feat_itr = g_features.find(key_item);
        if (feat_itr == feat_end)
        {
            continue;
        }

        bool found = false;
        rapidjson::Value feat;
        feat.SetObject();
        mapnik::feature_ptr feature = feat_itr->second;
        for (std::string const &attr : attributes)
        {
            if (attr == "__id__")
            {
                rapidjson::Value k;
                k.SetString(attr.c_str(), allocator);
                rapidjson::Value v;
                v.SetInt(feature->id());
                feat.AddMember(k, v, allocator);
            }
            else if (feature->has_key(attr))
            {

                found = true;
                rapidjson::Value k;
                k.SetString(attr.c_str(), allocator);

                const auto val = feature->get(attr);
                if (val.is<value_integer>())
                {
                    rapidjson::Value v(val.get<value_integer>());
                    feat.AddMember(k, v, allocator);
                }
                else if (val.is<value_bool>())
                {
                    rapidjson::Value v(val.get<value_bool>());
                    feat.AddMember(k, v, allocator);
                }
                else if (val.is<value_double>())
                {
                    rapidjson::Value v(val.get<value_bool>());
                    feat.AddMember(k, v, allocator);
                }
                else if (val.is<value_unicode_string>())
                {
                    std::string buf;
                    val.get<value_unicode_string>().toUTF8String(buf);
                    rapidjson::Value v;
                    v.SetString(buf.c_str(), allocator);
                    feat.AddMember(k, v, allocator);
                }
            }
        }

        if (found)
        {
            rapidjson::Value v;
            v.SetString(feat_itr->first.c_str(), allocator);
            feature_data.AddMember(v, feat, allocator);
        }
    }
}

template <typename T>
void DLL_PUBLIC grid_encode_utf(T const &grid_type,
                                rapidjson::Document &json, // result object
                                bool add_features,
                                unsigned int resolution)
{
    // convert buffer to utf and gather key order
    rapidjson::Value l;
    l.SetArray();
    std::vector<typename T::lookup_type> key_order;

    if (resolution != 1)
    {
        mapnik::grid2utf<T>(grid_type, l, key_order, resolution, json.GetAllocator());
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        l.Accept(writer);
    }
    else
    {
        mapnik::grid2utf<T>(grid_type, l, key_order, json.GetAllocator());
    }

    // convert key order to proper python list
    rapidjson::Value keys_a;
    keys_a.SetArray();
    for (typename T::lookup_type const &key_id : key_order)
    {
        rapidjson::Value v;
        v.SetString(std::string(key_id).c_str(), json.GetAllocator());
        keys_a.PushBack(v, json.GetAllocator());
    }

    // gather feature data
    rapidjson::Value feature_data;
    feature_data.SetObject();
    if (add_features)
    {
        mapnik::write_features<T>(grid_type, feature_data, key_order, json.GetAllocator());
    }

    json.AddMember("grid", l, json.GetAllocator());
    json.AddMember("keys", keys_a, json.GetAllocator());
    json.AddMember("data", feature_data, json.GetAllocator());
}

template <typename T>
rapidjson::Document DLL_PUBLIC
grid_encode(T const &grid, std::string const &format, bool add_features, unsigned int resolution)
{
    if (format == "utf")
    {
        rapidjson::Document document;
        document.SetObject();
        grid_encode_utf<T>(grid, document, add_features, resolution);
        return document;
    }
    else
    {
        std::stringstream s;
        s << "'utf' is currently the only supported encoding format.";
        throw mapnik::value_error(s.str());
    }
}

template rapidjson::Document DLL_PUBLIC grid_encode(mapnik::grid const &grid,
                                                    std::string const &format,
                                                    bool add_features,
                                                    unsigned int resolution);
template rapidjson::Document DLL_PUBLIC grid_encode(mapnik::grid_view const &grid,
                                                    std::string const &format,
                                                    bool add_features,
                                                    unsigned int resolution);

void DLL_PUBLIC render_layer_for_grid(mapnik::Map const &map,
                                      mapnik::grid &grid,
                                      unsigned layer_idx,
                                      std::vector<std::string> const &fields,
                                      double scale_factor,
                                      unsigned offset_x,
                                      unsigned offset_y)
{
    std::vector<mapnik::layer> const &layers = map.layers();
    std::size_t layer_num = layers.size();
    if (layer_idx >= layer_num)
    {
        std::ostringstream s;
        s << "Zero-based layer index '" << layer_idx << "' not valid, only '" << layer_num
          << "' layers are in map\n";
        throw std::runtime_error(s.str());
    }

    for (const auto &name : fields)
    {
        grid.add_field(name);
    }

    // copy field names
    std::set<std::string> attributes = grid.get_fields();
    // todo - make this a static constant
    std::string known_id_key = "__id__";
    if (attributes.find(known_id_key) != attributes.end())
    {
        attributes.erase(known_id_key);
    }

    std::string join_field = grid.get_key();
    if (known_id_key != join_field && attributes.find(join_field) == attributes.end())
    {
        attributes.insert(join_field);
    }

    mapnik::grid_renderer<mapnik::grid> ren(map, grid, scale_factor, offset_x, offset_y);
    mapnik::layer const &layer = layers[layer_idx];
    ren.apply(layer, attributes);
}
}