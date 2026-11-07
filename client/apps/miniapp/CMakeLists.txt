# miniapp: WXML/WXSS + QuickJS 小程序容器 spike。
set(MINIAPP_LIBS "")
set(MINIAPP_INCLUDES "")

if(PLATFORM_SWITCH AND BUILTIN_NSP)
    # nspmini: 运行时把内置 NSP 转发器安装到 SD 卡，生成桌面图标。
    set(NSPMINI_DIR ${CMAKE_SOURCE_DIR}/external/nspmini)
    list(APPEND MINIAPP_LIBS ${NSPMINI_DIR}/lib/libnsp.a)
    list(APPEND MINIAPP_INCLUDES ${NSPMINI_DIR}/include)
    add_subdirectory(${CMAKE_SOURCE_DIR}/../scripts/switch-forwarder
                     ${CMAKE_BINARY_DIR}/forwarder)
endif()

nxkit_add_app(miniapp
        TITLEID "0x010FF000FFFF0103"
        TITLE "Switch小程序"
        AUTHOR "xbw12138"
        ICON ${PROJECT_SOURCE_DIR}/resources/img/miniapp_icon.jpg
        SOURCES
        source/main.cpp
        source/miniapp_activity.cpp
        source/miniapp_view.cpp
        source/layout.cpp
        source/wxml_parser.cpp
        source/wxss.cpp
        source/style.cpp
        source/json.cpp
        source/js_runtime.cpp
        source/api/miniapp_api.cpp
        source/install/package_manager.cpp
        source/pages/launcher_activity.cpp
        source/pages/launcher_view.cpp
        LIBS
        quickjs
        yogacore
        ${MINIAPP_LIBS}
        )

if(PLATFORM_SWITCH AND BUILTIN_NSP)
    target_include_directories(miniapp PRIVATE ${MINIAPP_INCLUDES})
    target_compile_definitions(miniapp PRIVATE BUILTIN_NSP)
    # 先构建转发器 NSP 并放进资源目录，再打包 NRO romfs。
    add_dependencies(miniapp.nro nsp_forwarder.nsp)
endif()
