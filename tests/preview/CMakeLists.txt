# Host-only LVGL preview: renders the real Board App UI into a memory
# framebuffer and writes PNG screenshots. It is not part of the target build.
cmake_minimum_required(VERSION 3.16)
project(board_app_preview C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Use the preview lv_conf.h (no fbdev/evdev, 8 MB heap).
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lvgl"
                 "${CMAKE_BINARY_DIR}/lvgl")
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.."
                 "${CMAKE_BINARY_DIR}/board_app")

add_executable(board_preview preview_main.c)
target_include_directories(board_preview PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../core/third_party/stb")
target_link_libraries(board_preview PRIVATE
    "-Wl,--whole-archive" board_ui board_app "-Wl,--no-whole-archive"
    lvgl m pthread)
