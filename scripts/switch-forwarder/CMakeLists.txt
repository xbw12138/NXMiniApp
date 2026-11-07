cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif ()
set(DEVKITPRO $ENV{DEVKITPRO} CACHE BOOL "DEVKITPRO")
include(${DEVKITPRO}/cmake/Switch.cmake REQUIRED)
set(ARCH "-march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE")
set(CMAKE_C_FLAGS "${ARCH} -g -Wall -O2 -ffunction-sections")
set(CMAKE_ASM_FLAGS "-g ${ARCH}")
set(CMAKE_EXE_LINKER_FLAGS "-L${DEVKITPRO}/libnx/lib -specs=${DEVKITPRO}/libnx/switch.specs -g ${ARCH}")
enable_language(ASM)

find_program(HACBREWPACK_EXE NAMES hacbrewpack
    HINTS "${CMAKE_SOURCE_DIR}/external/bin")
if (NOT HACBREWPACK_EXE)
    message(FATAL_ERROR "Could not find hacbrewpack: place a binary in external/bin/ or install https://github.com/The-4n/hacBrewPack")
endif()

project(nsp_forwarder)

# 转发器的 title id 必须与主 NRO 不同（主程序 010FF000FFFF0103）。
set(FORWARDER_TITLEID "010FF000FFFF0104")
set(FORWARDER_NAME "Switch小程序")
set(FORWARDER_AUTHOR "xbw12138")
set(FORWARDER_VERSION "1.0.0")
set(FORWARDER_ICON "${CMAKE_SOURCE_DIR}/resources/img/miniapp_icon.jpg")
set(FORWARDER_NPDM "${CMAKE_CURRENT_SOURCE_DIR}/forwarder.json")
# NSP 输出到资源目录，打包进 NRO romfs，运行时由 mini::InstallSD 安装到桌面。
set(FORWARDER_OUTPUT "${CMAKE_SOURCE_DIR}/resources/nsp_forwarder.nsp")

set(FORWARDER_SRC
    "${PROJECT_SOURCE_DIR}/source/main.c"
    "${PROJECT_SOURCE_DIR}/source/trampoline.s")

add_executable(${PROJECT_NAME} ${FORWARDER_SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE nx)
target_compile_definitions(${PROJECT_NAME} PRIVATE VERSION="1.0.0")

if (NOT PROJECT_KEYSET)
    # 优先用本地副本（不进 git），没有再尝试下载。
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/prod.keys")
        set(PROJECT_KEYSET "${CMAKE_CURRENT_SOURCE_DIR}/prod.keys")
    else()
        SET(PROJECT_KEYSET "${PROJECT_BINARY_DIR}/prod.keys")
        find_program(CURL_EXE NAMES curl)
        if (CURL_EXE)
            execute_process(COMMAND ${CURL_EXE} -sL -o "${PROJECT_KEYSET}"
                "https://raw.githubusercontent.com/externalEmu/s/master/prod.keys"
                RESULT_VARIABLE _curl_result)
        else()
            file(DOWNLOAD https://raw.githubusercontent.com/externalEmu/s/master/prod.keys ${PROJECT_KEYSET}
                STATUS _dl_status)
        endif()
        if (EXISTS "${PROJECT_KEYSET}")
            file(SIZE "${PROJECT_KEYSET}" _keys_size)
            if (_keys_size EQUAL 0)
                message(FATAL_ERROR "Failed to download prod.keys (empty file). Download manually from https://raw.githubusercontent.com/externalEmu/s/master/prod.keys and set -DPROJECT_KEYSET=/path/to/prod.keys")
            endif()
        else()
            message(FATAL_ERROR "Failed to download prod.keys. Download manually from https://raw.githubusercontent.com/externalEmu/s/master/prod.keys and set -DPROJECT_KEYSET=/path/to/prod.keys")
        endif()
    endif()
endif ()

add_custom_target(${PROJECT_NAME}.pfs0 DEPENDS ${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E make_directory "exefs"
    COMMAND ${NX_ELF2NSO_EXE} "${PROJECT_NAME}.elf" "exefs/main"
    COMMAND ${NX_NPDMTOOL_EXE} "${FORWARDER_NPDM}" "exefs/main.npdm"
)

add_custom_target(${PROJECT_NAME}.nsp DEPENDS ${PROJECT_NAME}.pfs0
    COMMAND ${CMAKE_COMMAND} -E make_directory "control"
    COMMAND ${NX_NACPTOOL_EXE} --create "${FORWARDER_NAME}" "${FORWARDER_AUTHOR}" "${FORWARDER_VERSION}" "control/control.nacp"
    COMMAND ${CMAKE_COMMAND} -E copy "${FORWARDER_ICON}" "control/icon_AmericanEnglish.dat"
    COMMAND ${HACBREWPACK_EXE} -k "${PROJECT_KEYSET}" --titleid ${FORWARDER_TITLEID} --titlename "${FORWARDER_NAME}" --noromfs --nologo --nostartupuser
    COMMAND ${CMAKE_COMMAND} -E rename "hacbrewpack_nsp/${FORWARDER_TITLEID}.nsp" "${FORWARDER_OUTPUT}"
)
