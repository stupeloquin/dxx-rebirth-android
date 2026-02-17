#!/bin/bash
set -e

echo "=== DXX-Rebirth Android Build Script ==="

export ANDROID_HOME=/opt/android-sdk
export ANDROID_SDK_ROOT=$ANDROID_HOME
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/26.1.10909125
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_NDK_HOME:$ANDROID_HOME/cmake/3.28.3/bin:$PATH"

# Install CMake 3.28.3 if not present (needed to avoid try_compile bug in 3.22)
if [ ! -f $ANDROID_HOME/cmake/3.28.3/bin/cmake ]; then
    echo "=== Installing CMake 3.28.3 ==="
    yes | sdkmanager --install "cmake;3.28.3" 2>/dev/null || true
    if [ ! -f $ANDROID_HOME/cmake/3.28.3/bin/cmake ]; then
        # Fallback: download directly
        CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz"
        wget -q "$CMAKE_URL" -O /tmp/cmake.tar.gz
        mkdir -p $ANDROID_HOME/cmake/3.28.3
        tar xzf /tmp/cmake.tar.gz -C $ANDROID_HOME/cmake/3.28.3 --strip-components=1
        rm /tmp/cmake.tar.gz
    fi
fi

DXX_ROOT=/build/dxx-rebirth
ANDROID_PROJECT=/build/dxx-android
DEPS=/build/dxx-android/deps

# ============================================================
# Step 1: Download dependencies
# ============================================================
echo "=== Downloading dependencies ==="
mkdir -p $DEPS && cd $DEPS

if [ ! -d SDL2-2.30.10 ]; then
    echo "Downloading SDL2..."
    wget -q https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz
    tar xzf SDL2-2.30.10.tar.gz
fi

if [ ! -d SDL2_mixer-2.8.0 ]; then
    echo "Downloading SDL2_mixer..."
    wget -q https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.0/SDL2_mixer-2.8.0.tar.gz
    tar xzf SDL2_mixer-2.8.0.tar.gz
fi

if [ ! -d physfs-3.2.0 ]; then
    echo "Downloading PhysFS..."
    wget -q https://github.com/icculus/physfs/archive/refs/tags/release-3.2.0.tar.gz -O physfs-3.2.0.tar.gz
    tar xzf physfs-3.2.0.tar.gz
    mv physfs-release-3.2.0 physfs-3.2.0
fi

if [ ! -f vk_mem_alloc.h ]; then
    echo "Downloading Vulkan Memory Allocator..."
    wget -q https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.1.0/include/vk_mem_alloc.h
fi

# ============================================================
# Step 2: Set up Android project from SDL2 template
# ============================================================
echo "=== Setting up Android project ==="
if [ ! -d $ANDROID_PROJECT/app ]; then
    cp -r $DEPS/SDL2-2.30.10/android-project/* $ANDROID_PROJECT/
fi

# Create JNI directory structure and symlinks
mkdir -p $ANDROID_PROJECT/app/jni

# Symlink SDL2 source (relative paths so they work on both host and container)
if [ ! -e $ANDROID_PROJECT/app/jni/SDL ]; then
    ln -sf ../../deps/SDL2-2.30.10 $ANDROID_PROJECT/app/jni/SDL
fi

# Symlink SDL2_mixer source
if [ ! -e $ANDROID_PROJECT/app/jni/SDL2_mixer ]; then
    ln -sf ../../deps/SDL2_mixer-2.8.0 $ANDROID_PROJECT/app/jni/SDL2_mixer
fi

# ============================================================
# Step 3: Cross-compile PhysFS for Android x86_64
# ============================================================
echo "=== Cross-compiling PhysFS ==="
PHYSFS_BUILD=/tmp/physfs-build-android
PHYSFS_INSTALL=$ANDROID_PROJECT/app/jni/physfs-install

if [ ! -f $PHYSFS_INSTALL/lib/libphysfs.a ]; then
    mkdir -p $PHYSFS_BUILD $PHYSFS_INSTALL
    cd $PHYSFS_BUILD
    $ANDROID_HOME/cmake/3.28.3/bin/cmake \
        -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=${TARGET_ABI:-x86_64} \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DPHYSFS_BUILD_SHARED=OFF \
        -DPHYSFS_BUILD_STATIC=ON \
        -DPHYSFS_BUILD_TEST=OFF \
        -DPHYSFS_BUILD_DOCS=OFF \
        -DCMAKE_INSTALL_PREFIX=$PHYSFS_INSTALL \
        $DEPS/physfs-3.2.0
    make -j$(nproc)
    make install
    echo "PhysFS installed to $PHYSFS_INSTALL"
else
    echo "PhysFS already built, skipping"
fi

# ============================================================
# Step 3b: Cross-compile libADLMIDI for Android (OPL3 MIDI synthesizer)
# ============================================================
echo "=== Cross-compiling libADLMIDI ==="
ADLMIDI_INSTALL=$ANDROID_PROJECT/app/src/main/jniLibs/${TARGET_ABI:-x86_64}

if [ ! -f $ADLMIDI_INSTALL/libADLMIDI.so ]; then
    # Download libADLMIDI if not present
    if [ ! -d $DEPS/libADLMIDI-1.5.1 ]; then
        echo "Downloading libADLMIDI..."
        wget -q https://github.com/Wohlstand/libADLMIDI/archive/refs/tags/v1.5.1.tar.gz -O $DEPS/libADLMIDI-1.5.1.tar.gz
        cd $DEPS && tar xzf libADLMIDI-1.5.1.tar.gz
    fi

    ADLMIDI_BUILD=/tmp/adlmidi-build-android
    mkdir -p $ADLMIDI_BUILD $ADLMIDI_INSTALL
    cd $ADLMIDI_BUILD
    $ANDROID_HOME/cmake/3.28.3/bin/cmake \
        -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=${TARGET_ABI:-x86_64} \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DlibADLMIDI_STATIC=OFF \
        -DlibADLMIDI_SHARED=ON \
        -DWITH_EMBEDDED_BANKS=ON \
        -DWITH_GENADLDATA=OFF \
        -DWITH_MIDIPLAY=OFF \
        -DWITH_VLC_PLUGIN=OFF \
        -DWITH_OLD_UTILS=OFF \
        -DEXAMPLE_SDL2_AUDIO=OFF \
        $DEPS/libADLMIDI-1.5.1
    make -j$(nproc)
    cp libADLMIDI.so $ADLMIDI_INSTALL/
    echo "libADLMIDI installed to $ADLMIDI_INSTALL"
else
    echo "libADLMIDI already built, skipping"
fi

# ============================================================
# Step 4: Generate kconfig.udlr.h
# ============================================================
echo "=== Generating kconfig.udlr.h ==="
KCONFIG_SRC=$DXX_ROOT/similar/main/kconfig.ui-table.cpp
KCONFIG_GEN=$DXX_ROOT/similar/main/generate-kconfig-udlr.py
KCONFIG_OUTPUT=$ANDROID_PROJECT/app/jni/src/kconfig.udlr.h

if [ ! -f $KCONFIG_OUTPUT ] || [ $KCONFIG_SRC -nt $KCONFIG_OUTPUT ]; then
    KCONFIG_I=/tmp/kconfig.ui-table.i
    if command -v g++ &> /dev/null; then
        CXX_CMD=g++
    elif command -v c++ &> /dev/null; then
        CXX_CMD=c++
    else
        echo "No host C++ compiler found, installing..."
        apt-get update -qq && apt-get install -y -qq g++ > /dev/null 2>&1
        CXX_CMD=g++
    fi

    $CXX_CMD -E -std=c++20 \
        -DDXX_BUILD_DESCENT=1 \
        -DDXX_KCONFIG_UI_ENUM=DXX_KCONFIG_UI_ENUM \
        -DDXX_KCONFIG_UI_LABEL=DXX_KCONFIG_UI_LABEL \
        -Dkc_item=kc_item \
        -I$ANDROID_PROJECT/app/jni/src \
        -I$DXX_ROOT/common/include \
        -I$DXX_ROOT/common/main \
        -I$DXX_ROOT/d1x-rebirth/main \
        -I$DXX_ROOT \
        $KCONFIG_SRC > $KCONFIG_I

    python3 $KCONFIG_GEN $KCONFIG_I $KCONFIG_OUTPUT
    echo "Generated $KCONFIG_OUTPUT"
else
    echo "kconfig.udlr.h already generated, skipping"
fi

# ============================================================
# Step 4b: Compile Vulkan SPIR-V shaders
# ============================================================
echo "=== Compiling Vulkan shaders ==="
SHADER_DIR=$DXX_ROOT/similar/arch/vk/shaders
SHADER_OUT=$ANDROID_PROJECT/app/jni/src

# Install glslangValidator if not present
if ! command -v glslangValidator &> /dev/null; then
    echo "Installing glslang-tools..."
    apt-get update -qq && apt-get install -y -qq glslang-tools > /dev/null 2>&1
fi

# Function to compile a shader to a C header with embedded SPIR-V
compile_shader() {
    local input=$1
    local name=$2
    local output=$SHADER_OUT/${name}_spv.h
    local spv_tmp=/tmp/${name}.spv

    if [ ! -f "$output" ] || [ "$input" -nt "$output" ]; then
        echo "  Compiling $input -> $output"
        glslangValidator -V --target-env vulkan1.1 -o "$spv_tmp" "$input"

        # Generate C header from SPIR-V binary
        echo "// Auto-generated from $input" > "$output"
        echo "#include <cstdint>" >> "$output"
        # Use extern const for external linkage (const has internal linkage in C++ by default)
        echo "extern const uint32_t ${name}_spv[] = {" >> "$output"
        python3 -c "
import struct, sys
with open('$spv_tmp', 'rb') as f:
    data = f.read()
words = struct.unpack('<' + 'I' * (len(data) // 4), data)
for i, w in enumerate(words):
    if i % 8 == 0:
        sys.stdout.write('    ')
    sys.stdout.write('0x%08x,' % w)
    if i % 8 == 7:
        sys.stdout.write('\n')
    else:
        sys.stdout.write(' ')
if len(words) % 8 != 0:
    sys.stdout.write('\n')
" >> "$output"
        echo "};" >> "$output"
        echo "extern const uint32_t ${name}_spv_size = sizeof(${name}_spv);" >> "$output"
        rm -f "$spv_tmp"
    else
        echo "  $output already up to date"
    fi
}

compile_shader "$SHADER_DIR/basic.vert" "basic_vert"
compile_shader "$SHADER_DIR/basic.frag" "basic_frag"
compile_shader "$SHADER_DIR/textured.vert" "textured_vert"
compile_shader "$SHADER_DIR/textured.frag" "textured_frag"

# Copy VMA header to build location
cp $DEPS/vk_mem_alloc.h $ANDROID_PROJECT/app/jni/src/vk_mem_alloc.h 2>/dev/null || true

# ============================================================
# Step 5: Configure Android project build files
# ============================================================
echo "=== Configuring Android project ==="

# Create local.properties
cat > $ANDROID_PROJECT/local.properties << 'LOCALEOF'
sdk.dir=/opt/android-sdk
LOCALEOF

# Create/update gradle.properties
cat > $ANDROID_PROJECT/gradle.properties << 'GRADLEPROPEOF'
org.gradle.jvmargs=-Xmx2048m
android.useAndroidX=true
GRADLEPROPEOF

# Write the app/build.gradle
cat > $ANDROID_PROJECT/app/build.gradle << 'BUILDEOF'
plugins {
    id 'com.android.application'
}

android {
    namespace "com.dxxrebirth.d1x"
    compileSdk 34
    ndkVersion "26.1.10909125"

    defaultConfig {
        applicationId "com.dxxrebirth.d1x"
        minSdk 24
        targetSdk 34
        versionCode 1
        versionName "0.61.0"

        ndk {
            abiFilters 'PLACEHOLDER_ABI'
        }

        externalNativeBuild {
            cmake {
                arguments "-DANDROID_STL=c++_shared",
                          "-DANDROID_PLATFORM=android-24"
                cppFlags "-std=c++20"
            }
        }
    }

    buildTypes {
        release {
            minifyEnabled false
        }
    }

    externalNativeBuild {
        cmake {
            path "jni/src/CMakeLists.txt"
            version "3.28.3"
        }
    }

    sourceSets {
        main {
            java.srcDirs = ['src/main/java']
        }
    }

    lint {
        abortOnError false
    }
}

dependencies {
}
BUILDEOF
sed -i "s/PLACEHOLDER_ABI/${TARGET_ABI:-x86_64}/" $ANDROID_PROJECT/app/build.gradle

# Write top-level build.gradle
cat > $ANDROID_PROJECT/build.gradle << 'TOPBUILDEOF'
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath 'com.android.tools.build:gradle:8.1.1'
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}
TOPBUILDEOF

# Write settings.gradle
cat > $ANDROID_PROJECT/settings.gradle << 'SETTINGSEOF'
include ':app'
SETTINGSEOF

# Write gradle wrapper properties
mkdir -p $ANDROID_PROJECT/gradle/wrapper
cat > $ANDROID_PROJECT/gradle/wrapper/gradle-wrapper.properties << 'WRAPPEREOF'
distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl=https\://services.gradle.org/distributions/gradle-8.5-bin.zip
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
WRAPPEREOF

# AndroidManifest.xml
mkdir -p $ANDROID_PROJECT/app/src/main
cat > $ANDROID_PROJECT/app/src/main/AndroidManifest.xml << 'MANIFESTEOF'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:tools="http://schemas.android.com/tools">

    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />
    <!-- Android 11+ broad file access for reading game data from /sdcard/dxx-rebirth/ -->
    <uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE"
        tools:ignore="ScopedStorage" />

    <uses-feature android:glEsVersion="0x00010001" android:required="true" />

    <application
        android:allowBackup="true"
        android:label="D1X-Rebirth"
        android:hasCode="true"
        android:hardwareAccelerated="true"
        android:requestLegacyExternalStorage="true">
        <activity
            android:name="com.dxxrebirth.d1x.DxxActivity"
            android:configChanges="keyboard|keyboardHidden|orientation|screenSize|screenLayout|uiMode"
            android:screenOrientation="sensorLandscape"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
MANIFESTEOF

# Copy SDL2 Java source files
mkdir -p $ANDROID_PROJECT/app/src/main/java/org/libsdl/app
cp $DEPS/SDL2-2.30.10/android-project/app/src/main/java/org/libsdl/app/*.java \
   $ANDROID_PROJECT/app/src/main/java/org/libsdl/app/

# Copy DxxActivity.java (SDLActivity subclass for storage permission)
mkdir -p $ANDROID_PROJECT/app/src/main/java/com/dxxrebirth/d1x
cp $DXX_ROOT/android/DxxActivity.java \
   $ANDROID_PROJECT/app/src/main/java/com/dxxrebirth/d1x/DxxActivity.java

# ============================================================
# Step 6: Set up the CMake build to find SDL2 and SDL2_mixer properly
# ============================================================

# The SDL2 android-project template normally uses Android.mk (ndk-build).
# We need to build SDL2 and SDL2_mixer as part of our CMake build.
# Update CMakeLists.txt to use add_subdirectory for SDL2 and SDL2_mixer.

cat > $ANDROID_PROJECT/app/jni/src/CMakeLists.txt << 'CMAKEOF'
cmake_minimum_required(VERSION 3.28)
project(d1x-rebirth C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
    # ADLMIDI dynamic loader (OPL3 MIDI synthesizer)
    ${DXX_SRC_ROOT}/common/music/adlmidi_dynamic.cpp
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
    # Our hand-crafted dxxsconf.h, generated kconfig.udlr.h, VMA, SPIR-V headers
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
    dl
    m
)
CMAKEOF

# ============================================================
# Step 7: Set up Gradle wrapper from SDL2 template
# ============================================================
echo "=== Setting up Gradle ==="
cd $ANDROID_PROJECT

# Copy the gradle wrapper from the SDL2 android-project template
SDL2_TEMPLATE=$DEPS/SDL2-2.30.10/android-project
mkdir -p $ANDROID_PROJECT/gradle/wrapper
cp $SDL2_TEMPLATE/gradlew $ANDROID_PROJECT/gradlew
cp $SDL2_TEMPLATE/gradlew.bat $ANDROID_PROJECT/gradlew.bat 2>/dev/null || true
cp $SDL2_TEMPLATE/gradle/wrapper/gradle-wrapper.jar $ANDROID_PROJECT/gradle/wrapper/gradle-wrapper.jar
cp $SDL2_TEMPLATE/gradle/wrapper/gradle-wrapper.properties $ANDROID_PROJECT/gradle/wrapper/gradle-wrapper.properties
chmod +x $ANDROID_PROJECT/gradlew

# ============================================================
# Step 8: Build the APK
# ============================================================
echo "=== Building APK ==="
cd $ANDROID_PROJECT
./gradlew assembleDebug --no-daemon --stacktrace 2>&1

echo "=== Build Complete ==="
ls -la $ANDROID_PROJECT/app/build/outputs/apk/debug/ 2>/dev/null || echo "APK not found - build may have failed"
