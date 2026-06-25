# openblocks

A falling-block puzzle game written in C with Raylib.

## Building

### Linux/WSL2

```bash
make
make run
```

Or use the helper script, which builds (incrementally) and runs in one step:

```bash
./run.sh
```

### Release Build

```bash
make release
make run-release
```

### Windows Cross-Compile

Requires the mingw-w64 toolchain:

```bash
make windows
```

This produces:
- `build/openblocks-x64.exe` (64-bit)
- `build/openblocks-x86.exe` (32-bit)

### macOS

On a Mac (with the Xcode command-line tools), build a universal
(arm64 + x86_64) binary:

```bash
make mac    # -> build/openblocks-mac
```

## Tests

Unit tests cover the game logic (scoring, the gravity curve, and line
detection/collapse) with no raylib or window required:

```bash
make test
```

## Continuous Integration

Every pull request to `main` builds openblocks on Linux, Windows (mingw-w64
cross-compile, x64 + x86), and macOS (universal) via GitHub Actions
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)), and runs `make test`
on the Linux job. All checks must pass before a PR can merge.

Tagged releases (`release-N`) are cut by the
[`release`](.github/workflows/release.yml) workflow (manual dispatch), which
builds all three platforms and attaches prebuilt Linux, Windows (x64/x86), and
macOS (universal) archives to the GitHub Release.

## Controls

- **Left / Right** (arrows or A / D): move
- **Down** (arrow or S): soft drop
- **Space** (or Up / W / X / Z): rotate
- **Enter**: pause (press any key to resume)
- **Escape**: in-game, return to the menu (the game can be resumed); on the
  menu, exit
- **Alt+Enter**: toggle fullscreen
- **Up / Down** (or W / S): move the menu cursor; **Enter** (or Space): select

The window is a fixed 640x480 unless toggled to fullscreen. Sound is off by
default and can be toggled from the menu.

## Recording

openblocks can record a frame-fidelity H.264 MP4 of the session (one video
frame per rendered frame, constant 60 fps, no external tools). Recording is off
by default and produces no overhead when off.

- Toggle **Record: On/Off** from the menu (writes an auto-named
  `openblocks-YYYYMMDD-HHMMSS.mp4` in the working directory), or
- start recording from the command line:

```bash
./build/openblocks --record            # auto-named file
./build/openblocks --record clip.mp4   # explicit path
```

A red **REC** indicator shows on-screen while recording (it is not part of the
captured video). The file is finalized when recording is toggled off or the
program exits. Encoding is synchronous, so on slower machines the game may run
below real-time while recording; the video stays exactly one frame per frame.

## Dependencies

- A C99 compiler (GCC or Clang)
- [Raylib](https://github.com/raysan5/raylib) 6.0, built as a static library.
  The install directories are gitignored, so build raylib once on a fresh clone
  with the matching helper script:
  - Linux: `./scripts/build_raylib_linux.sh` → `third_party/raylib-install`
  - Windows cross (mingw-w64): `./scripts/build_raylib_windows.sh` →
    `third_party/raylib-install-win64` and `-win32`
  - macOS (universal): `./scripts/build_raylib_mac.sh` →
    `third_party/raylib-install-mac`
- For the Windows cross-compile: the mingw-w64 toolchain

Each script clones raylib (pinned via `RAYLIB_TAG`, default `6.0`) and installs
its headers and `libraylib.a` into the path the Makefile expects. CI runs the
same scripts before each platform build.

The MP4 recorder uses two vendored single-header libraries, both public domain
(CC0): [minih264](third_party/minih264) (H.264 encoder) and
[minimp4](third_party/minimp4) (MP4 muxer). No external tools or shared
libraries are required.

The MP4 recorder uses two vendored single-header libraries, both public domain
(CC0): [minih264](third_party/minih264) (H.264 encoder) and
[minimp4](third_party/minimp4) (MP4 muxer). No external tools or shared
libraries are required.

## License

openblocks' own code is released under the [MIT License](LICENSE). The vendored
`minih264` and `minimp4` libraries are public domain (CC0); see [NOTICE](NOTICE)
for attributions.

## Project Structure

```
openblocks/
├── src/           # Source files
├── third_party/   # Vendored single-header libs (minih264, minimp4)
├── Makefile       # Build configuration
├── run.sh         # Build-and-run helper
├── LICENSE        # MIT (this project's own code)
├── NOTICE         # Third-party attributions
└── README.md
```

## Notes

Sound effects are generated procedurally at startup (no audio files) for a
crunchy chiptune feel.
