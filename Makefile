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

SRC := src/main.c src/game.c src/input.c src/render.c src/gfx_raylib.c src/sound.c \
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
ANDROID_BUILD_TOOLS  ?= 35.0.0
ANDROID_PLATFORM_VER ?= 35

# versionCode must be a monotonically increasing integer for Play uploads; drive
# it off the release number (unique + monotonic). Clamp to >=1 for local builds
# where OPENBLOCKS_VERSION is 0 (no release tags yet). versionName is the
# human-facing string. Both are injected at package time (aapt/aapt2 flags), so
# the manifest values are just fallbacks.
ANDROID_VERSION_CODE ?= $(OPENBLOCKS_VERSION)
ifeq ($(ANDROID_VERSION_CODE),0)
ANDROID_VERSION_CODE := 1
endif
ANDROID_VERSION_NAME ?= 1.0.$(ANDROID_VERSION_CODE)

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
#
# -z max-page-size=16384 gives the .so 16 KB-aligned LOAD segments. Google Play
# requires 16 KB page-size support for apps targeting Android 15+ (devices with
# 16 KB memory pages); NDK r26's linker still defaults to 4 KB, so we set it
# explicitly. Verified after linking (check_elf_align.sh).
ANDROID_LDFLAGS := -shared -L$(RAYLIB_ANDROID)/lib -lraylib \
                   -Wl,--wrap=fopen \
                   -Wl,-z,max-page-size=16384,-z,common-page-size=16384 \
                   -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm -lc -ldl

ANDROID_OBJ_DIR := build/obj-android
ANDROID_OBJ     := $(ANDROID_SRC:src/%.c=$(ANDROID_OBJ_DIR)/%.o) \
                   $(ANDROID_OBJ_DIR)/native_app_glue.o

ANDROID_APK_DIR  := build/android
ANDROID_LIB      := $(ANDROID_APK_DIR)/lib/$(ANDROID_ABI)/libopenblocks.so
ANDROID_APK      := build/openblocks.apk
ANDROID_KEYSTORE ?= build/debug.keystore

# One tiny Java class (OpenblocksActivity, a NativeActivity subclass that hides
# the system bars for a truly full-screen game) is compiled to a classes.dex and
# bundled. No Gradle: javac -> d8, both from the JDK + SDK build-tools already on
# the CI runner. The C game is unchanged; the activity just sets up immersive
# mode and NativeActivity loads libopenblocks.so as before.
ANDROID_JAVA_SRC := android/java/com/openblocks/game/OpenblocksActivity.java
ANDROID_DEX      := build/dex/classes.dex
JAVAC            ?= javac

android: $(ANDROID_APK)

$(ANDROID_OBJ_DIR)/native_app_glue.o: $(NATIVE_APP_GLUE)/android_native_app_glue.c | $(ANDROID_OBJ_DIR)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -c $< -o $@

$(ANDROID_OBJ_DIR)/%.o: src/%.c | $(ANDROID_OBJ_DIR)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -MMD -MP -c $< -o $@

$(ANDROID_LIB): $(ANDROID_OBJ)
	@mkdir -p $(dir $@)
	$(ANDROID_CC) $(ANDROID_OBJ) -o $@ $(ANDROID_LDFLAGS)
	@scripts/check_elf_align.sh $(ANDROID_TOOLCHAIN)/bin/llvm-readelf $@

# Compile OpenblocksActivity.java against the platform jar, then dex it. d8 emits
# classes.dex into build/dex/. -source/-target 8 keeps the bytecode dex-friendly;
# android.jar on the classpath resolves the framework APIs (java.* comes from the
# JDK's own boot classpath).
$(ANDROID_DEX): $(ANDROID_JAVA_SRC)
	@rm -rf build/java-classes && mkdir -p build/java-classes $(dir $@)
	$(JAVAC) -source 1.8 -target 1.8 -Xlint:-options \
	    -classpath $(ANDROID_JAR) -d build/java-classes $(ANDROID_JAVA_SRC)
	$(ANDROID_SDK_BT)/d8 --min-api $(ANDROID_API) --lib $(ANDROID_JAR) \
	    --output build/dex build/java-classes/com/openblocks/game/*.class

# Throwaway debug keystore for signing. Real distributable builds sign with a
# keystore supplied from a CI secret instead.
$(ANDROID_KEYSTORE):
	@mkdir -p $(dir $@)
	keytool -genkeypair -keystore $@ -storepass android -keypass android \
	    -alias openblocks -keyalg RSA -keysize 2048 -validity 10000 \
	    -dname "CN=openblocks, O=openblocks, C=US"

$(ANDROID_APK): $(ANDROID_LIB) $(ANDROID_DEX) $(ANDROID_KEYSTORE) \
                android/AndroidManifest.xml android/res/values/styles.xml
	# -S compiles android/res (the custom theme that enables edge-to-edge).
	$(ANDROID_SDK_BT)/aapt package -f -M android/AndroidManifest.xml \
	    -S android/res -I $(ANDROID_JAR) \
	    --version-code $(ANDROID_VERSION_CODE) --version-name $(ANDROID_VERSION_NAME) \
	    -F build/openblocks.unaligned.apk
	# Store the native lib at lib/<abi>/ inside the APK (path is relative to cwd).
	(cd $(ANDROID_APK_DIR) && $(ANDROID_SDK_BT)/aapt add \
	    ../../build/openblocks.unaligned.apk lib/$(ANDROID_ABI)/libopenblocks.so)
	# Store classes.dex at the APK root (path relative to cwd = build/dex).
	(cd build/dex && $(ANDROID_SDK_BT)/aapt add \
	    ../openblocks.unaligned.apk classes.dex)
	$(ANDROID_SDK_BT)/zipalign -f 4 \
	    build/openblocks.unaligned.apk build/openblocks.aligned.apk
	$(ANDROID_SDK_BT)/apksigner sign --ks $(ANDROID_KEYSTORE) \
	    --ks-pass pass:android --key-pass pass:android \
	    --out $@ build/openblocks.aligned.apk
	@rm -f build/openblocks.unaligned.apk build/openblocks.aligned.apk
	@echo "[android] built $@"

# ---------------------------------------------------------------------------
# Android App Bundle (.aab) for Google Play. Play only accepts AABs for new
# apps, and the legacy `aapt` (v1) above cannot emit one, so this path uses
# `aapt2` (proto resources) + `bundletool`. Kept fully separate from the
# sideload APK target: same libopenblocks.so, different packaging + a real
# upload key. Signed with the upload key; Google's Play App Signing re-signs the
# delivered APKs, so this signature only has to satisfy the Play upload check.
#
# Signing defaults to the throwaway debug keystore so `make dist-android-play`
# works locally to exercise the pipeline; CI overrides PLAY_* with the real
# upload keystore (from a secret) to produce an uploadable bundle.
# ---------------------------------------------------------------------------
ANDROID_AAB        := build/openblocks.aab
BUNDLETOOL_VERSION ?= 1.17.2
BUNDLETOOL         ?= build/bundletool.jar

PLAY_KEYSTORE   ?= $(ANDROID_KEYSTORE)
PLAY_KEY_ALIAS  ?= openblocks
PLAY_STORE_PASS ?= android
PLAY_KEY_PASS   ?= android

android-play: $(ANDROID_AAB)

$(BUNDLETOOL):
	@mkdir -p $(dir $@)
	curl -fsSL -o $@ \
	    https://github.com/google/bundletool/releases/download/$(BUNDLETOOL_VERSION)/bundletool-all-$(BUNDLETOOL_VERSION).jar

$(ANDROID_AAB): $(ANDROID_LIB) $(ANDROID_DEX) $(BUNDLETOOL) $(PLAY_KEYSTORE) \
                android/AndroidManifest.xml android/res/values/styles.xml
	@rm -rf build/aab && mkdir -p build/aab/module/manifest \
	    build/aab/module/lib/$(ANDROID_ABI) build/aab/module/dex
	# Compile android/res, then link into a *protobuf* APK (bundletool's input).
	$(ANDROID_SDK_BT)/aapt2 compile --dir android/res -o build/aab/res.zip
	$(ANDROID_SDK_BT)/aapt2 link --proto-format -o build/aab/proto.apk \
	    -I $(ANDROID_JAR) --manifest android/AndroidManifest.xml \
	    -R build/aab/res.zip --auto-add-overlay \
	    --version-code $(ANDROID_VERSION_CODE) --version-name $(ANDROID_VERSION_NAME)
	# Re-lay the proto APK into bundletool's base-module layout, add the .so.
	cd build/aab && unzip -qo proto.apk -d proto
	mv build/aab/proto/AndroidManifest.xml build/aab/module/manifest/AndroidManifest.xml
	mv build/aab/proto/resources.pb        build/aab/module/resources.pb
	mv build/aab/proto/res                 build/aab/module/res
	cp $(ANDROID_LIB) build/aab/module/lib/$(ANDROID_ABI)/libopenblocks.so
	cp $(ANDROID_DEX) build/aab/module/dex/classes.dex
	cd build/aab/module && zip -qr ../module.zip manifest resources.pb res lib dex
	java -jar $(BUNDLETOOL) build-bundle --modules=build/aab/module.zip --output=$@
	# Sign the bundle (JAR signature) with the upload key.
	jarsigner -keystore $(PLAY_KEYSTORE) -storepass $(PLAY_STORE_PASS) \
	    -keypass $(PLAY_KEY_PASS) -sigalg SHA256withRSA -digestalg SHA-256 \
	    $@ $(PLAY_KEY_ALIAS)
	@echo "[android] built $@ (versionCode $(ANDROID_VERSION_CODE), versionName $(ANDROID_VERSION_NAME))"

# ---------------------------------------------------------------------------
# Web build (WebAssembly via Emscripten). CI-only: needs emcc (emsdk) on PATH.
# Reuses the mobile touch UI (OB_TOUCH) plus keyboard, and drives the loop from
# emscripten_set_main_loop. The recorder is stubbed, so the encode_h264/mux TUs
# and minih264/minimp4 includes are dropped (as on Android). Outputs
# build/web/openblocks.{html,js,wasm} with the mobile shell in web/shell.html.
# ---------------------------------------------------------------------------
RAYLIB_WEB := third_party/raylib-install-web

WEB_SRC     := $(filter-out src/encode_h264.c src/encode_mux.c,$(SRC))
WEB_CFLAGS  := -std=c99 -Wall -Wextra -Isrc -DPLATFORM_WEB -Os \
               -I$(RAYLIB_WEB)/include
# Fixed memory (not ALLOW_MEMORY_GROWTH): a growable WASM heap yields resizable
# ArrayBuffers, which modern browsers reject in WebGL texImage2D. 64 MiB is ample
# for this game.
WEB_LDFLAGS := -Os -sUSE_GLFW=3 -sINITIAL_MEMORY=67108864 \
               --shell-file web/shell.html $(RAYLIB_WEB)/lib/libraylib.a

WEB_OUT_DIR := build/web
WEB_OUT     := $(WEB_OUT_DIR)/openblocks.html

web: $(WEB_OUT)

$(WEB_OUT): $(WEB_SRC) $(wildcard src/*.h) web/shell.html | $(WEB_OUT_DIR)
	emcc $(WEB_CFLAGS) $(WEB_SRC) -o $@ $(WEB_LDFLAGS)
	@echo "[web] built $@"

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

# The APK is a self-contained installable, so it ships as-is (versioned name).
dist-android: $(ANDROID_APK)
	@mkdir -p $(DIST)
	cp $(ANDROID_APK) $(DIST)/openblocks-$(VERSION_SLUG)-android-arm64.apk

# The Play upload artifact: the signed App Bundle (see the android-play target).
dist-android-play: $(ANDROID_AAB)
	@mkdir -p $(DIST)
	cp $(ANDROID_AAB) $(DIST)/openblocks-$(VERSION_SLUG)-android.aab

# The web build ships as a zip of the HTML/JS/WASM (serve over http to play).
dist-web: $(WEB_OUT)
	@rm -rf $(STAGING)/web && mkdir -p $(STAGING)/web/openblocks-$(VERSION_SLUG)-web $(DIST)
	cp $(WEB_OUT_DIR)/openblocks.html $(WEB_OUT_DIR)/openblocks.js $(WEB_OUT_DIR)/openblocks.wasm \
	    $(STAGING)/web/openblocks-$(VERSION_SLUG)-web/
	cp $(DOCS) $(STAGING)/web/openblocks-$(VERSION_SLUG)-web/
	(cd $(STAGING)/web && zip -qr ../../../$(DIST)/openblocks-$(VERSION_SLUG)-web-wasm.zip openblocks-$(VERSION_SLUG)-web)

# ---------------------------------------------------------------------------
$(OBJ_DIR) $(REL_OBJ_DIR) $(WIN64_OBJ_DIR) $(WIN32_OBJ_DIR) $(MAC_OBJ_DIR) $(ANDROID_OBJ_DIR) $(WEB_OUT_DIR):
	mkdir -p $@

clean:
	rm -rf build dist

# Pull in auto-generated header dependencies (ignored if not yet present).
-include $(OBJ:.o=.d) $(REL_OBJ:.o=.d) $(WIN64_OBJ:.o=.d) $(WIN32_OBJ:.o=.d) $(MAC_OBJ:.o=.d)

.PHONY: all run release run-release windows mac test dist dist-linux dist-windows dist-mac clean
