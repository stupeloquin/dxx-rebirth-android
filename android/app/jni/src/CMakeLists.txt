cmake_minimum_required(VERSION 3.22)
project(d1x-rebirth C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Root of the DXX-Rebirth source tree
set(DXX_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dxx-rebirth")

# --------------------------------------------------------------------------
# Build SDL2 from source
# --------------------------------------------------------------------------
set(SDL2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SDL")
add_subdirectory(${SDL2_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/SDL2-build)

# --------------------------------------------------------------------------
# Build SDL2_mixer from source
# --------------------------------------------------------------------------
set(SDL2_MIXER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SDL2_mixer")

# SDL2_mixer needs to find SDL2
set(SDL2_DIR "${CMAKE_CURRENT_BINARY_DIR}/SDL2-build")
set(SDL2_INCLUDE_DIR "${SDL2_SOURCE_DIR}/include")

# Disable optional SDL2_mixer dependencies we don't need
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_WAVPACK OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3 OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OGG OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(${SDL2_MIXER_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/SDL2_mixer-build)

# --------------------------------------------------------------------------
# PhysFS (pre-built static library)
# --------------------------------------------------------------------------
set(PHYSFS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../physfs-install")
add_library(physfs STATIC IMPORTED)
set_target_properties(physfs PROPERTIES
    IMPORTED_LOCATION "${PHYSFS_ROOT}/lib/libphysfs.a"
)
set(PHYSFS_INCLUDE_DIR "${PHYSFS_ROOT}/include")

# --------------------------------------------------------------------------
# Source files - Library objects (common/maths)
# --------------------------------------------------------------------------
set(DXX_LIBRARY_SOURCES
    ${DXX_SRC_ROOT}/common/maths/fixc.cpp
    ${DXX_SRC_ROOT}/common/maths/tables.cpp
    ${DXX_SRC_ROOT}/common/maths/vecmat.cpp
)

# --------------------------------------------------------------------------
# Source files - Common objects (DXXCommon)
# --------------------------------------------------------------------------
set(DXX_COMMON_SOURCES
    ${DXX_SRC_ROOT}/common/2d/2dsline.cpp
    ${DXX_SRC_ROOT}/common/2d/bitblt.cpp
    ${DXX_SRC_ROOT}/common/2d/bitmap.cpp
    ${DXX_SRC_ROOT}/common/2d/box.cpp
    ${DXX_SRC_ROOT}/common/2d/canvas.cpp
    ${DXX_SRC_ROOT}/common/2d/circle.cpp
    ${DXX_SRC_ROOT}/common/2d/disc.cpp
    ${DXX_SRC_ROOT}/common/2d/font.cpp
    ${DXX_SRC_ROOT}/common/2d/gpixel.cpp
    ${DXX_SRC_ROOT}/common/2d/line.cpp
    ${DXX_SRC_ROOT}/common/2d/palette.cpp
    ${DXX_SRC_ROOT}/common/2d/pixel.cpp
    ${DXX_SRC_ROOT}/common/2d/rect.cpp
    ${DXX_SRC_ROOT}/common/2d/rle.cpp
    ${DXX_SRC_ROOT}/common/2d/scalec.cpp
    ${DXX_SRC_ROOT}/common/3d/draw.cpp
    ${DXX_SRC_ROOT}/common/3d/globvars.cpp
    ${DXX_SRC_ROOT}/common/3d/instance.cpp
    ${DXX_SRC_ROOT}/common/3d/matrix.cpp
    ${DXX_SRC_ROOT}/common/3d/points.cpp
    ${DXX_SRC_ROOT}/common/3d/rod.cpp
    ${DXX_SRC_ROOT}/common/3d/setup.cpp
    ${DXX_SRC_ROOT}/common/arch/sdl/event.cpp
    ${DXX_SRC_ROOT}/common/arch/sdl/joy.cpp
    ${DXX_SRC_ROOT}/common/arch/sdl/key.cpp
    ${DXX_SRC_ROOT}/common/arch/sdl/mouse.cpp
    ${DXX_SRC_ROOT}/common/arch/sdl/timer.cpp
    ${DXX_SRC_ROOT}/common/arch/sdl/window.cpp
    ${DXX_SRC_ROOT}/common/main/cglobal.cpp
    ${DXX_SRC_ROOT}/common/main/cli.cpp
    ${DXX_SRC_ROOT}/common/main/cmd.cpp
    ${DXX_SRC_ROOT}/common/main/cvar.cpp
    ${DXX_SRC_ROOT}/common/main/piggy.cpp
    ${DXX_SRC_ROOT}/common/maths/rand.cpp
    ${DXX_SRC_ROOT}/common/mem/mem.cpp
    ${DXX_SRC_ROOT}/common/misc/error.cpp
    ${DXX_SRC_ROOT}/common/misc/hash.cpp
    ${DXX_SRC_ROOT}/common/misc/hmp.cpp
    ${DXX_SRC_ROOT}/common/misc/ignorecase.cpp
    ${DXX_SRC_ROOT}/common/misc/physfsrwops.cpp
    ${DXX_SRC_ROOT}/common/misc/physfsx.cpp
    ${DXX_SRC_ROOT}/common/misc/strutil.cpp
    ${DXX_SRC_ROOT}/common/misc/vgrphys.cpp
    ${DXX_SRC_ROOT}/common/misc/vgwphys.cpp
    # OGL support
    ${DXX_SRC_ROOT}/common/arch/ogl/ogl_extensions.cpp
    ${DXX_SRC_ROOT}/common/arch/ogl/ogl_sync.cpp
    # SDL_mixer support
    ${DXX_SRC_ROOT}/common/arch/sdl/digi_mixer_music.cpp
    ${DXX_SRC_ROOT}/common/arch/sdl/jukebox.cpp
    # SDL2 messagebox (Linux/Android)
    ${DXX_SRC_ROOT}/common/arch/sdl/messagebox.cpp
    # Touch overlay for Android
    ${DXX_SRC_ROOT}/common/arch/sdl/touch.cpp
)

# --------------------------------------------------------------------------
# Source files - DXXProgram "similar" objects
# --------------------------------------------------------------------------
set(DXX_SIMILAR_SOURCES
    # OGL renderer
    ${DXX_SRC_ROOT}/similar/arch/ogl/gr.cpp
    ${DXX_SRC_ROOT}/similar/arch/ogl/ogl.cpp
    # SDL_mixer digi
    ${DXX_SRC_ROOT}/similar/arch/sdl/digi_mixer.cpp
    # Main similar sources
    ${DXX_SRC_ROOT}/similar/2d/palette.cpp
    ${DXX_SRC_ROOT}/similar/2d/pcx.cpp
    ${DXX_SRC_ROOT}/similar/3d/interp.cpp
    ${DXX_SRC_ROOT}/similar/arch/sdl/digi.cpp
    ${DXX_SRC_ROOT}/similar/arch/sdl/digi_audio.cpp
    ${DXX_SRC_ROOT}/similar/arch/sdl/init.cpp
    ${DXX_SRC_ROOT}/similar/main/ai.cpp
    ${DXX_SRC_ROOT}/similar/main/aipath.cpp
    ${DXX_SRC_ROOT}/similar/main/automap.cpp
    ${DXX_SRC_ROOT}/similar/main/bm.cpp
    ${DXX_SRC_ROOT}/similar/main/bmread.cpp
    ${DXX_SRC_ROOT}/similar/main/cntrlcen.cpp
    ${DXX_SRC_ROOT}/similar/main/collide.cpp
    ${DXX_SRC_ROOT}/similar/main/config.cpp
    ${DXX_SRC_ROOT}/similar/main/console.cpp
    ${DXX_SRC_ROOT}/similar/main/controls.cpp
    ${DXX_SRC_ROOT}/similar/main/credits.cpp
    ${DXX_SRC_ROOT}/similar/main/digiobj.cpp
    ${DXX_SRC_ROOT}/similar/main/effects.cpp
    ${DXX_SRC_ROOT}/similar/main/endlevel.cpp
    ${DXX_SRC_ROOT}/similar/main/fireball.cpp
    ${DXX_SRC_ROOT}/similar/main/fuelcen.cpp
    ${DXX_SRC_ROOT}/similar/main/fvi.cpp
    ${DXX_SRC_ROOT}/similar/main/game.cpp
    ${DXX_SRC_ROOT}/similar/main/gamecntl.cpp
    ${DXX_SRC_ROOT}/similar/main/gamefont.cpp
    ${DXX_SRC_ROOT}/similar/main/gamemine.cpp
    ${DXX_SRC_ROOT}/similar/main/gamerend.cpp
    ${DXX_SRC_ROOT}/similar/main/gamesave.cpp
    ${DXX_SRC_ROOT}/similar/main/gameseg.cpp
    ${DXX_SRC_ROOT}/similar/main/gameseq.cpp
    ${DXX_SRC_ROOT}/similar/main/gauges.cpp
    ${DXX_SRC_ROOT}/similar/main/hostage.cpp
    ${DXX_SRC_ROOT}/similar/main/hud.cpp
    ${DXX_SRC_ROOT}/similar/main/iff.cpp
    ${DXX_SRC_ROOT}/similar/main/inferno.cpp
    ${DXX_SRC_ROOT}/similar/main/kconfig.cpp
    ${DXX_SRC_ROOT}/similar/main/kmatrix.cpp
    ${DXX_SRC_ROOT}/similar/main/laser.cpp
    ${DXX_SRC_ROOT}/similar/main/lighting.cpp
    ${DXX_SRC_ROOT}/similar/main/menu.cpp
    ${DXX_SRC_ROOT}/similar/main/mglobal.cpp
    ${DXX_SRC_ROOT}/similar/main/mission.cpp
    ${DXX_SRC_ROOT}/similar/main/morph.cpp
    ${DXX_SRC_ROOT}/similar/main/multi.cpp
    ${DXX_SRC_ROOT}/similar/main/multibot.cpp
    ${DXX_SRC_ROOT}/similar/main/net_udp.cpp
    ${DXX_SRC_ROOT}/similar/main/newdemo.cpp
    ${DXX_SRC_ROOT}/similar/main/newmenu.cpp
    ${DXX_SRC_ROOT}/similar/main/object.cpp
    ${DXX_SRC_ROOT}/similar/main/paging.cpp
    ${DXX_SRC_ROOT}/similar/main/physics.cpp
    ${DXX_SRC_ROOT}/similar/main/piggy.cpp
    ${DXX_SRC_ROOT}/similar/main/player.cpp
    ${DXX_SRC_ROOT}/similar/main/playsave.cpp
    ${DXX_SRC_ROOT}/similar/main/polyobj.cpp
    ${DXX_SRC_ROOT}/similar/main/powerup.cpp
    ${DXX_SRC_ROOT}/similar/main/render.cpp
    ${DXX_SRC_ROOT}/similar/main/robot.cpp
    ${DXX_SRC_ROOT}/similar/main/scores.cpp
    ${DXX_SRC_ROOT}/similar/main/segment.cpp
    ${DXX_SRC_ROOT}/similar/main/slew.cpp
    ${DXX_SRC_ROOT}/similar/main/songs.cpp
    ${DXX_SRC_ROOT}/similar/main/state.cpp
    ${DXX_SRC_ROOT}/similar/main/switch.cpp
    ${DXX_SRC_ROOT}/similar/main/terrain.cpp
    ${DXX_SRC_ROOT}/similar/main/texmerge.cpp
    ${DXX_SRC_ROOT}/similar/main/text.cpp
    ${DXX_SRC_ROOT}/similar/main/titles.cpp
    ${DXX_SRC_ROOT}/similar/main/vclip.cpp
    ${DXX_SRC_ROOT}/similar/main/vers_id.cpp
    ${DXX_SRC_ROOT}/similar/main/wall.cpp
    ${DXX_SRC_ROOT}/similar/main/weapon.cpp
    ${DXX_SRC_ROOT}/similar/misc/args.cpp
    ${DXX_SRC_ROOT}/similar/misc/physfsx.cpp
)

# --------------------------------------------------------------------------
# Source files - D1X-specific
# --------------------------------------------------------------------------
set(DXX_D1X_SOURCES
    ${DXX_SRC_ROOT}/d1x-rebirth/main/custom.cpp
    ${DXX_SRC_ROOT}/d1x-rebirth/main/snddecom.cpp
)

# --------------------------------------------------------------------------
# Build as shared library (libmain.so) for SDL2 Android
# --------------------------------------------------------------------------
add_library(main SHARED
    ${DXX_LIBRARY_SOURCES}
    ${DXX_COMMON_SOURCES}
    ${DXX_SIMILAR_SOURCES}
    ${DXX_D1X_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/build_env_stubs.cpp
)

# --------------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------------
target_include_directories(main PRIVATE
    # Our hand-crafted dxxsconf.h and generated kconfig.udlr.h
    ${CMAKE_CURRENT_SOURCE_DIR}
    # DXX-Rebirth include paths (matches SConstruct CPPPATH)
    ${DXX_SRC_ROOT}/common/include
    ${DXX_SRC_ROOT}/common/main
    ${DXX_SRC_ROOT}/d1x-rebirth/main
    ${DXX_SRC_ROOT}
    # Dependencies
    ${SDL2_SOURCE_DIR}/include
    ${SDL2_MIXER_SOURCE_DIR}/include
    ${PHYSFS_INCLUDE_DIR}
)

# --------------------------------------------------------------------------
# Compile definitions
# --------------------------------------------------------------------------
target_compile_definitions(main PRIVATE
    # Descent 1 build
    DXX_BUILD_DESCENT=1
    # PhysFS deprecation warnings
    PHYSFS_DEPRECATED=
    # PRIi64 etc.
    __STDC_FORMAT_MACROS
    # No share path
    DXX_USE_SHAREPATH=0
)

# --------------------------------------------------------------------------
# Compile options
# --------------------------------------------------------------------------
target_compile_options(main PRIVATE
    -funsigned-char
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-c99-designator
    -Wno-deprecated-declarations
    -Wno-implicit-const-int-float-conversion
)

# kconfig.cpp needs the generated kconfig.udlr.h included
set_source_files_properties(
    ${DXX_SRC_ROOT}/similar/main/kconfig.cpp
    PROPERTIES COMPILE_FLAGS "-include ${CMAKE_CURRENT_SOURCE_DIR}/kconfig.udlr.h"
)

# --------------------------------------------------------------------------
# Link libraries
# --------------------------------------------------------------------------
target_link_libraries(main PRIVATE
    SDL2
    SDL2_mixer
    physfs
    GLESv1_CM
    EGL
    log
    android
    m
)
