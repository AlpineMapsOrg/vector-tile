cmake_minimum_required(VERSION 3.24)
project(vector_tiles_src LANGUAGES CXX)

add_library(vector_tiles
    mapbox/vector_tile.hpp
    mapbox/feature.hpp
    mapbox/vector_tile/vector_tile_config.hpp
    mapbox/vector_tile/version.hpp
    mapbox/recursive_wrapper.hpp
    mapbox/geometry.hpp
    mapbox/geometry_io.hpp
    mapbox/geometry/box.hpp
    mapbox/geometry/empty.hpp
    mapbox/geometry/envelope.hpp
    mapbox/geometry/for_each_point.hpp
    mapbox/geometry/geometry.hpp
    mapbox/geometry/line_string.hpp
    mapbox/geometry/multi_line_string.hpp
    mapbox/geometry/multi_point.hpp
    mapbox/geometry/multi_polygon.hpp
    mapbox/geometry/point_arithmetic.hpp
    mapbox/geometry/point.hpp
    mapbox/geometry/polygon.hpp

)

target_include_directories(vector_tiles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vector_tiles PUBLIC cxx_std_20)
