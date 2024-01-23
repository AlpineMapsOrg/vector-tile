cmake_minimum_required(VERSION 3.24)
project(vector_tiles)

set(MASON_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mason_packages")

include(cmake/mason.cmake)

# install dependencies
mason_use(protozero VERSION "1.5.1" HEADER_ONLY)
mason_use(geometry VERSION "1.0.0" HEADER_ONLY)
mason_use(variant VERSION "1.1.4" HEADER_ONLY)

add_subdirectory(include)