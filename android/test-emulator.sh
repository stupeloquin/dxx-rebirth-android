#!/bin/bash
set -e

export ANDROID_HOME=/opt/android-sdk
export PATH="$ANDROID_HOME/emulator:$ANDROID_HOME/platform-tools:$PATH"

echo "=== Starting Android Emulator (headless) ==="

# Start emulator in background, no window, no audio, no boot animation
emulator -avd dxx_test \
    -no-window \
    -no-audio \
    -no-boot-anim \
    -gpu swiftshader_indirect \
    -no-snapshot \
    -wipe-data \
    -accel on \
    -verbose \
    &

EMULATOR_PID=$!
echo "Emulator PID: $EMULATOR_PID"

# Wait for emulator to boot
echo "Waiting for emulator to boot..."
BOOT_TIMEOUT=180
ELAPSED=0
while [ $ELAPSED -lt $BOOT_TIMEOUT ]; do
    BOOT_COMPLETE=$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' || true)
    if [ "$BOOT_COMPLETE" = "1" ]; then
        echo "Emulator booted after ${ELAPSED}s"
        break
    fi
    sleep 5
    ELAPSED=$((ELAPSED + 5))
    echo "  Waiting... (${ELAPSED}s)"
done

if [ "$BOOT_COMPLETE" != "1" ]; then
    echo "ERROR: Emulator failed to boot within ${BOOT_TIMEOUT}s"
    kill $EMULATOR_PID 2>/dev/null || true
    exit 1
fi

# Give it a moment to settle
sleep 5

echo "=== Installing APK ==="
adb install /build/dxx-android/app/build/outputs/apk/debug/app-debug.apk

echo "=== Launching App ==="
adb shell am start -n com.dxxrebirth.d1x/org.libsdl.app.SDLActivity

# Wait for app to start
sleep 10

echo "=== Checking logcat ==="
adb logcat -d -s "SDL" "SDL/APP" "D1X" "libSDL" "SDLActivity" "DEBUG" "AndroidRuntime" | tail -100

echo "=== Checking if app process is running ==="
adb shell ps | grep -i "dxxrebirth\|sdl\|d1x" || echo "App process not found (may have crashed)"

echo "=== Full crash logs (if any) ==="
adb logcat -d | grep -E "FATAL|AndroidRuntime|signal|SIGSEGV|SIGABRT|SDL|libmain" | tail -50

echo "=== Test complete ==="

# Keep emulator running for a bit for further inspection
sleep 5
kill $EMULATOR_PID 2>/dev/null || true
wait $EMULATOR_PID 2>/dev/null || true
echo "Done"
