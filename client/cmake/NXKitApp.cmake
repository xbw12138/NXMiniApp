# nxkit_add_app(<name>
#     [TITLEID   <title id>]
#     [TITLE     <display name>]
#     [AUTHOR    <author>]
#     [VERSION   <1.0.0>]
#     [ICON      <path to icon>]
#     SOURCES    <sources...>
#     [LIBS      <extra libs...>])
#
# Builds an executable from the given sources, links it against nxkit_core and
# applies the platform specific packaging rules (.nro for Switch, .data for
# desktop, .vpk for PSVita).
function(nxkit_add_app APP_NAME)
    cmake_parse_arguments(APP "" "TITLEID;TITLE;AUTHOR;VERSION;ICON" "SOURCES;LIBS" ${ARGN})

    if(NOT APP_SOURCES)
        message(FATAL_ERROR "nxkit_add_app(${APP_NAME}): no SOURCES given")
    endif()
    if(NOT APP_TITLE)
        set(APP_TITLE ${APP_NAME})
    endif()

    if(NOT APP_TITLEID)
        set(APP_TITLEID ${NXKIT_DEFAULT_TITLEID})
    endif()
    if(NOT APP_AUTHOR)
        set(APP_AUTHOR ${NXKIT_DEFAULT_AUTHOR})
    endif()
    if(NOT APP_VERSION)
        set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REVISION}")
    endif()
    if(NOT APP_ICON)
        set(APP_ICON ${NXKIT_DEFAULT_ICON})
    endif()

    set(APP_SRC ${APP_SOURCES})
    set(APP_LINK_LIBS nxkit_core ${APP_LIBS})

    if(PLATFORM_SWITCH)
        # Custom libnx init (sockets / romfs / sdmc mount) must live in the
        # executable itself so the linker keeps the userAppInit override.
        list(APPEND APP_SRC ${NXKIT_CORE_DIR}/source/platform/switch_init.c)
        list(APPEND APP_LINK_LIBS ${NXKIT_PLATFORM_LIBS})
    elseif(PLATFORM_PSV)
        list(APPEND APP_LINK_LIBS ${NXKIT_PLATFORM_LIBS})
    endif()

    add_executable(${APP_NAME} ${APP_SRC})
    set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)
    target_include_directories(${APP_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${NXKIT_CORE_DIR}/include
            )
    target_compile_options(${APP_NAME} PRIVATE -ffunction-sections -fdata-sections)
    target_link_libraries(${APP_NAME} PRIVATE ${APP_LINK_LIBS})

    # -------------------------------------------------------------------------
    # Packaging
    # -------------------------------------------------------------------------
    if(PLATFORM_DESKTOP)
        add_custom_target(${APP_NAME}.data
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${NXKIT_RESOURCES_DIR}
                ${CMAKE_CURRENT_BINARY_DIR}/resources
                )
        add_dependencies(${APP_NAME} ${APP_NAME}.data)
    elseif(PLATFORM_PSV)
        vita_create_self(${APP_NAME}.self ${APP_NAME} UNSAFE)
        vita_create_vpk(${APP_NAME}.vpk ${APP_TITLEID} ${APP_NAME}.self
                VERSION ${APP_VERSION}
                NAME ${APP_NAME}
                FILE ${NXKIT_RESOURCES_DIR} resources
                )
    elseif(PLATFORM_SWITCH)
        add_custom_target(${APP_NAME}.nro
                DEPENDS ${APP_NAME}
                COMMAND ${NX_NACPTOOL_EXE} --create "${APP_TITLE}"
                "${APP_AUTHOR}" "${APP_VERSION}" ${APP_NAME}.nacp --titleid=${APP_TITLEID}
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${NXKIT_RESOURCES_DIR} ${CMAKE_BINARY_DIR}/resources
                # Switch 的 RomFS 只保留一份主字体 font.ttf（任天堂系统字体，
                # 含 CJK）：applet 模式下 pl 共享字体不可用，borealis 会回退
                # 加载 romfs:/font/font.ttf，保证中文/图标任何启动方式都能显示。
                COMMAND ${CMAKE_COMMAND} -E copy
                ${NXKIT_RESOURCES_DIR}/font/switch_font.ttf
                ${CMAKE_BINARY_DIR}/resources/font/font.ttf
                COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/resources/font/switch_font.ttf
                COMMAND ${NX_ELF2NRO_EXE} ${APP_NAME}.elf ${APP_NAME}.nro --icon=${APP_ICON}
                --nacp=${APP_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
                )
    endif()
endfunction()
