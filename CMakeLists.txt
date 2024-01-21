cmake_minimum_required(VERSION 3.24)
project(vector-tiles LANGUAGES CXX)

add_library(${PROJECT_NAME} 
    mapbox/vector_tile.hpp
    mapbox/vector_tile/vector_tile_config.hpp
    mapbox/vector_tile/version.hpp
)

# install dependencies
include(cmake/mason.cmake)

mason_use(protozero VERSION "1.5.1" HEADER_ONLY)
mason_use(geometry VERSION "1.0.0" HEADER_ONLY)
mason_use(variant VERSION "1.1.4" HEADER_ONLY)

# link dependencies
target_add_mason_package(${PROJECT_NAME} PUBLIC protozero)
target_add_mason_package(${PROJECT_NAME} PUBLIC geometry)
target_add_mason_package(${PROJECT_NAME} PUBLIC variant)


target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vector-tile/include")
target_compile_features(stb_slim PUBLIC cxx_std_20)