cmake_minimum_required(VERSION 3.15)
project(TPC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ---------------------------------------------------------
# --- Config (YAML loader) --------------------------------
# ---------------------------------------------------------
add_subdirectory(config)

# ---------------------------------------------------------
# --- Solver executable (no geometry coupling) ------------
# ---------------------------------------------------------
add_executable(TPC
  main.cpp
  load_mesh.cpp
  boundary_conditions.cpp
  solver.cpp
  ComputeElectricField.cpp
)

target_link_libraries(TPC PRIVATE mfem config)
target_include_directories(TPC PRIVATE ${CMAKE_SOURCE_DIR})

# ---------------------------------------------------------
# --- Geometry (optional, separate build) -----------------
# ---------------------------------------------------------
# TODO Not sure how to handle this properly. Its not part of the solver
# And will likely only be done to generate geometries
add_subdirectory(geometry)

