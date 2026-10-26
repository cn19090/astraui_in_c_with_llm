# astra —— 独立工程的构建脚本
#
# 本文件让 Inc/astra 目录可以脱离外层演示工程单独构建：
#
#   cmake -S Inc/astra -B Inc/astra/build
#   cmake --build Inc/astra/build
#
# 产出三个目标：
#   astra        框架静态库（C99，零平台依赖）
#   astra_demo   示例应用静态库（只调用 HAL，用于验证 demo 只依赖框架）
#   astra_test_hal 框架自测（16 项，不需要窗口，不链接任何图形库）
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
# ---------------------------------------------------------------------------
add_library(astra STATIC
    src/astra_config.c
    src/hal.c
    src/item.c
    src/launcher.c
    src/logo.c)

target_include_directories(astra PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# ---------------------------------------------------------------------------
# 示例应用：可单独编译，用来证明它只依赖框架公开头文件
# ---------------------------------------------------------------------------
add_library(astra_demo STATIC demo/astra_demo.c)
target_link_libraries(astra_demo PUBLIC astra)

# ---------------------------------------------------------------------------
# 框架自测：纯逻辑，无窗口、无后端
#   cmake --build ... && astra_test_hal
# ---------------------------------------------------------------------------
option(ASTRA_BUILD_TESTS "构建 astra 框架自测" ON)
if(ASTRA_BUILD_TESTS)
    add_executable(astra_test_hal tests/test_hal.c)
    target_link_libraries(astra_test_hal PRIVATE astra)
endif()
