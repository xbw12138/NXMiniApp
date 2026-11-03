# nxkit_core: the reusable framework library (app bootstrap, config, http,
# cache, auth and the generic UI widgets).
file(GLOB_RECURSE NXKIT_CORE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)

add_library(nxkit_core STATIC ${NXKIT_CORE_SOURCE})
set_property(TARGET nxkit_core PROPERTY CXX_STANDARD 17)
target_include_directories(nxkit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(nxkit_core PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(nxkit_core PUBLIC
        borealis
        cpr::cpr
        qrcode
        lunasvg
        pystring
        z
        )

# -----------------------------------------------------------------------------
# Optional libmpv based video/audio player (nxkit/media/mpv_core.hpp).
# Enabled by default on Switch (devkitPro portlibs provide mpv), optional on
# desktop (-DNXKIT_WITH_MPV=ON, needs pkg-config to find mpv).
# -----------------------------------------------------------------------------
if(PLATFORM_SWITCH)
    set(NXKIT_WITH_MPV_DEFAULT ON)
else()
    set(NXKIT_WITH_MPV_DEFAULT OFF)
endif()
option(NXKIT_WITH_MPV "build the libmpv based media player" ${NXKIT_WITH_MPV_DEFAULT})

if(NXKIT_WITH_MPV)
    find_package(PkgConfig REQUIRED)
    pkg_search_module(MPV REQUIRED mpv)
    target_compile_definitions(nxkit_core PUBLIC NXKIT_WITH_MPV)
    target_include_directories(nxkit_core PUBLIC ${MPV_INCLUDE_DIRS})
    target_link_libraries(nxkit_core PUBLIC ${MPV_LIBRARIES})
    target_link_directories(nxkit_core PUBLIC ${MPV_LIBRARY_DIRS})
endif()
