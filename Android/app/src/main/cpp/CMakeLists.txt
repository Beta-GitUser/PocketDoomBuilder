cmake_minimum_required(VERSION 3.22.1)

project(pocketdoombuilder LANGUAGES CXX)

# AGP passes debug builds through without an optimisation level, which left the
# native library at -O0: startup did 295 PNG decodes and a large atlas upload
# unoptimised (about 30 s of black screen on a phone). Default an unset build
# type to an optimised one that keeps symbols for the hang watchdog.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dear ImGui (vendored in Android/third_party/imgui, v1.92.9b-docking)
# ---------------------------------------------------------------------------
# Android/third_party/imgui, resolved relative to this file so the project can
# be configured from anywhere.
get_filename_component(IMGUI_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/imgui" ABSOLUTE)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_compile_options(imgui PRIVATE -fno-exceptions -fno-rtti)
# The OpenGL3 backend must be told to use OpenGL ES 3 / GLES3 headers.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
target_link_libraries(imgui PUBLIC GLESv3 android log)

# ---------------------------------------------------------------------------
# PocketDoomBuilder native shell
# ---------------------------------------------------------------------------
add_library(pocketdoombuilder SHARED
    src/editor.cpp
    src/icons.cpp
    src/map.cpp
    src/mapio.cpp
    src/mapio_binary.cpp
    src/mapio_udmf.cpp
    src/png.cpp
    src/platform_android.cpp
    src/render2d.cpp
    src/theme.cpp
    src/ui.cpp
    src/ui_dialogs.cpp
    src/ui_mapview.cpp
    src/ui_panels.cpp
    src/ui_toolbar.cpp
    src/view2d.cpp
    src/wad.cpp
)

target_include_directories(pocketdoombuilder PRIVATE src)
target_compile_options(pocketdoombuilder PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -fno-exceptions
    -fno-rtti
)
target_link_libraries(pocketdoombuilder PRIVATE
    imgui
    GLESv3
    EGL
    android
    log
)
