# src/plotting/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

set(IS_PLOT_TOPLEVEL OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IS_PLOT_TOPLEVEL ON)
    project(PLOT LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# Path to vtk-config.cmake / VTKConfig.cmake
if (NOT DEFINED VTK_DIR)
    if (DEFINED ENV{VTK_DIR} AND EXISTS "$ENV{VTK_DIR}")
        set(VTK_DIR "$ENV{VTK_DIR}" CACHE PATH "Path to VTK config")
    elseif (EXISTS "$ENV{HOME}/vtk/install/lib/cmake/vtk-9.5")
        set(VTK_DIR "$ENV{HOME}/vtk/install/lib/cmake/vtk-9.5" CACHE PATH "Path to VTK config")
    endif()
endif()

find_package(VTK REQUIRED COMPONENTS
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

if (NOT VTK_FOUND)
    message(FATAL_ERROR "PLOT: Unable to find the VTK build folder.")
endif()

# Core plotting library (reused by PLOT and xemfem)
add_library(plotting STATIC
    FEMPlotVTK.cpp
    plotting_api.cpp
)

target_include_directories(plotting
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plotting
    PUBLIC
        ${VTK_LIBRARIES}
        config
        path_handler
)

vtk_module_autoinit(
  TARGETS plotting
  MODULES ${VTK_LIBRARIES}
)
