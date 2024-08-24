cmake_minimum_required(VERSION 3.24)
project(vector_tiles_src LANGUAGES CXX)

add_library(vector_tiles
    mapbox/vector_tile.hpp
    mapbox/vector_tile/vector_tile_config.hpp
    mapbox/vector_tile/version.hpp
)

target_include_directories(vector_tiles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vector_tiles PUBLIC cxx_std_20)
