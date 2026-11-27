# Board App 工程 CMake
# 生成三个目标：
#   board_core  - 纯 C 业务逻辑（不依赖 LVGL）
#   board_ui    - LVGL 主题/控件/导航/各应用界面
#   board_app   - 应用入口，链接 UI 与 core
#
# 新增应用文件夹由 CONFIGURE_DEPENDS 自动发现。

cmake_minimum_required(VERSION 3.16)

set(BOARD_APP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Core：不含 LVGL。通过不给 board_core 链接 lvgl 来强制保证。
# ---------------------------------------------------------------------------
file(GLOB_RECURSE BOARD_CORE_SOURCES CONFIGURE_DEPENDS
    "${BOARD_APP_ROOT}/core/src/*.c"
    "${BOARD_APP_ROOT}/core/third_party/cjson/*.c"
    "${BOARD_APP_ROOT}/app/*/core/src/*.c"
    "${BOARD_APP_ROOT}/app/games/*/core/src/*.c")

file(GLOB BOARD_CORE_APP_INCLUDES CONFIGURE_DEPENDS
    "${BOARD_APP_ROOT}/app/*/core/include"
    "${BOARD_APP_ROOT}/app/games/*/core/include")

add_library(board_core STATIC ${BOARD_CORE_SOURCES})
target_include_directories(board_core PUBLIC
    "${BOARD_APP_ROOT}/core/include"
    "${BOARD_APP_ROOT}/core/third_party/cjson"
    "${BOARD_APP_ROOT}/core/third_party/stb"
    ${BOARD_CORE_APP_INCLUDES})
target_compile_definitions(board_core PRIVATE _GNU_SOURCE)
target_compile_options(board_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(board_core PUBLIC m pthread dl)

# ---------------------------------------------------------------------------
# UI：LVGL 控件、主题、导航和所有应用页面。
# ---------------------------------------------------------------------------
file(GLOB_RECURSE BOARD_UI_SOURCES CONFIGURE_DEPENDS
    "${BOARD_APP_ROOT}/ui/src/*.c"
    "${BOARD_APP_ROOT}/main_page/*.c"
    "${BOARD_APP_ROOT}/app/*/ui/*.c"
    "${BOARD_APP_ROOT}/app/games/*/ui/*.c")

file(GLOB BOARD_UI_APP_INCLUDES CONFIGURE_DEPENDS
    "${BOARD_APP_ROOT}/app/*/ui"
    "${BOARD_APP_ROOT}/app/games/*/ui")

add_library(board_ui STATIC ${BOARD_UI_SOURCES})
target_include_directories(board_ui PUBLIC
    "${BOARD_APP_ROOT}/ui/include"
    "${BOARD_APP_ROOT}"
    PRIVATE
    "${BOARD_APP_ROOT}/main_page"
    ${BOARD_UI_APP_INCLUDES})
target_compile_options(board_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(board_ui PUBLIC board_core lvgl)

# ---------------------------------------------------------------------------
# 应用入口（board_app_init / board_app_deinit）。
# ---------------------------------------------------------------------------
add_library(board_app STATIC "${BOARD_APP_ROOT}/board_app.c")
target_include_directories(board_app PUBLIC "${BOARD_APP_ROOT}")
target_compile_definitions(board_app PUBLIC BOARD_APP_VERSION="1.0.0")
target_compile_options(board_app PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(board_app PUBLIC board_ui board_core)


# ---------------------------------------------------------------------------
# 板端 https 自检工具（可选目标）：
#   cmake --build build_board_app --target tls_check
# 用来看板上 OpenSSL 是否可用、能否连上 DeepSeek（不需要真 API key）。
# ---------------------------------------------------------------------------
add_executable(tls_check "${BOARD_APP_ROOT}/tools/tls_check.c")
target_compile_options(tls_check PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tls_check PRIVATE board_core)
# 板端 AI 自检工具（可选目标）：BOARD_APP_AI_KEY=sk-xxx ./ai_check
add_executable(ai_check "${BOARD_APP_ROOT}/tools/ai_check.c")
target_compile_options(ai_check PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(ai_check PRIVATE board_core)
# 把 API key 写入本地配置（data/settings.conf），免环境变量
add_executable(ai_key "${BOARD_APP_ROOT}/tools/ai_key.c")
target_compile_options(ai_key PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(ai_key PRIVATE board_core)