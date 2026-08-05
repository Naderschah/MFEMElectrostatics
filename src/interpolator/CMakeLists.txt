# src/config
add_library(interpolator STATIC
    interpolator.cpp
    interpolator.h
)

set(HDF5_TARGET "")

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(HDF5_CPP QUIET IMPORTED_TARGET hdf5_cpp)
    if (TARGET PkgConfig::HDF5_CPP)
        set(HDF5_TARGET PkgConfig::HDF5_CPP)
    endif()
endif()

if (NOT HDF5_TARGET)
    find_package(HDF5 REQUIRED COMPONENTS CXX)

    if (TARGET HDF5::HDF5)
        set(HDF5_TARGET HDF5::HDF5)
    elseif (TARGET HDF5::HDF5_CPP)
        set(HDF5_TARGET HDF5::HDF5_CPP)
    else()
        add_library(interpolator_hdf5 INTERFACE)
        target_include_directories(interpolator_hdf5 INTERFACE ${HDF5_INCLUDE_DIRS})
        target_link_libraries(interpolator_hdf5 INTERFACE ${HDF5_LIBRARIES})
        set(HDF5_TARGET interpolator_hdf5)
    endif()
endif()

target_include_directories(interpolator
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        config
        SolverCore
)

target_link_libraries(interpolator
    PUBLIC
        config
        SolverCore
        ${HDF5_TARGET}
        path_handler
)
