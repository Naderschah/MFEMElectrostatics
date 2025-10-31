# ---- Settings for this geometry ----
set(GEOM_NAME        tpc)  # change if you add more geometries
set(GEOM_SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR})
set(GEOM_BUILD_DIR   ${CMAKE_BINARY_DIR}/geometries/${GEOM_NAME})

set(GEOM_MSH_SRC     ${GEOM_SRC_DIR}/tpc_occ.msh)
set(GEOM_MSH_BIN     ${GEOM_BUILD_DIR}/geometry.msh)

set(GEOM_YAML_SRC    ${GEOM_SRC_DIR}/solver_params.yaml)
set(GEOM_YAML_BIN    ${GEOM_BUILD_DIR}/solver_params.yaml)

# ---- Geometry generator executable ----
add_executable(build_geometry
    ${GEOM_SRC_DIR}/main.cpp
    ${GEOM_SRC_DIR}/partition_tools.cpp
    ${GEOM_SRC_DIR}/debugging_things.cpp
)

# Link deps (gmsh required; config if geometry reads YAML)
find_library(GMSH_LIB gmsh REQUIRED)
target_link_libraries(build_geometry PRIVATE ${GMSH_LIB} config)
target_include_directories(build_geometry PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${GEOM_SRC_DIR}
)

# ---- Custom command: run generator and stage outputs ----
add_custom_command(
  OUTPUT ${GEOM_MSH_BIN} ${GEOM_YAML_BIN}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEOM_BUILD_DIR}
  COMMAND build_geometry
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GEOM_MSH_SRC}  ${GEOM_MSH_BIN}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GEOM_YAML_SRC} ${GEOM_YAML_BIN}
  WORKING_DIRECTORY ${GEOM_SRC_DIR}
  DEPENDS build_geometry
          ${GEOM_SRC_DIR}/main.cpp
          ${GEOM_SRC_DIR}/partition_tools.cpp
          ${GEOM_SRC_DIR}/partition_tools.h
          ${GEOM_SRC_DIR}/debugging_things.cpp
          ${GEOM_SRC_DIR}/debugging_things.h
          ${GEOM_YAML_SRC}
  BYPRODUCTS ${GEOM_MSH_SRC}
  COMMENT "Generating mesh + copying YAML into ${GEOM_BUILD_DIR}"
  VERBATIM
)

# ---- Convenience target: build this geometry’s artifacts ----
add_custom_target(mesh_${GEOM_NAME} ALL
  DEPENDS ${GEOM_MSH_BIN} ${GEOM_YAML_BIN}
)

# Optional: a short alias if you want `make mesh`
add_custom_target(mesh DEPENDS mesh_${GEOM_NAME})
