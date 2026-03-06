# DXX-Rebirth Android Build System
#
# Usage:
#   make                  Build D1X arm64 APK (for physical device)
#   make build-apk-x86   Build D1X x86_64 APK (for emulator)
#   make build-d2x       Build D2X arm64 APK
#   make build-d2x-x86   Build D2X x86_64 APK
#   make deploy           Install APK on connected device
#   make clean            Remove build container for current game
#   make clean-all        Remove build containers for both games
#
# Select game: DXX_GAME=d1x (default) or DXX_GAME=d2x
#
# One-time setup:
#   make docker-image     Build the Docker build image

.PHONY: build-apk build-apk-x86 build-d2x build-d2x-x86 \
        docker-image docker-image-emulator \
        gen-icons gen-app-icon deploy deploy-permissions deploy-data \
        emulator-start emulator-test clean clean-all _build

# --- Paths (resolved relative to Makefile location in android/) ---
MAKEFILE_DIR    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
DXX_ROOT        := $(abspath $(MAKEFILE_DIR)/..)
ANDROID_PROJECT := $(abspath $(MAKEFILE_DIR)/../../dxx-android)
ADB_KEYS        := $(HOME)/.android

# --- Game selection ---
DXX_GAME        ?= d1x

ifeq ($(DXX_GAME),d2x)
    DXX_PACKAGE := com.dxxrebirth.d2x
    GAME_DATA   := $(abspath $(MAKEFILE_DIR)/../../descent2/data)
else
    DXX_PACKAGE := com.dxxrebirth.d1x
    GAME_DATA   := $(abspath $(MAKEFILE_DIR)/../../descent/data)
endif

# --- Docker ---
DOCKER          := sg docker -c
BUILDER_IMAGE   := dxx-android-builder
EMULATOR_IMAGE  := dxx-android-emulator
CONTAINER        = dxx-builder-$(DXX_GAME)

# --- Build ---
TARGET_ABI      ?= arm64-v8a
APK             := $(ANDROID_PROJECT)/app/build/outputs/apk/debug/app-debug.apk

# ==== Build targets ====

build-apk: TARGET_ABI = arm64-v8a
build-apk: _build

build-apk-x86: TARGET_ABI = x86_64
build-apk-x86: _build

build-d2x: DXX_GAME = d2x
build-d2x: TARGET_ABI = arm64-v8a
build-d2x: _build

build-d2x-x86: DXX_GAME = d2x
build-d2x-x86: TARGET_ABI = x86_64
build-d2x-x86: _build

_build:
	-@$(DOCKER) "docker create --name $(CONTAINER) \
		-e TARGET_ABI=$(TARGET_ABI) \
		-e DXX_GAME=$(DXX_GAME) \
		-v $(DXX_ROOT):/build/dxx-rebirth \
		-v $(ANDROID_PROJECT):/build/dxx-android \
		$(BUILDER_IMAGE) \
		/build/dxx-rebirth/android/build-android.sh" 2>/dev/null
	$(DOCKER) "docker start -a $(CONTAINER)"

# ==== Docker image targets ====

docker-image:
	$(DOCKER) "docker build -t $(BUILDER_IMAGE) -f $(MAKEFILE_DIR)Dockerfile.android $(MAKEFILE_DIR)"

docker-image-emulator: docker-image
	$(DOCKER) "docker build -t $(EMULATOR_IMAGE) -f $(MAKEFILE_DIR)Dockerfile.emulator $(MAKEFILE_DIR)"

# ==== Generator targets ====

gen-icons:
	$(DOCKER) "docker run --rm \
		-v $(DXX_ROOT):/build/dxx-rebirth \
		$(BUILDER_IMAGE) \
		python3 /build/dxx-rebirth/android/gen_touch_icons.py"

gen-app-icon:
	$(DOCKER) "docker run --rm \
		-v $(DXX_ROOT):/build/dxx-rebirth \
		-v $(ANDROID_PROJECT):/build/dxx-android \
		-v $(GAME_DATA):/build/descent/data \
		$(BUILDER_IMAGE) \
		python3 /build/dxx-rebirth/android/gen_icon.py $(if $(filter d2x,$(DXX_GAME)),--d2)"

# ==== Deploy targets (adb via Docker with USB passthrough) ====

deploy:
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		-v $(ANDROID_PROJECT):/build/dxx-android \
		$(BUILDER_IMAGE) \
		bash -c 'adb wait-for-device && adb install -r /build/dxx-android/app/build/outputs/apk/debug/app-debug.apk'"

deploy-permissions:
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		$(BUILDER_IMAGE) \
		adb shell appops set $(DXX_PACKAGE) MANAGE_EXTERNAL_STORAGE allow"

deploy-data:
ifeq ($(DXX_GAME),d2x)
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		-v $(GAME_DATA):/build/game-data \
		$(BUILDER_IMAGE) \
		bash -c 'adb push /build/game-data/DESCENT2.HOG /sdcard/dxx-rebirth/data/ && \
			adb push /build/game-data/DESCENT2.HAM /sdcard/dxx-rebirth/data/ && \
			adb push /build/game-data/DESCENT2.S11 /sdcard/dxx-rebirth/data/ && \
			adb push /build/game-data/DESCENT2.S22 /sdcard/dxx-rebirth/data/ && \
			for f in /build/game-data/*.PIG; do adb push \"\$$f\" /sdcard/dxx-rebirth/data/; done'"
else
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		-v $(GAME_DATA):/build/game-data \
		$(BUILDER_IMAGE) \
		bash -c 'adb push /build/game-data/descent.hog /sdcard/dxx-rebirth/data/ && \
			adb push /build/game-data/descent.pig /sdcard/dxx-rebirth/data/'"
endif

# ==== Emulator targets ====

emulator-start:
	$(DOCKER) "docker run -d --name dxx-emulator \
		--device /dev/kvm \
		-v $(ANDROID_PROJECT):/build/dxx-android \
		$(EMULATOR_IMAGE) \
		bash -c 'emulator -avd dxx_test -no-window -no-audio -gpu swiftshader_indirect & sleep 300'"

emulator-test:
	$(DOCKER) "docker run --rm \
		--device /dev/kvm \
		-e DXX_PACKAGE=$(DXX_PACKAGE) \
		-v $(DXX_ROOT):/build/dxx-rebirth \
		-v $(ANDROID_PROJECT):/build/dxx-android \
		$(EMULATOR_IMAGE) \
		/build/dxx-rebirth/android/test-emulator.sh"

# ==== Cleanup ====

clean:
	-$(DOCKER) "docker rm -f $(CONTAINER)" 2>/dev/null

clean-all:
	-$(DOCKER) "docker rm -f dxx-builder-d1x" 2>/dev/null
	-$(DOCKER) "docker rm -f dxx-builder-d2x" 2>/dev/null
