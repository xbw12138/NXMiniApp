# miniapp: WXML/WXSS + QuickJS 小程序容器 spike。
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
        # borealis 内置的 yogacore 已经提供 Yoga 头文件和实现；不能再链接
        # library/yoga，否则两套 Yoga 符号会在运行期互相覆盖。
        yogacore
        )
