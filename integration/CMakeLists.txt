cmake_minimum_required(VERSION 3.16)

# Board App 交叉编译到 aarch64 开发板。
# 请在 WSL 的 ext4 文件系统内构建，不要在 /mnt/d 上直接构建（9p 很慢）。
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")

project(lvgl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 板端是 Buildroot + glibc 2.29，而 WSL 交叉 gcc 自带 glibc 2.38。
# 优先使用 glibc 2.27（Ubuntu bionic）aarch64 sysroot，让动态程序能在板上运行；
# 找不到 sysroot 时回退为静态链接 main。
set(BOARD_APP_SYSROOT "$ENV{HOME}/aarch64-bionic" CACHE PATH
    "aarch64 sysroot with glibc <= 2.29 (Ubuntu bionic packages)")
if(EXISTS "${BOARD_APP_SYSROOT}/usr/include/stdio.h" AND
   EXISTS "${BOARD_APP_SYSROOT}/usr/aarch64-linux-gnu/lib/libc.so")
  set(BOARD_APP_HAVE_SYSROOT ON)
  message(STATUS "Board App: using sysroot ${BOARD_APP_SYSROOT}")
  add_compile_options(-isystem "${BOARD_APP_SYSROOT}/usr/include")
  add_link_options(
      "-B${BOARD_APP_SYSROOT}/usr/aarch64-linux-gnu/lib/"
      "-L${BOARD_APP_SYSROOT}/usr/aarch64-linux-gnu/lib/")
  set(BOARD_APP_STATIC OFF CACHE BOOL "Link main statically" FORCE)
else()
  set(BOARD_APP_HAVE_SYSROOT OFF)
  message(STATUS "Board App: bionic sysroot not found, static main fallback")
  set(BOARD_APP_STATIC ON CACHE BOOL "Link main statically" FORCE)
endif()

# Static LVGL so the board only needs bin/main + assets (no liblvgl.so).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# The app does not use LVGL's ThorVG/vector-graphics backend. Disabling it
# keeps the build C-only (the WSL image has no aarch64 C++ compiler).
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)

# LVGL itself (existing source tree; only the CMake language list is adapted).
add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR})

# Board application: board_core + board_ui + board_app.
# New app folders under board_app/app/ are collected automatically.
add_subdirectory(board_app)

add_executable(main main.c mouse_cursor_icon.c)
if(BOARD_APP_STATIC)
  target_link_options(main PRIVATE -static)
endif()

# board_ui/board_app use a linker section for auto-registration. Static archive
# members are normally pulled only when referenced, so link them whole.
target_link_libraries(main PRIVATE
  "-Wl,--whole-archive"
  board_ui
  board_app
  "-Wl,--no-whole-archive"
  lvgl m pthread dl)

add_custom_target(
  run
  COMMAND ${EXECUTABLE_OUTPUT_PATH}/main
  DEPENDS main)

