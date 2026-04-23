cmake_minimum_required(VERSION 3.10.2)
project("LeviViewModel")

# Folder include
include_directories(
    include
    include/backends
)

# Daftar file sumber secara eksplisit (lebih aman daripada GLOB)
set(IMGUI_SOURCES
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/backends/imgui_impl_android.cpp
    src/imgui/backends/imgui_impl_opengl3.cpp
)

add_library(LeviViewModel SHARED
    src/main.cpp
    ${IMGUI_SOURCES}
)

# Link library yang dibutuhkan
target_link_libraries(LeviViewModel
    android
    log
    GLESv3
    EGL
)
