# quickjs: 精简版 QuickJS 静态库（不包含 qjs.c / quickjs-libc.c）。
cmake_minimum_required(VERSION 3.15)

add_library(quickjs STATIC
        quickjs.c
        cutils.c
        libregexp.c
        libunicode.c
        libbf.c
        )

# quickjs.c 的 BigInt/大数实现依赖 libbf。
set_property(TARGET quickjs PROPERTY C_STANDARD 11)
set_property(TARGET quickjs PROPERTY C_STANDARD_REQUIRED ON)
target_include_directories(quickjs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(quickjs PRIVATE
        _GNU_SOURCE
        CONFIG_BIGNUM
        CONFIG_VERSION="2024-01-13"
        )

if (PLATFORM_SWITCH)
    # borealis 的公共编译选项里才带 __SWITCH__，QuickJS 需要单独补上。
    target_compile_definitions(quickjs PRIVATE __SWITCH__)
endif ()
target_compile_options(quickjs PRIVATE
        -funsigned-char
        -Wno-sign-compare
        -Wno-unused-parameter
        )
