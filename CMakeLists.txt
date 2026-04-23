cmake_minimum_required(VERSION 3.18)
project("LeviViewModel" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mengunduh Dobby secara otomatis
include(FetchContent)
FetchContent_Declare(
    dobby
    GIT_REPOSITORY https://github.com/jmpews/Dobby.git
    GIT_TAG master
)
FetchContent_MakeAvailable(dobby)

# Folder include untuk ImGui
include_directories(
    include
    include/backends
)

# Daftar file sumber ImGui (sesuaikan dengan struktur folder Anda)
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
    dobby
    android
    log
    GLESv3
    EGL
)
