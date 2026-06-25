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

- A C99 compiler (GCC or compatible)
- [Raylib](https://github.com/raysan5/raylib), built as a static library — the
  install directories are **not** checked in, so build them before compiling:
  - `third_party/raylib-install` (Linux)
  - `third_party/raylib-install-win64` / `-win32` (Windows cross-builds)
- For the Windows cross-compile: the mingw-w64 toolchain

Build raylib as a static library from source and install its headers and
`libraylib.a` under `third_party/raylib-install/{include,lib}` (see the raylib
documentation for platform-specific build instructions). Repeat with the
mingw-w64 toolchain into `third_party/raylib-install-win64` and `-win32` for the
Windows targets.

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
