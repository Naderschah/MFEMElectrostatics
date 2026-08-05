add_library(geometry STATIC
  geometry_api.cpp
  geometry_dxf.cpp
  geometry_internal.h
  geometry_legacy_json.cpp
  geometry_api.h
)

target_include_directories(geometry
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(yaml-cpp REQUIRED)

find_path(GMSH_INCLUDE_DIR
  NAMES gmsh.h
  PATH_SUFFIXES include
)
find_library(GMSH_LIBRARY
  NAMES gmsh
  PATH_SUFFIXES lib
)
if(NOT GMSH_INCLUDE_DIR OR NOT GMSH_LIBRARY)
  message(FATAL_ERROR
    "Could not locate gmsh headers/library. Ensure the gmsh package is available in CMAKE_PREFIX_PATH."
  )
endif()

target_include_directories(geometry PRIVATE
  "${GMSH_INCLUDE_DIR}"
)
target_compile_options(geometry PRIVATE
  "-I${GMSH_INCLUDE_DIR}"
)

find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
  target_link_libraries(geometry
    PUBLIC
      yaml-cpp
      nlohmann_json::nlohmann_json
      "${GMSH_LIBRARY}"
  )
else()
  find_path(NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    PATH_SUFFIXES include
  )
  if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR
      "Could not locate nlohmann/json.hpp for geometry module."
    )
  endif()
  target_include_directories(geometry PRIVATE
    "${NLOHMANN_JSON_INCLUDE_DIR}"
  )
  target_compile_options(geometry PRIVATE
    "-I${NLOHMANN_JSON_INCLUDE_DIR}"
  )
  target_link_libraries(geometry
    PUBLIC
      yaml-cpp
      "${GMSH_LIBRARY}"
  )
endif()
