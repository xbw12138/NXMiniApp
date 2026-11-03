# yoga: Yoga 1.19.0 的精简静态库（仅 flex 布局，不含测试工程）。
cmake_minimum_required(VERSION 3.15)

add_library(yoga STATIC
        Yoga.cpp
        YGNode.cpp
        YGConfig.cpp
        YGEnums.cpp
        YGStyle.cpp
        YGLayout.cpp
        YGValue.cpp
        YGNodePrint.cpp
        Utils.cpp
        log.cpp
        )

set_property(TARGET yoga PROPERTY CXX_STANDARD 14)
set_property(TARGET yoga PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(yoga PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
