# ---------------------------------------------------------------------------
# openblocks build
#
# Per-object compilation with automatic header-dependency tracking (-MMD -MP).
# Each target (linux dev/release, win64, win32) builds into its own object dir
# so their differing flags never clash.
# ---------------------------------------------------------------------------
RAYLIB       := third_party/raylib-install
RAYLIB_WIN64 := third_party/raylib-install-win64
RAYLIB_WIN32 := third_party/raylib-install-win32

# Single-header video pipeline: minih264 (encoder) + minimp4 (muxer). No -l
# flag — both compile into their own dedicated translation units.
MINIH264_INC := third_party/minih264
MINIMP4_INC  := third_party/minimp4

SRC := src/main.c src/game.c src/input.c src/render.c src/sound.c \
       src/recorder.c src/encode_h264.c src/encode_mux.c

# Shared standard/warning flags and vendored-header include paths.
CFLAGS_COMMON := -std=c99 -Wall -Wextra -I$(MINIH264_INC) -I$(MINIMP4_INC) -Isrc

# Release version: a single integer. The release workflow passes
# OPENBLOCKS_VERSION explicitly; for local `make dist` it derives from the
# latest release-N tag (or 0 if there are none). Only used to name archives.
OPENBLOCKS_VERSION ?= $(shell git tag --list 'release-*' 2>/dev/null | sed -n 's/^release-\([1-9][0-9]*\)$$/\1/p' | sort -n | tail -1 | grep . || echo 0)
VERSION_SLUG       := build-$(OPENBLOCKS_VERSION)

# ---------------------------------------------------------------------------
# Linux (dev + release, static linking)
# ---------------------------------------------------------------------------
CFLAGS   := $(CFLAGS_COMMON) -O2 -I$(RAYLIB)/include
RELFLAGS := $(CFLAGS_COMMON) -O3 -I$(RAYLIB)/include
# Static link raylib and its dependencies.
LDFLAGS  := -L$(RAYLIB)/lib -Wl,-Bstatic -lraylib -Wl,-Bdynamic -lm -lpthread -ldl -lrt -lX11

OBJ_DIR     := build/obj
REL_OBJ_DIR := build/obj-release
OBJ     := $(SRC:src/%.c=$(OBJ_DIR)/%.o)
REL_OBJ := $(SRC:src/%.c=$(REL_OBJ_DIR)/%.o)

OUT         := build/openblocks
OUT_RELEASE := build/openblocks-release

all: $(OUT)

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	gcc $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT): $(OBJ)
	gcc $(OBJ) -o $@ $(LDFLAGS)

$(REL_OBJ_DIR)/%.o: src/%.c | $(REL_OBJ_DIR)
	gcc $(RELFLAGS) -MMD -MP -c $< -o $@

$(OUT_RELEASE): $(REL_OBJ)
	gcc $(REL_OBJ) -o $@ $(LDFLAGS)

release: $(OUT_RELEASE)

run: $(OUT)
	./$(OUT)

run-release: $(OUT_RELEASE)
	./$(OUT_RELEASE)

# ---------------------------------------------------------------------------
# Windows cross-compile (x64 + x86, static, fully self-contained)
# mingw-w64 predefines _WIN32, so no -D is needed.
# ---------------------------------------------------------------------------
WIN_CFLAGS  := $(CFLAGS_COMMON) -O2
WIN_LDFLAGS := -Wl,-Bstatic -lraylib -lopengl32 -lgdi32 -lwinmm -lpthread -Wl,-Bdynamic -mwindows -static -static-libgcc

WIN64_CC := x86_64-w64-mingw32-gcc
WIN32_CC := i686-w64-mingw32-gcc

WIN64_OBJ_DIR := build/obj-win64
WIN32_OBJ_DIR := build/obj-win32
WIN64_OBJ := $(SRC:src/%.c=$(WIN64_OBJ_DIR)/%.o)
WIN32_OBJ := $(SRC:src/%.c=$(WIN32_OBJ_DIR)/%.o)

OUT_WIN64 := build/openblocks-x64.exe
OUT_WIN32 := build/openblocks-x86.exe

windows: $(OUT_WIN64) $(OUT_WIN32)

$(WIN64_OBJ_DIR)/%.o: src/%.c | $(WIN64_OBJ_DIR)
	$(WIN64_CC) $(WIN_CFLAGS) -I$(RAYLIB_WIN64)/include -MMD -MP -c $< -o $@

$(OUT_WIN64): $(WIN64_OBJ)
	$(WIN64_CC) $(WIN64_OBJ) -o $@ -L$(RAYLIB_WIN64)/lib $(WIN_LDFLAGS)

$(WIN32_OBJ_DIR)/%.o: src/%.c | $(WIN32_OBJ_DIR)
	$(WIN32_CC) $(WIN_CFLAGS) -I$(RAYLIB_WIN32)/include -MMD -MP -c $< -o $@

$(OUT_WIN32): $(WIN32_OBJ)
	$(WIN32_CC) $(WIN32_OBJ) -o $@ -L$(RAYLIB_WIN32)/lib $(WIN_LDFLAGS)

# ---------------------------------------------------------------------------
# macOS build (universal arm64 + x86_64). CI-only: needs a macOS runner with an
# Xcode toolchain. raylib links several system frameworks for windowing, input,
# and OpenGL.
# ---------------------------------------------------------------------------
RAYLIB_MAC  := third_party/raylib-install-mac
MAC_CC      := clang
MAC_ARCHES  := -arch arm64 -arch x86_64
MAC_CFLAGS  := $(CFLAGS_COMMON) -O2 $(MAC_ARCHES) -I$(RAYLIB_MAC)/include
MAC_LDFLAGS := $(MAC_ARCHES) -L$(RAYLIB_MAC)/lib -lraylib -lpthread \
               -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL

MAC_OBJ_DIR := build/obj-mac
MAC_OBJ := $(SRC:src/%.c=$(MAC_OBJ_DIR)/%.o)
OUT_MAC := build/openblocks-mac

mac: $(OUT_MAC)

$(MAC_OBJ_DIR)/%.o: src/%.c | $(MAC_OBJ_DIR)
	$(MAC_CC) $(MAC_CFLAGS) -MMD -MP -c $< -o $@

$(OUT_MAC): $(MAC_OBJ)
	$(MAC_CC) $(MAC_OBJ) -o $@ $(MAC_LDFLAGS)

# ---------------------------------------------------------------------------
# Android build (NativeActivity APK, no Gradle). CI-only: needs the NDK + SDK
# build-tools, both provided by the setup-android action. Mirrors raylib's
# upstream Makefile.Android flow: cross-compile the game + the NDK's
# native_app_glue into libopenblocks.so, then package + sign an APK with
# aapt / zipalign / apksigner. The recorder is stubbed on Android, so the
# encode_h264/encode_mux TUs and the minih264/minimp4 include paths are dropped.
#
# Requires env: ANDROID_NDK, ANDROID_SDK_ROOT.
# ---------------------------------------------------------------------------
ANDROID_API          ?= 24
ANDROID_ABI          := arm64-v8a
ANDROID_BUILD_TOOLS  ?= 34.0.0
ANDROID_PLATFORM_VER ?= 34

RAYLIB_ANDROID := third_party/raylib-install-android/$(ANDROID_ABI)

ANDROID_TOOLCHAIN := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64
ANDROID_CC        := $(ANDROID_TOOLCHAIN)/bin/aarch64-linux-android$(ANDROID_API)-clang
NATIVE_APP_GLUE   := $(ANDROID_NDK)/sources/android/native_app_glue

ANDROID_SDK_BT := $(ANDROID_SDK_ROOT)/build-tools/$(ANDROID_BUILD_TOOLS)
ANDROID_JAR    := $(ANDROID_SDK_ROOT)/platforms/android-$(ANDROID_PLATFORM_VER)/android.jar

ANDROID_SRC     := $(filter-out src/encode_h264.c src/encode_mux.c,$(SRC))
ANDROID_CFLAGS  := -std=c99 -Wall -Wextra -Isrc -DPLATFORM_ANDROID -fPIC \
                   -I$(RAYLIB_ANDROID)/include -I$(NATIVE_APP_GLUE)
# raylib wraps fopen at link time (-Wl,--wrap=fopen) so file access routes
# through the Android asset manager; libraylib.a references __real_fopen, which
# only exists when this flag is present. Without it, dlopen of libopenblocks.so
# fails at launch with "cannot locate symbol __real_fopen".
ANDROID_LDFLAGS := -shared -L$(RAYLIB_ANDROID)/lib -lraylib \
                   -Wl,--wrap=fopen \
                   -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm -lc -ldl

ANDROID_OBJ_DIR := build/obj-android
ANDROID_OBJ     := $(ANDROID_SRC:src/%.c=$(ANDROID_OBJ_DIR)/%.o) \
                   $(ANDROID_OBJ_DIR)/native_app_glue.o

ANDROID_APK_DIR  := build/android
ANDROID_LIB      := $(ANDROID_APK_DIR)/lib/$(ANDROID_ABI)/libopenblocks.so
ANDROID_APK      := build/openblocks.apk
ANDROID_KEYSTORE ?= build/debug.keystore

android: $(ANDROID_APK)

$(ANDROID_OBJ_DIR)/native_app_glue.o: $(NATIVE_APP_GLUE)/android_native_app_glue.c | $(ANDROID_OBJ_DIR)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -c $< -o $@

$(ANDROID_OBJ_DIR)/%.o: src/%.c | $(ANDROID_OBJ_DIR)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -MMD -MP -c $< -o $@

$(ANDROID_LIB): $(ANDROID_OBJ)
	@mkdir -p $(dir $@)
	$(ANDROID_CC) $(ANDROID_OBJ) -o $@ $(ANDROID_LDFLAGS)

# Throwaway debug keystore for signing. Real distributable builds sign with a
# keystore supplied from a CI secret instead.
$(ANDROID_KEYSTORE):
	@mkdir -p $(dir $@)
	keytool -genkeypair -keystore $@ -storepass android -keypass android \
	    -alias openblocks -keyalg RSA -keysize 2048 -validity 10000 \
	    -dname "CN=openblocks, O=openblocks, C=US"

$(ANDROID_APK): $(ANDROID_LIB) $(ANDROID_KEYSTORE) android/AndroidManifest.xml \
                android/res/values/styles.xml
	# -S compiles android/res (the custom theme that enables edge-to-edge).
	$(ANDROID_SDK_BT)/aapt package -f -M android/AndroidManifest.xml \
	    -S android/res -I $(ANDROID_JAR) -F build/openblocks.unaligned.apk
	# Store the native lib at lib/<abi>/ inside the APK (path is relative to cwd).
	(cd $(ANDROID_APK_DIR) && $(ANDROID_SDK_BT)/aapt add \
	    ../../build/openblocks.unaligned.apk lib/$(ANDROID_ABI)/libopenblocks.so)
	$(ANDROID_SDK_BT)/zipalign -f 4 \
	    build/openblocks.unaligned.apk build/openblocks.aligned.apk
	$(ANDROID_SDK_BT)/apksigner sign --ks $(ANDROID_KEYSTORE) \
	    --ks-pass pass:android --key-pass pass:android \
	    --out $@ build/openblocks.aligned.apk
	@rm -f build/openblocks.unaligned.apk build/openblocks.aligned.apk
	@echo "[android] built $@"

# ---------------------------------------------------------------------------
# Unit tests (game logic only — no raylib/window needed). The test TU includes
# game.c directly to reach its file-static helpers.
# ---------------------------------------------------------------------------
TEST_BIN := build/test_game

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test_game.c $(wildcard src/*.c src/*.h) | $(OBJ_DIR)
	gcc $(CFLAGS_COMMON) -O0 -g tests/test_game.c -o $(TEST_BIN)

# ---------------------------------------------------------------------------
# Distribution archives. Each dist-<platform> stages the platform binary plus
# README.md + LICENSE + NOTICE and packages it under dist/. Driven by the
# release workflow; runnable locally for the platforms you can build.
# ---------------------------------------------------------------------------
DIST    := dist
STAGING := build/staging
DOCS    := README.md LICENSE NOTICE

dist: dist-linux dist-windows dist-mac

dist-linux: release
	@rm -rf $(STAGING)/linux && mkdir -p $(STAGING)/linux/openblocks-$(VERSION_SLUG) $(DIST)
	cp $(OUT_RELEASE) $(STAGING)/linux/openblocks-$(VERSION_SLUG)/openblocks
	cp $(DOCS) $(STAGING)/linux/openblocks-$(VERSION_SLUG)/
	(cd $(STAGING)/linux && tar -czf ../../../$(DIST)/openblocks-$(VERSION_SLUG)-linux-x86_64.tar.gz openblocks-$(VERSION_SLUG))

dist-windows: $(OUT_WIN64) $(OUT_WIN32)
	@rm -rf $(STAGING)/win && mkdir -p $(DIST) \
	    $(STAGING)/win/openblocks-$(VERSION_SLUG)-x64 \
	    $(STAGING)/win/openblocks-$(VERSION_SLUG)-x86
	cp $(OUT_WIN64) $(STAGING)/win/openblocks-$(VERSION_SLUG)-x64/openblocks.exe
	cp $(DOCS) $(STAGING)/win/openblocks-$(VERSION_SLUG)-x64/
	cp $(OUT_WIN32) $(STAGING)/win/openblocks-$(VERSION_SLUG)-x86/openblocks.exe
	cp $(DOCS) $(STAGING)/win/openblocks-$(VERSION_SLUG)-x86/
	(cd $(STAGING)/win && zip -qr ../../../$(DIST)/openblocks-$(VERSION_SLUG)-windows-x64.zip openblocks-$(VERSION_SLUG)-x64)
	(cd $(STAGING)/win && zip -qr ../../../$(DIST)/openblocks-$(VERSION_SLUG)-windows-x86.zip openblocks-$(VERSION_SLUG)-x86)

dist-mac: $(OUT_MAC)
	@rm -rf $(STAGING)/mac && mkdir -p $(STAGING)/mac/openblocks-$(VERSION_SLUG) $(DIST)
	cp $(OUT_MAC) $(STAGING)/mac/openblocks-$(VERSION_SLUG)/openblocks
	codesign --force --sign - $(STAGING)/mac/openblocks-$(VERSION_SLUG)/openblocks
	cp $(DOCS) $(STAGING)/mac/openblocks-$(VERSION_SLUG)/
	(cd $(STAGING)/mac && zip -qr ../../../$(DIST)/openblocks-$(VERSION_SLUG)-macos-universal.zip openblocks-$(VERSION_SLUG))

# ---------------------------------------------------------------------------
$(OBJ_DIR) $(REL_OBJ_DIR) $(WIN64_OBJ_DIR) $(WIN32_OBJ_DIR) $(MAC_OBJ_DIR) $(ANDROID_OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf build dist

# Pull in auto-generated header dependencies (ignored if not yet present).
-include $(OBJ:.o=.d) $(REL_OBJ:.o=.d) $(WIN64_OBJ:.o=.d) $(WIN32_OBJ:.o=.d) $(MAC_OBJ:.o=.d)

.PHONY: all run release run-release windows mac test dist dist-linux dist-windows dist-mac clean
