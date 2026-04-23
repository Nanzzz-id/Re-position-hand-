cmake_minimum_required(VERSION 3.18)
project("LeviViewModel" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags & Warning suppression
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -fvisibility=hidden -ffunction-sections -fdata-sections -w")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fvisibility=hidden -ffunction-sections -fdata-sections -w")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections,--strip-all -s")

# Konfigurasi Dobby sebelum FetchContent
# Menggunakan fork neomloader yang sudah mengganti file .asm dengan runtime assembly (TurboAssembler)
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_FOR_ANDROID ON CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    dobby
    GIT_REPOSITORY https://github.com/neomloader/Dobby.git
    GIT_TAG main
)
FetchContent_MakeAvailable(dobby)

# Folder include
include_directories(
    include
    include/backends
    ${dobby_SOURCE_DIR}/include
)

# Daftar file sumber ImGui
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

# Link library
target_link_libraries(LeviViewModel
    dobby
    android
    log
    GLESv3
)
