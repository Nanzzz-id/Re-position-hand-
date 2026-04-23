cmake_minimum_required(VERSION 3.10.2)

project("LeviViewModel")

include_directories(
    include
    include/imgui
    include/imgui/backends
)

file(GLOB_RECURSE IMGUI_SOURCES
    "src/imgui/*.cpp"
    "src/imgui/backends/*.cpp"
)

add_library(LeviViewModel SHARED
    src/main.cpp
    ${IMGUI_SOURCES}
)

target_link_libraries(LeviViewModel
    android
    log
    GLESv3
    EGL
)
