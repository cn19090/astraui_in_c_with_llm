# astra —— 独立工程的构建脚本
#
# 本文件让 Inc/astra 目录可以脱离外层演示工程单独构建：
#
#   cmake -S Inc/astra -B Inc/astra/build
#   cmake --build Inc/astra/build
#
# 产出四个目标：
#   astra          框架静态库 + 内置的内存后端 hal_vbuf（C99，零平台依赖）
#   astra_demo     示例应用静态库（只调用 HAL，用于验证 demo 只依赖框架头文件）
#   astra_test_hal 框架自测（16 项，不需要窗口，不链接任何图形库）
#   astra_test_vbuf 内存后端自测（15 项，校验画布布局、绘制、刷新回调、按键）
#
cmake_minimum_required(VERSION 3.16)

project(astra LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 源码里有中文注释，MSVC 必须按 UTF-8 解析，否则解析错乱并报 C4819。
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# 框架本体：不含任何平台头文件，只依赖 <stdint.h>/<stdbool.h>/<string.h>/<math.h>
# hal_vbuf.c 是内置的"只写内存缓冲区"后端，同样没有平台依赖。
# ---------------------------------------------------------------------------
add_library(astra STATIC
    src/astra_config.c
    src/hal.c
    src/item.c
    src/launcher.c
    src/logo.c
    src/hal_vbuf.c)

target_include_directories(astra PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# ---------------------------------------------------------------------------
# Capacity knobs: the only macros that change the static RAM footprint.
# Every page costs sizeof(astra_menu) ~= 24 + 4*ASTRA_MENU_MAX_CHILDREN
#                                    + ASTRA_TITLE_MAX bytes.
#
#   cmake -S Inc/astra -B build -DASTRA_MENU_POOL_SIZE=16
#
# Defaults match the upstream layout; lower them on a small MCU.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Panel selection: the single compile-time switch that fixes the canvas size.
#
#   ASTRA_PANEL_128x64   (default)   1024 B canvas
#   ASTRA_PANEL_128x32                512 B canvas
#   ASTRA_PANEL_128x128              2048 B canvas
#   ASTRA_PANEL_240x240              7200 B canvas
#
#   cmake -S Inc/astra -B build -DASTRA_PANEL=ASTRA_PANEL_128x128
# ---------------------------------------------------------------------------
set(ASTRA_PANEL "ASTRA_PANEL_128x64" CACHE STRING "Target OLED panel")
set_property(CACHE ASTRA_PANEL PROPERTY STRINGS
    ASTRA_PANEL_128x64 ASTRA_PANEL_128x32 ASTRA_PANEL_128x128 ASTRA_PANEL_240x240)

set(ASTRA_MENU_POOL_SIZE 32 CACHE STRING "Number of page objects (pool size)")
set(ASTRA_MENU_MAX_CHILDREN 8 CACHE STRING "Max children per page")
set(ASTRA_TITLE_MAX 24 CACHE STRING "Title buffer size in bytes")
set(LOGO_STARS_MAX 32 CACHE STRING "Max logo stars (config default is 16)")
set(ASTRA_PRINT_LINES 8 CACHE STRING "hal_default_print_info cache lines")
set(ASTRA_PRINT_COLS 48 CACHE STRING "hal_default_print_info bytes per line")

target_compile_definitions(astra PUBLIC
    ASTRA_PANEL=${ASTRA_PANEL}
    ASTRA_MENU_POOL_SIZE=${ASTRA_MENU_POOL_SIZE}
    ASTRA_MENU_MAX_CHILDREN=${ASTRA_MENU_MAX_CHILDREN}
    ASTRA_TITLE_MAX=${ASTRA_TITLE_MAX}
    ASTRA_PRINT_LINES=${ASTRA_PRINT_LINES}
    ASTRA_PRINT_COLS=${ASTRA_PRINT_COLS})
target_compile_definitions(astra PRIVATE
    LOGO_STARS_MAX=${LOGO_STARS_MAX})

# ---------------------------------------------------------------------------
# 示例应用：可单独编译，用来证明它只依赖框架公开头文件
# ---------------------------------------------------------------------------
add_library(astra_demo STATIC demo/astra_demo.c)
target_link_libraries(astra_demo PUBLIC astra)

# ---------------------------------------------------------------------------
# 自测：纯逻辑，无窗口、无后端
#   cmake --build ... && astra_test_hal / astra_test_vbuf
# ---------------------------------------------------------------------------
option(ASTRA_BUILD_TESTS "构建 astra 自测程序" ON)
if(ASTRA_BUILD_TESTS)
    add_executable(astra_test_hal tests/test_hal.c)
    target_link_libraries(astra_test_hal PRIVATE astra)

    add_executable(astra_test_vbuf tests/test_vbuf.c)
    target_link_libraries(astra_test_vbuf PRIVATE astra)
endif()
