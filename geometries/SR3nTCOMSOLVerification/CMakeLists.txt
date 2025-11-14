cmake_minimum_required(VERSION 3.14)
project(SR3XENONnTAxisym LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Your sources
add_executable(SR3XENONnTAxisym
  main.cpp
  ../../src/config/Config.cpp
)

target_include_directories(SR3XENONnTAxisym PRIVATE
  ../../src/config
)

# yaml-cpp if you use it
find_package(yaml-cpp REQUIRED)
find_library(GMSH_LIB gmsh REQUIRED)

target_link_libraries(SR3XENONnTAxisym PRIVATE yaml-cpp ${GMSH_LIB})

# Make the generated header visible
target_include_directories(SR3XENONnTAxisym PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
