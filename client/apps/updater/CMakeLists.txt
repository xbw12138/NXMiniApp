# updater: 独立的容器更新/日志上传工具（不依赖 QuickJS）。
nxkit_add_app(updater
        TITLEID "0x010FF000FFFF0104"
        TITLE "Switch小程序更新工具"
        AUTHOR "xbw12138"
        ICON ${PROJECT_SOURCE_DIR}/resources/img/miniapp_icon.jpg
        SOURCES
        source/main.cpp
        source/updater_view.cpp
        source/updater_service.cpp
        )
