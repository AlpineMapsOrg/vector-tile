#pragma once

#include "vector_tile/vector_tile_config.hpp"
#include <mapbox/geometry.hpp>
#include <mapbox/feature.hpp>
#include <protozero/pbf_reader.hpp>

#include <cmath>
#include <cstdint>
#include <map>
#include <functional> // reference_wrapper
#include <string>
#include <stdexcept>

namespace mapbox { namespace vector_tile {

using point_type = mapbox::geometry::point<std::int16_t>;

class points_array_type : public std::vector<point_type> {
public:
    using coordinate_type = point_type::coordinate_type;
    template <class... Args>
    points_array_type(Args&&... args) : std::vector<point_type>(std::forward<Args>(args)...) {}
};

class points_arrays_type : public std::vector<points_array_type> {
public:
    using coordinate_type = points_array_type::coordinate_type;
    template <class... Args>
    points_arrays_type(Args&&... args) : std::vector<points_array_type>(std::forward<Args>(args)...) {}
};

class layer;

class feature {
public:
    using properties_type = mapbox::feature::property_map;
    using packed_iterator_type = protozero::iterator_range<protozero::pbf_reader::const_uint32_iterator>;

    feature(protozero::data_view const&, layer const&);

    GeomType getType() const { return type; }
    /**
     * Retrieve the value associated with a given key from the feature.
     *
     * @param key The key used to look up the corresponding value.
     * @param warning  A pointer to a string that may be used to record any warnings that
 *                     occur during the lookup process.
     *                 The caller is responsible for managing the memory of this string.
     * @return The value associated with the specified key, or a null value if the key is not found.
     *
     * Note: If the lookup process encounters a duplicate key in the feature, the function will
     *       return the value in the `values` set to which the associated tag index points to, and 
     *       will append a message to the `warning` string (if provided) to alert the caller to the
     *       presence of the duplicate key. 
     *       The caller should ensure that the `warning` string is properly initialized
     *       and cleaned up after use.
     */
    mapbox::feature::value getValue(std::string const&, std::string* warning = nullptr) const;
    properties_type getProperties() const;
    mapbox::feature::identifier const& getID() const;
    std::uint32_t getExtent() const;
    std::uint32_t getVersion() const;
    template <typename GeometryCollectionType>
    GeometryCollectionType getGeometries(float scale) const;

private:
    const layer& layer_;
    mapbox::feature::identifier id;
    GeomType type = GeomType::UNKNOWN;
    packed_iterator_type tags_iter;
    packed_iterator_type geometry_iter;
};

class layer {
public:
    layer(protozero::data_view const& layer_view);

    std::size_t featureCount() const { return features.size(); }
    protozero::data_view const& getFeature(std::size_t) const;
    std::string const& getName() const;
    std::uint32_t getExtent() const { return extent; }
    std::uint32_t getVersion() const { return version; }

private:
    friend class feature;

    std::string name;
    std::uint32_t version;
    std::uint32_t extent;
    std::multimap<std::string, std::uint32_t> keysMap;
    std::vector<std::reference_wrapper<const std::string>> keys;
    std::vector<protozero::data_view> values;
    std::vector<protozero::data_view> features;
};

class buffer {
public:
    buffer(std::string const& data);
    std::vector<std::string> layerNames() const;
    std::map<std::string, const protozero::data_view> getLayers() const { return layers; };
    layer getLayer(const std::string&) const;

private:
    std::map<std::string, const protozero::data_view> layers;
};

static mapbox::feature::value parseValue(protozero::data_view const& value_view) {
    mapbox::feature::value value;
    protozero::pbf_reader value_reader(value_view);
    while (value_reader.next())
    {
        switch (value_reader.tag()) {
        case ValueType::STRING:
            value = value_reader.get_string();
            break;
        case ValueType::FLOAT:
            value = static_cast<double>(value_reader.get_float());
            break;
        case ValueType::DOUBLE:
            value = value_reader.get_double();
            break;
        case ValueType::INT:
            value = value_reader.get_int64();
            break;
        case ValueType::UINT:
            value = value_reader.get_uint64();
            break;
        case ValueType::SINT:
            value = value_reader.get_sint64();
            break;
        case ValueType::BOOL:
            value = value_reader.get_bool();
            break;
        default:
            value_reader.skip();
            break;
        }
    }
    return value;
}

inline feature::feature(protozero::data_view const& feature_view, layer const& l)
    : layer_(l),
      id(),
      type(GeomType::UNKNOWN),
      tags_iter(),
      geometry_iter()
    {
    protozero::pbf_reader feature_pbf(feature_view);
    while (feature_pbf.next()) {
        switch (feature_pbf.tag()) {
        case FeatureType::ID:
            id = feature_pbf.get_uint64();
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

inline mapbox::feature::value feature::getValue(const std::string& key, std::string* warning ) const {
    const auto key_range = layer_.keysMap.equal_range(key);
    const auto key_count = std::distance(key_range.first, key_range.second) ;
    if (key_count < 1) {
        return mapbox::feature::null_value;
    }

    const auto values_count = layer_.values.size();
    auto start_itr = tags_iter.begin();
    const auto end_itr = tags_iter.end();
    while (start_itr != end_itr) {
        std::uint32_t tag_key = static_cast<std::uint32_t>(*start_itr++);

        if (start_itr == end_itr) {
            throw std::runtime_error("uneven number of feature tag ids");
        }

        std::uint32_t tag_val = static_cast<std::uint32_t>(*start_itr++);;
        if (values_count <= tag_val) {
            throw std::runtime_error("feature referenced out of range value");
        }

        bool key_found = false;
        for (auto i = key_range.first; i != key_range.second; ++i) {
            if (i->second == tag_key) {
                key_found = true;
                break;
            }
        }

        if (key_found) {
            // Continue process with case when same keys having multiple tag ids.
            if (key_count > 1 && warning) {
                *warning = std::string("duplicate keys with different tag ids are found");
            }
            return parseValue(layer_.values[tag_val]);
        }
    }

    return mapbox::feature::null_value;
}

inline feature::properties_type feature::getProperties() const {
    auto start_itr = tags_iter.begin();
    const auto end_itr = tags_iter.end();
    properties_type properties;
    auto iter_len = std::distance(start_itr,end_itr);
    if (iter_len > 0) {
        properties.reserve(static_cast<std::size_t>(iter_len/2));
        while (start_itr != end_itr) {
            std::uint32_t tag_key = static_cast<std::uint32_t>(*start_itr++);
            if (start_itr == end_itr) {
                throw std::runtime_error("uneven number of feature tag ids");
            }
            std::uint32_t tag_val = static_cast<std::uint32_t>(*start_itr++);
            properties.emplace(layer_.keys.at(tag_key),parseValue(layer_.values.at(tag_val)));
        }
    }
    return properties;
}

inline mapbox::feature::identifier const& feature::getID() const {
    return id;
}

inline std::uint32_t feature::getExtent() const {
    return layer_.getExtent();
}

inline std::uint32_t feature::getVersion() const {
    return layer_.getVersion();
}

template <typename GeometryCollectionType>
GeometryCollectionType feature::getGeometries(float scale) const {
    std::uint8_t cmd = 1;
    std::uint32_t length = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;

    GeometryCollectionType paths;

    paths.emplace_back();

    auto start_itr = geometry_iter.begin();
    const auto end_itr = geometry_iter.end();
    bool first = true;
    std::uint32_t len_reserve = 0;
    std::size_t extra_coords = 0;
    if (type == GeomType::LINESTRING) {
        extra_coords = 1;
    } else if (type == GeomType::POLYGON) {
        extra_coords = 2;
    }
    bool is_point = type == GeomType::POINT;

    while (start_itr != end_itr) {
        if (length == 0) {
            std::uint32_t cmd_length = static_cast<std::uint32_t>(*start_itr++);
            cmd = cmd_length & 0x7;
            length = len_reserve = cmd_length >> 3;
            // Prevents the creation of vector tiles that would cause
            // a denial of service from massive over allocation. Protection
            // limit is based on the assumption of an int64_t point which is
            // 16 bytes in size and wanting to have a maximum of 1 MB of memory
            // used.
            constexpr std::uint32_t MAX_LENGTH = (1024 * 1024) / 16;
            if (len_reserve > MAX_LENGTH) {
                len_reserve = MAX_LENGTH;
            }
        }

        if (cmd == CommandType::MOVE_TO || cmd == CommandType::LINE_TO) {
            if (length == 0) {
                // If length is still equal to zero after the preceding step, this
                // represents a command with an invalid command count, so we continue here to
                // exit appropriately rather than underflow when we decrement length
                continue;
            }

            --length;

            if (is_point) {
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
                if (paths.back().size() < paths.back().capacity()) {
                    // Assuming we had an invalid length before
                    // lets shrink to fit, just to make sure
                    // we don't have a large capacity vector
                    // just wasting memory
                    paths.back().shrink_to_fit();
                }
                paths.emplace_back();
                if (!is_point) {
                    first = true;
                }
            }

            x += protozero::decode_zigzag32(static_cast<std::uint32_t>(*start_itr++));
            y += protozero::decode_zigzag32(static_cast<std::uint32_t>(*start_itr++));
            float px = ::roundf(static_cast<float>(x) * scale);
            float py = ::roundf(static_cast<float>(y) * scale);
            static const float max_coord = static_cast<float>(std::numeric_limits<typename GeometryCollectionType::coordinate_type>::max());
            static const float min_coord = static_cast<float>(std::numeric_limits<typename GeometryCollectionType::coordinate_type>::min());

            if (px > max_coord ||
                px < min_coord ||
                py > max_coord ||
                py < min_coord
                ) {
                throw std::runtime_error("paths outside valid range of coordinate_type");
            } else {
                paths.back().emplace_back(
                    static_cast<typename GeometryCollectionType::coordinate_type>(px),
                    static_cast<typename GeometryCollectionType::coordinate_type>(py));
            }
        } else if (cmd == CommandType::CLOSE) {
            if (!paths.back().empty()) {
                paths.back().push_back(paths.back()[0]);
            }
            length = 0;
        } else {
            throw std::runtime_error("unknown command");
        }
    }
    if (paths.size() < paths.capacity()) {
        // Assuming we had an invalid length before
        // lets shrink to fit, just to make sure
        // we don't have a large capacity vector
        // just wasting memory
        paths.shrink_to_fit();
    }
#if defined(DEBUG)
    for (auto const& p : paths) {
        assert(p.size() == p.capacity());
    }
#endif
    return paths;
}

inline buffer::buffer(std::string const& data)
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

inline std::vector<std::string> buffer::layerNames() const {
    std::vector<std::string> names;
    names.reserve(layers.size());
    for (auto const& layer : layers) {
        names.emplace_back(layer.first);
    }
    return names;
}

inline layer buffer::getLayer(const std::string& name) const {
    auto layer_it = layers.find(name);
    if (layer_it == layers.end()) {
        throw std::runtime_error(std::string("no layer by the name of '")+name+"'");
    }
    return layer(layer_it->second);
}

inline layer::layer(protozero::data_view const& layer_view) :
    name(),
    version(1),
    extent(4096),
    keysMap(),
    keys(),
    values(),
    features()
{
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
            {
                features.push_back(layer_pbf.get_view());
            }
            break;
        case LayerType::KEYS:
            {
                // We want to keep the keys in the order of the vector tile
                // https://github.com/mapbox/mapbox-gl-native/pull/5183
                auto iter = keysMap.emplace(layer_pbf.get_string(), uint32_t(keys.size()));
                keys.emplace_back(std::reference_wrapper<const std::string>(iter->first));
            }
            break;
        case LayerType::VALUES:
            {
                values.emplace_back(layer_pbf.get_view());
            }
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
        std::string msg("missing required field:");
        if (!has_version) {
            msg += " version ";
        }
        if (!has_extent) {
            msg += " extent ";
        }
        if (!has_name) {
            msg += " name";
        }
        throw std::runtime_error(msg.c_str());
    }
}

inline protozero::data_view const& layer::getFeature(std::size_t i) const {
    return features.at(i);
}

inline std::string const& layer::getName() const {
    return name;
}

}} // namespace mapbox/vector_tile
