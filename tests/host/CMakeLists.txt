cmake_minimum_required(VERSION 3.16)
project(board_app_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()

set(BOARD_APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${BOARD_APP_ROOT}/core/src/*.c"
    "${BOARD_APP_ROOT}/core/third_party/cjson/*.c"
    "${BOARD_APP_ROOT}/app/*/core/src/*.c"
    "${BOARD_APP_ROOT}/app/games/*/core/src/*.c")

file(GLOB BOARD_CORE_APP_INCLUDES CONFIGURE_DEPENDS
    "${BOARD_APP_ROOT}/app/*/core/include"
    "${BOARD_APP_ROOT}/app/games/*/core/include")

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_executable(board_host_tests ${CORE_SOURCES} ${TEST_SOURCES})
target_include_directories(board_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${BOARD_APP_ROOT}/core/include"
    "${BOARD_APP_ROOT}/core/third_party/cjson"
    "${BOARD_APP_ROOT}/core/third_party/stb"
    ${BOARD_CORE_APP_INCLUDES})
target_compile_definitions(board_host_tests PRIVATE _GNU_SOURCE)
target_compile_options(board_host_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(board_host_tests PRIVATE m pthread)

add_test(NAME board_core_tests COMMAND board_host_tests)

option(BOARD_APP_SANITIZE "Enable ASan/UBSan for host tests" OFF)
if(BOARD_APP_SANITIZE)
    target_compile_options(board_host_tests PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(board_host_tests PRIVATE
        -fsanitize=address,undefined)
endif()
