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
        source/smb_manager.cpp
        source/media_library.cpp
        source/api/miniapp_api.cpp
        source/install/package_manager.cpp
        source/pages/launcher_activity.cpp
        source/pages/launcher_view.cpp
        LIBS
        quickjs
        yogacore
        smb2
        mongoose
        ${MINIAPP_LIBS}
        )

# 容器版本号：显示在启动页标题旁，和后台上传时填的 version 保持一致。
set(NXMINIAPP_VERSION "1.6.5")
target_compile_definitions(miniapp PRIVATE NXMINIAPP_VERSION="${NXMINIAPP_VERSION}")

if(PLATFORM_SWITCH AND BUILTIN_NSP)
    target_include_directories(miniapp PRIVATE ${MINIAPP_INCLUDES})
    target_compile_definitions(miniapp PRIVATE BUILTIN_NSP)
    # 先构建转发器 NSP 并放进资源目录，再打包 NRO romfs。
    add_dependencies(miniapp.nro nsp_forwarder.nsp)
endif()

# macOS：把可执行文件与资源打包成 .app，双击图标即可启动。
# 布局沿用 borealis 的资源约定：工作目录 = Contents/MacOS，资源在其上一级的 resources/。
if(PLATFORM_DESKTOP AND APPLE)
    set(MINIAPP_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/Switch小程序.app")
    add_custom_target(miniapp.app
            COMMAND ${CMAKE_COMMAND} -E make_directory
            "${MINIAPP_BUNDLE}/Contents/MacOS"
            "${MINIAPP_BUNDLE}/Contents/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:miniapp>
            "${MINIAPP_BUNDLE}/Contents/MacOS/miniapp"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/resources"
            "${MINIAPP_BUNDLE}/Contents/resources"
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist"
            "${MINIAPP_BUNDLE}/Contents/Info.plist"
            COMMAND sips -s format icns "${NXKIT_RESOURCES_DIR}/img/miniapp_icon.jpg"
            --out "${MINIAPP_BUNDLE}/Contents/Resources/app.icns"
            COMMAND codesign --force --sign - "${MINIAPP_BUNDLE}"
            )
    add_dependencies(miniapp.app miniapp)
endif()

