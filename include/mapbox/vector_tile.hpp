#pragma once

#include "vector_tile/vector_tile_config.hpp"
#include <mapbox/geometry.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas" // clang+gcc
#pragma GCC diagnostic ignored "-Wpragmas" // gcc
#pragma GCC diagnostic ignored "-W#pragma-messages"
#pragma GCC diagnostic ignored "-Wshadow"
#include <protozero/pbf_reader.hpp>
#pragma GCC diagnostic pop

#include <cmath>
#include <memory>
#include <map>
#include <unordered_map>
#include <functional>
#include <utility>
#include <sstream>

#include <experimental/optional>
template <typename T>
using optional = std::experimental::optional<T>;

namespace mapbox { namespace vector_tile {

using point_type = mapbox::geometry::point<std::int16_t>;

class points_array_type : public std::vector<point_type> {
public:
    using coordinate_type = point_type::coordinate_type;
    using std::vector<point_type>::vector;
};

class points_arrays_type : public std::vector<points_array_type> {
public:
    using coordinate_type = points_array_type::coordinate_type;
    using std::vector<points_array_type>::vector;
};

class layer;

class feature {
public:
    using properties_type = std::unordered_map<std::string,mapbox::geometry::value>;
    using packed_iterator_type = protozero::iterator_range<protozero::pbf_reader::const_uint32_iterator>;

    feature(protozero::data_view const&, layer const&);

    GeomType getType() const { return type; }
    optional<mapbox::geometry::value> getValue(std::string const&) const;
    properties_type getProperties() const;
    optional<mapbox::geometry::identifier> const& getID() const;
    std::uint32_t getExtent() const;
    std::uint32_t getVersion() const;
    template <typename GeometryCollectionType>
    GeometryCollectionType getGeometries(float scale) const;

private:
    const layer& layer_;
    optional<mapbox::geometry::identifier> id;
    GeomType type = GeomType::UNKNOWN;
    packed_iterator_type tags_iter;
    packed_iterator_type geometry_iter;
};

class layer {
public:
    layer(protozero::data_view const& layer_view);

    std::size_t featureCount() const { return features.size(); }
    std::unique_ptr<const feature> getFeature(std::size_t) const;
    std::string const& getName() const;
    std::uint32_t getExtent() const { return extent; }
    std::uint32_t getVersion() const { return version; }

private:
    friend class feature;

    std::string name;
    uint32_t version = 9;
    uint32_t extent = 4096;
    std::map<std::string, uint32_t> keysMap;
    std::vector<std::reference_wrapper<const std::string>> keys;
    std::vector<protozero::data_view> values;
    std::vector<protozero::data_view> features;
};

class buffer {
public:
    buffer(std::string const& data);
    std::vector<std::string> layerNames() const;
    std::map<std::string, const protozero::data_view> getLayers() const { return layers; }
    std::unique_ptr<const layer> getLayer(const std::string&) const;

private:
    std::map<std::string, const protozero::data_view> layers;
};

static mapbox::geometry::value parseValue(protozero::data_view const& value_view) {
    protozero::pbf_reader value_reader(value_view);
    while (value_reader.next())
    {
        switch (value_reader.tag()) {
        case ValueType::STRING:
            return value_reader.get_string();
        case ValueType::FLOAT:
            return static_cast<double>(value_reader.get_float());
        case ValueType::DOUBLE:
            return value_reader.get_double();
        case ValueType::INT:
            return value_reader.get_int64();
        case ValueType::UINT:
            return value_reader.get_uint64();
        case ValueType::SINT:
            return value_reader.get_sint64();
        case ValueType::BOOL:
            return value_reader.get_bool();
        default:
            value_reader.skip();
            break;
        }
    }
    return false;
}

feature::feature(protozero::data_view const& feature_view, layer const& l)
    : layer_(l) {
    protozero::pbf_reader feature_pbf(feature_view);
    while (feature_pbf.next()) {
        switch (feature_pbf.tag()) {
        case FeatureType::ID:
            id = { feature_pbf.get_uint64() };
            break;
        case FeatureType::TAGS:
            tags_iter = feature_pbf.get_packed_uint32();
            break;
        case FeatureType::TYPE:
            type = static_cast<GeomType>(feature_pbf.get_enum());
            break;
        case FeatureType::GEOMETRY:
            geometry_iter = feature_pbf.get_packed_uint32();
            break;
        default:
            feature_pbf.skip();
            break;
        }
    }
}

optional<mapbox::geometry::value> feature::getValue(const std::string& key) const {
    auto keyIter = layer_.keysMap.find(key);
    if (keyIter == layer_.keysMap.end()) {
        return optional<mapbox::geometry::value>();
    }

    const auto values_count = layer_.values.size();
    const auto keymap_count = layer_.keysMap.size();
    auto start_itr = tags_iter.begin();
    const auto end_itr = tags_iter.end();
    while (start_itr != end_itr) {
        uint32_t tag_key = static_cast<uint32_t>(*start_itr++);

        if (keymap_count <= tag_key) {
            throw std::runtime_error("feature referenced out of range key");
        }

        if (start_itr == end_itr) {
            throw std::runtime_error("uneven number of feature tag ids");
        }

        uint32_t tag_val = static_cast<uint32_t>(*start_itr++);;
        if (values_count <= tag_val) {
            throw std::runtime_error("feature referenced out of range value");
        }

        if (tag_key == keyIter->second) {
            return parseValue(layer_.values[tag_val]);
        }
    }

    return optional<mapbox::geometry::value>();
}

feature::properties_type feature::getProperties() const {
    auto start_itr = tags_iter.begin();
    const auto end_itr = tags_iter.end();
    std::size_t num_properties = std::distance(start_itr,end_itr);
    properties_type properties;
    if (num_properties == 0) {
        return properties;
    }
    properties.reserve(std::distance(start_itr,end_itr)/2);
    while (start_itr != end_itr) {
        uint32_t tag_key = static_cast<uint32_t>(*start_itr++);
        if (start_itr == end_itr) {
            throw std::runtime_error("uneven number of feature tag ids");
        }
        uint32_t tag_val = static_cast<uint32_t>(*start_itr++);
        properties.emplace(layer_.keys.at(tag_key),parseValue(layer_.values.at(tag_val)));
    }
    return properties;
}

optional<mapbox::geometry::identifier> const& feature::getID() const {
    return id;
}

std::uint32_t feature::getExtent() const {
    return layer_.getExtent();
}

std::uint32_t feature::getVersion() const {
    return layer_.getVersion();
}

template <typename GeometryCollectionType>
GeometryCollectionType feature::getGeometries(float scale) const {
    uint8_t cmd = 1;
    uint32_t length = 0;
    int32_t x = 0;
    int32_t y = 0;

    GeometryCollectionType paths;

    paths.emplace_back();

    auto start_itr = geometry_iter.begin();
    const auto end_itr = geometry_iter.end();
    bool first = true;
    uint32_t len_reserve = 0;
    int32_t extra_coords = 0;
    if (type == GeomType::LINESTRING) {
        extra_coords = 1;
    } else if (type == GeomType::POLYGON) {
        extra_coords = 2;
    }
    bool point_type = type == GeomType::POINT;

    while (start_itr != end_itr) {
        if (length == 0) {
            uint32_t cmd_length = static_cast<uint32_t>(*start_itr++);
            cmd = cmd_length & 0x7;
            length = len_reserve = cmd_length >> 3;
        }

        --length;

        if (cmd == CommandType::MOVE_TO || cmd == CommandType::LINE_TO) {

            if (point_type) {
                if (first && cmd == CommandType::MOVE_TO) {
                    // note: this invalidates pointers. So we always
                    // dynamically get the path with paths.back()
                    paths.reserve(len_reserve);
                    first = false;
                }
            } else {
                if (first && cmd == CommandType::LINE_TO) {
                    paths.back().reserve(len_reserve + extra_coords);
                    first = false;
                }
            }

            if (cmd == CommandType::MOVE_TO && !paths.back().empty()) {
                paths.emplace_back();
                if (!point_type) first = true;
            }

            x += protozero::decode_zigzag32(static_cast<uint32_t>(*start_itr++));
            y += protozero::decode_zigzag32(static_cast<uint32_t>(*start_itr++));
            float px = std::round(x * float(scale));
            float py = std::round(y * float(scale));

            if (px > std::numeric_limits<typename GeometryCollectionType::coordinate_type>::max() ||
                px < std::numeric_limits<typename GeometryCollectionType::coordinate_type>::min() ||
                py > std::numeric_limits<typename GeometryCollectionType::coordinate_type>::max() ||
                py < std::numeric_limits<typename GeometryCollectionType::coordinate_type>::min()
                ) {
                std::runtime_error("paths outside valid range of coordinate_type");
            } else {
                paths.back().emplace_back(
                    static_cast<typename GeometryCollectionType::coordinate_type>(px),
                    static_cast<typename GeometryCollectionType::coordinate_type>(py));
            }
        } else if (cmd == CommandType::CLOSE) {
            if (!paths.back().empty()) {
                paths.back().push_back(paths.back()[0]);
            }
        } else {
            throw std::runtime_error("unknown command");
        }
    }
#if defined(DEBUG)
    for (auto const& p : paths) {
        assert(p.size() == p.capacity());
    }
#endif
    return paths;
}

buffer::buffer(std::string const& data)
    : layers() {
        protozero::pbf_reader data_reader(data);
        while (data_reader.next(TileType::LAYERS)) {
            const protozero::data_view layer_view = data_reader.get_view();
            protozero::pbf_reader layer_reader(layer_view);
            std::string name;
            bool has_name = false;
            while (layer_reader.next(LayerType::NAME)) {
                name = layer_reader.get_string();
                has_name = true;
            }
            if (!has_name) {
                throw std::runtime_error("Layer missing name");
            }
            layers.emplace(name, layer_view);
        }
}

std::vector<std::string> buffer::layerNames() const {
    std::vector<std::string> names;
    names.reserve(layers.size());
    for (auto const& layer : layers) {
        names.emplace_back(layer.first);
    }
    return names;
}

std::unique_ptr<const layer> buffer::getLayer(const std::string& name) const {
    auto layer_it = layers.find(name);
    if (layer_it != layers.end()) {
        return std::make_unique<layer>(layer_it->second);
    }
    return nullptr;
}

layer::layer(protozero::data_view const& layer_view) {
    bool has_name = false;
    bool has_extent = false;
    bool has_version = false;
    protozero::pbf_reader layer_pbf(layer_view);
    while (layer_pbf.next()) {
        switch (layer_pbf.tag()) {
        case LayerType::NAME:
            {
                name = layer_pbf.get_string();
                has_name = true;
            }
            break;
        case LayerType::FEATURES:
            features.push_back(layer_pbf.get_view());
            break;
        case LayerType::KEYS:
            {
                auto iter = keysMap.emplace(layer_pbf.get_string(), keysMap.size());
                keys.emplace_back(std::reference_wrapper<const std::string>(iter.first->first));
            }
            break;
        case LayerType::VALUES:
            values.emplace_back(layer_pbf.get_view());
            break;
        case LayerType::EXTENT:
            {
                extent = layer_pbf.get_uint32();
                has_extent = true;
            }
            break;
        case LayerType::VERSION:
            {
                version = layer_pbf.get_uint32();
                has_version = true;
            }
            break;
        default:
            {
                layer_pbf.skip();
            }
            break;
        }
    }
    if (!has_version || !has_name || !has_extent) {
        std::stringstream msg;
        msg << "missing required field:";
        if (!has_version) {
            msg << " version ";
        }
        if (!has_extent) {
            msg << " extent ";
        }
        if (!has_name) {
            msg << " name";
        }
        throw std::runtime_error(msg.str().c_str());
    }
}

std::unique_ptr<const feature> layer::getFeature(std::size_t i) const {
    return std::make_unique<feature>(features.at(i), *this);
}

std::string const& layer::getName() const {
    return name;
}

}} // namespace mapbox/vector_tile
