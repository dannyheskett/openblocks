# openblocks

A falling-block puzzle game written in C. It runs natively on Windows, macOS,
Linux, Android, and iOS, and in the browser via WebAssembly.

Rendering, input, and audio go through raylib 6.0 on every platform except iOS,
which uses a native Metal backend with no raylib (see
[Architecture](#architecture)). The game logic (`src/game.c`) is
platform-independent and shared unchanged.

## Platforms

| Platform | Build | Renderer | Input |
|----------|-------|----------|-------|
| Linux / Windows / macOS | native (raylib) | landscape | keyboard |
| Web (WASM) | Emscripten (raylib) | landscape **or** portrait, chosen at runtime | keyboard + touch |
| Android | NativeActivity (raylib) | portrait | touch |
| iOS | native Metal (no raylib) | portrait | touch |

## Rendering modes

There are two renderers. Native desktop compiles only landscape; Android and iOS
compile only portrait; the web build compiles both and selects one at runtime.

- **Landscape** — a fixed 640×480 offscreen canvas, letterbox-scaled to the
  window (integer scale on native desktop, fractional on web). Three-column
  layout: piece statistics on the left, the playfield centered, and
  lines / score / level / next on the right.
- **Portrait** — adaptive to the live screen size. A title bar with a menu
  button is pinned to the top, a SCORE / LINES / LEVEL / NEXT band sits below it,
  the playfield fills the middle at the largest square cell that fits, and a row
  of on-screen buttons sits in a bottom control bar.

The web build picks the renderer from the pointer type
(`matchMedia('(pointer: coarse)')`): a phone or tablet gets portrait, a desktop
browser gets landscape. A **Controls: On/Off** menu item overrides the choice.

## Controls

**Keyboard** (desktop, and desktop browsers):

- **Left / Right** (or A / D): move
- **Down** (or S): soft drop
- **Space** (or Up / W / X / Z): rotate
- **Enter**: pause / resume
- **Escape**: return to the menu (the game stays resumable); on the menu, exit
- **Alt+Enter**: toggle fullscreen
- **Up / Down** (or W / S) + **Enter / Space**: menu navigation

**Touch** (Android, iOS, and mobile browsers):

- **◀ / ▶** buttons: move (hold to auto-repeat)
- **Rotate** button, or a tap on the playfield: rotate
- **Drop** button: tap for a hard drop, hold for a soft drop
- **☰** button (top-right): return to the menu
- **Swipe up / down** + **tap**: menu navigation and select

## Building

raylib is built once from source into a gitignored install directory (per
platform) before the game is built. Each `scripts/build_raylib_*.sh` clones
raylib (pinned via `RAYLIB_TAG`, default `6.0`) and installs its headers and
`libraylib.a`. CI runs these scripts before each build.

### Desktop

```bash
./scripts/build_raylib_linux.sh      # once, on a fresh clone
make                                 # -> build/openblocks   (dev, -O2)
make run
make release                         # -> build/openblocks-release (-O3)
```

Windows (mingw-w64 cross-compile) and macOS (universal arm64 + x86_64):

```bash
./scripts/build_raylib_windows.sh && make windows   # -> build/openblocks-x64.exe, -x86.exe
./scripts/build_raylib_mac.sh     && make mac        # -> build/openblocks-mac
```

### Android (needs the Android SDK + NDK)

```bash
./scripts/build_raylib_android.sh
make android        # -> build/openblocks.apk   (debug-signed, sideloadable)
make android-play   # -> build/openblocks.aab   (Play App Bundle; PLAY_* signing vars)
```

The app is a `NativeActivity` (no Gradle); a small `OpenblocksActivity` Java
class (compiled with `javac` + `d8`) enables immersive full-screen. arm64-v8a,
`targetSdk` 35, 16 KB-page aligned.

### iOS (needs macOS + Xcode; no raylib)

```bash
make ios-sim   # -> build/ios-sim/Openblocks.app   (Simulator, arm64)
make ios       # -> build/openblocks.ipa           (device arm64, unsigned)
```

The `.ipa` is unsigned; AWS Device Farm re-signs it on upload, and Sideloadly /
a free Apple ID can install it on a device. C sources build with `clang`, the
Objective-C++ backend (`ios/`) with `clang++`.

### Web (needs Emscripten)

```bash
./scripts/build_raylib_web.sh
make web        # -> build/web/openblocks.{html,js,wasm}
```

Serve `build/web` over HTTP (not `file://`) and open `openblocks.html`.

## Tests

Game-logic unit tests (scoring, gravity, line detection/collapse) with no raylib
or window required:

```bash
make test
```

## Continuous integration and releases

Every pull request to `main` builds all platforms via GitHub Actions
([`ci.yml`](.github/workflows/ci.yml)) — Linux, Windows (x64/x86), macOS
(universal), Android (APK + a packaging check of the AAB), Web (WASM), and iOS
(a Metal app booted in the Simulator and screenshotted) — and runs `make test`.
All checks must pass to merge.

Pushing to `main` cuts the next `release-N` via
[`release.yml`](.github/workflows/release.yml), which attaches per-platform
archives, the Android APK, the iOS `.ipa`, and the WASM bundle to the GitHub
Release. The web build is also published to
[danheskett.com/dist/openblocks](https://danheskett.com/dist/openblocks/).

## Recording (desktop only)

The desktop builds can record a frame-fidelity H.264 MP4 of the session (one
video frame per rendered frame, constant 60 fps, no external tools). Recording
is compiled out of the mobile and web builds.

- Toggle **Record: On/Off** from the menu (writes an auto-named
  `openblocks-YYYYMMDD-HHMMSS.mp4` in the working directory), or start it from
  the command line:

```bash
./build/openblocks --record            # auto-named file
./build/openblocks --record clip.mp4   # explicit path
```

A red **REC** indicator shows on-screen while recording (it is not part of the
captured video). Encoding is synchronous, so on slower machines the game may run
below real-time while recording; the video stays exactly one frame per frame.

## Architecture

- `src/game.c` — pure game logic (no rendering/input/audio), shared by all
  platforms and covered by `make test`.
- `src/render.c` draws through a small immediate-mode primitive layer
  (`src/gfx.h`): `src/gfx_raylib.c` wraps raylib (desktop / web / android);
  `ios/gfx_metal.mm` is a native Metal implementation. `src/ob_types.h` supplies
  raylib-compatible types so the shared code compiles without raylib on iOS.
- Audio is a similar seam (`src/audio.h`): `src/audio_raylib.c` (raylib) vs
  `ios/audio_ios.mm` (AVAudioEngine). Effects are synthesized at startup (no
  audio files); sound is off by default.
- iOS backend: `ios/plat_ios.mm` (touch / screen / timing), `ios/ios_main.mm`
  (UIKit app + `CAMetalLayer` view + `CADisplayLink` loop), and the embedded
  raylib default font (`src/font_atlas.h`, generated by
  `scripts/gen_font_atlas.c`) so iOS text matches the other platforms.

## Dependencies

- A C99 compiler (GCC or Clang); a C++ / Objective-C++ compiler for the iOS
  backend.
- [raylib](https://github.com/raysan5/raylib) 6.0 (static) on all platforms
  except iOS, built by the `scripts/build_raylib_*.sh` helpers.
- The MP4 recorder uses two vendored public-domain (CC0) single-header
  libraries: [minih264](third_party/minih264) (H.264 encoder) and
  [minimp4](third_party/minimp4) (MP4 muxer). No external tools or shared
  libraries.

## Project structure

```
openblocks/
├── src/            # shared C sources + gfx/audio raylib backends
├── ios/            # native Metal / UIKit backend (Objective-C++)
├── android/        # NativeActivity manifest, resources, Java activity
├── web/            # Emscripten HTML shell
├── scripts/        # raylib build scripts, asset/font generators
├── third_party/    # vendored single-header libs (minih264, minimp4)
├── tests/          # game-logic unit tests
├── docs/           # design notes
├── Makefile
├── LICENSE         # MIT (this project's own code)
└── NOTICE          # third-party attributions
```

## License

openblocks' own code is released under the [MIT License](LICENSE). The vendored
`minih264` and `minimp4` libraries are public domain (CC0); see [NOTICE](NOTICE)
for attributions.
