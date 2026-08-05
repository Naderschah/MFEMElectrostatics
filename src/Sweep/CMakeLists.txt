# src/Sweep/CMakeLists.txt

set(IS_SWEEP_TOPLEVEL OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IS_SWEEP_TOPLEVEL ON)
    project(SWEEP LANGUAGES C CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

find_package(yaml-cpp REQUIRED)
find_package(MPI REQUIRED COMPONENTS C CXX)
find_package(OpenMP REQUIRED)
include(FetchContent)

option(XEMFEM_ENABLE_VTK "Enable VTK-dependent tracing/plotting support" OFF)

if (IS_SWEEP_TOPLEVEL)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../config
                     ${CMAKE_CURRENT_BINARY_DIR}/config)

    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cmdLineInteraction
                     ${CMAKE_CURRENT_BINARY_DIR}/cmdLineInteraction)

    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../SolverCore
                     ${CMAKE_CURRENT_BINARY_DIR}/SolverCore)
endif()

# ------ Nelder Mead (sometimes network fails for this for some reason) ---------------------------
set(NELDERMEAD_LOCAL "$ENV{NELDERMEAD_ROOT}" CACHE PATH "Path to a local nelder-mead checkout")
if (NOT NELDERMEAD_LOCAL)
  set(NELDERMEAD_LOCAL "/home/felix/third_party/nelder-mead")
endif()

if(EXISTS "${NELDERMEAD_LOCAL}/nelder-mead.h")
  add_library(neldermead_headers INTERFACE)
  target_include_directories(neldermead_headers INTERFACE "${NELDERMEAD_LOCAL}")
else()
  FetchContent_Declare(
      neldermead
      GIT_REPOSITORY https://github.com/YibaiMeng/nelder-mead.git
      GIT_TAG master
  )
  FetchContent_GetProperties(neldermead)
  if(NOT neldermead_POPULATED)
      FetchContent_Populate(neldermead)
  endif()
  add_library(neldermead_headers INTERFACE)
  target_include_directories(neldermead_headers INTERFACE "${neldermead_SOURCE_DIR}")
endif()

# ------ Indicators (header-only progress bar) --------------------------------
set(INDICATORS_LOCAL "$ENV{INDICATORS_ROOT}" CACHE PATH "Path to a local indicators checkout")
if (NOT INDICATORS_LOCAL)
  set(INDICATORS_LOCAL "/home/felix/third_party/indicators")
endif()

set(INDICATORS_INCLUDE_DIR "")
if(EXISTS "${INDICATORS_LOCAL}/include/indicators/progress_bar.hpp")
  set(INDICATORS_INCLUDE_DIR "${INDICATORS_LOCAL}/include")
elseif(EXISTS "${INDICATORS_LOCAL}/indicators/progress_bar.hpp")
  set(INDICATORS_INCLUDE_DIR "${INDICATORS_LOCAL}")
endif()

if(INDICATORS_INCLUDE_DIR)
  add_library(indicators_headers INTERFACE)
  target_include_directories(indicators_headers INTERFACE "${INDICATORS_INCLUDE_DIR}")
else()
  FetchContent_Declare(
      indicators
      GIT_REPOSITORY https://github.com/p-ranav/indicators.git
      GIT_TAG master
  )
  FetchContent_GetProperties(indicators)
  if(NOT indicators_POPULATED)
      FetchContent_Populate(indicators)
  endif()

  add_library(indicators_headers INTERFACE)
  target_include_directories(indicators_headers INTERFACE
      "${indicators_SOURCE_DIR}/include"
  )
endif()

# ------ Stepping (header-only) ---------------------------
# We need Boost Odeint
find_package(Boost 1.84 QUIET)

add_library(boost_headers INTERFACE)
if (Boost_FOUND)
  if (TARGET Boost::headers)
    target_link_libraries(boost_headers INTERFACE Boost::headers)
  elseif (TARGET Boost::boost)
    target_link_libraries(boost_headers INTERFACE Boost::boost)
  else()
    target_include_directories(boost_headers INTERFACE ${Boost_INCLUDE_DIRS})
  endif()
else()
  FetchContent_Declare(
      boost
      URL https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz
  )
  FetchContent_GetProperties(boost)
  if(NOT boost_POPULATED)
      FetchContent_Populate(boost)
  endif()
  target_include_directories(boost_headers INTERFACE "${boost_SOURCE_DIR}")
endif()

# ---------------------------------------------------------
# --- Sweep implementation as an OBJECT library ------------
# ---------------------------------------------------------
add_library(sweep_objects OBJECT
  config_modification.cpp
  optimization.cpp
  field_spread.cpp
  sweeps.cpp
  trace_fieldlines.cpp
  trace_fieldlines_MPI.cpp
  CIV.cpp
  common_tracing/tracing_objects.cpp
)

if (XEMFEM_ENABLE_VTK)
  if (NOT DEFINED VTK_DIR)
    if (DEFINED ENV{VTK_DIR} AND EXISTS "$ENV{VTK_DIR}")
      set(VTK_DIR "$ENV{VTK_DIR}" CACHE PATH "Path to VTK config")
    elseif (EXISTS "$ENV{HOME}/vtk/install/lib/cmake/vtk-9.5")
      set(VTK_DIR "$ENV{HOME}/vtk/install/lib/cmake/vtk-9.5" CACHE PATH "Path to VTK config")
    endif()
  endif()

  find_package(VTK QUIET COMPONENTS
      CommonCore
      CommonColor
      CommonDataModel
      IOXML
      IOImage
      FiltersCore
      FiltersGeneral
      FiltersGeometry
      FiltersExtraction
      FiltersSources
      FiltersFlowPaths
      RenderingCore
      RenderingOpenGL2
      RenderingFreeType
      RenderingContext2D
      RenderingContextOpenGL2
      RenderingAnnotation
      ChartsCore
      InteractionStyle
      FiltersPoints
      RenderingGridAxes
      FiltersExtraction
  )

  if (VTK_FOUND)
    message(STATUS "PLOT: VTK found (${VTK_VERSION}) — enabling VTK support")

  #  target_sources(sweep_objects PRIVATE plot_vtk.cpp)
    target_link_libraries(sweep_objects PRIVATE ${VTK_LIBRARIES})

    vtk_module_autoinit(
      TARGETS sweep_objects
      MODULES ${VTK_LIBRARIES}
    )

    target_compile_definitions(sweep_objects PRIVATE HAVE_VTK=1)
  else()
    message(STATUS "PLOT: VTK support requested but VTK was not found; disabling VTK support")
    target_compile_definitions(sweep_objects PRIVATE HAVE_VTK=0)
  endif()
else()
  message(STATUS "PLOT: VTK support disabled (XEMFEM_ENABLE_VTK=OFF)")
  target_compile_definitions(sweep_objects PRIVATE HAVE_VTK=0)
endif()


target_include_directories(sweep_objects PRIVATE
  ${MFEM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/..          # src/
  ${CMAKE_CURRENT_SOURCE_DIR}/../SolverCore
  ${CMAKE_CURRENT_SOURCE_DIR}/../SolverCore/common_tracing
)

target_link_libraries(sweep_objects PRIVATE
  SolverCore
  neldermead_headers
  interpolator
  indicators_headers
  boost_headers
  mfem
  path_handler
)
