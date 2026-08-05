# src/config
add_library(config STATIC
    Config.cpp
    Config.h
    ConfigDocument.cpp
    ConfigDocument.h
)

find_package(yaml-cpp REQUIRED)

target_include_directories(config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(config PRIVATE yaml-cpp)
