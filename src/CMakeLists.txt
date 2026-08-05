# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

project(MFEMElectrostatics LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(XEMFEM_ENABLE_VTK "Enable VTK-dependent tracing/plotting support" OFF)

cmake_policy(SET CMP0144 NEW)
cmake_policy(SET CMP0167 NEW)

# Put all build artifacts into the top-level build dir
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR})

# Common libraries
add_subdirectory(cmdLineInteraction)
add_subdirectory(config)

# Modules / executables
add_subdirectory(SolverCore)
if (XEMFEM_ENABLE_VTK)
  add_subdirectory(plotting)
endif()
add_subdirectory(interpolator)
add_subdirectory(geometry)
add_subdirectory(Sweep)
add_subdirectory(xemfem) 
add_subdirectory(path_handler) 
