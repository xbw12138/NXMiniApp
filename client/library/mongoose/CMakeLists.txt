cmake_minimum_required(VERSION 3.15)

add_library(mongoose STATIC ${CMAKE_CURRENT_SOURCE_DIR}/mongoose.c)

if (__SWITCH__)
    # PUBLIC：让 link mongoose 的 target（如 miniapp）也能看到 MG_ARCH，否则
    # 会走到 mongoose_custom.h 分支导致找不到头文件。
    target_compile_definitions(mongoose PUBLIC MG_ARCH=14)
endif()

target_include_directories(mongoose PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})