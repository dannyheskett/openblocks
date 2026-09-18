#ifndef OPENBLOCKS_PLATFORM_H
#define OPENBLOCKS_PLATFORM_H

// The game's name: window title and recording file prefix.
#define GAME_NAME "openblocks"

// Recording size (recorder.c): the landscape layout's 640x480 logical space.
// Both are multiples of 16 for the H.264 encoder.
#define REC_W 640
#define REC_H 480

// OB_TOUCH selects the touch-first mobile frontend: the adaptive portrait
// layout, on-screen control buttons, and tap-driven menus. It is enabled on
// Android, iOS and the WebAssembly build (which targets mobile browsers but also
// accepts keyboard for desktop browsers). Desktop native builds leave it unset
// and use the scaled landscape layout with keyboard input.
//
// raylib defines PLATFORM_ANDROID / PLATFORM_WEB for its own sources; our build
// passes the matching -D for the game translation units.
#if defined(PLATFORM_ANDROID) || defined(PLATFORM_WEB) || defined(PLATFORM_IOS)
#define OB_TOUCH 1
#endif

// The two renderers, selected by availability:
//   OB_PORTRAIT  — the touch-first portrait renderer (adaptive layout + on-screen
//                  buttons). Available on Android and web.
//   OB_LANDSCAPE — the desktop renderer (3-column layout in a 640x480 logical
//                  space, scaled to fit the window). Available on desktop native and web.
// Native desktop compiles only landscape; Android only portrait; the web build
// compiles BOTH and chooses at runtime (desktop browser -> landscape, phone ->
// portrait), so a laptop browser gets the same look as the native desktop app.
#ifdef OB_TOUCH
#define OB_PORTRAIT 1
#endif
#if !defined(PLATFORM_ANDROID) && !defined(PLATFORM_IOS)
#define OB_LANDSCAPE 1
#endif

// True only on the build that has both renderers and must pick at runtime (web).
#if defined(OB_PORTRAIT) && defined(OB_LANDSCAPE)
#define OB_RUNTIME_RENDERER 1
#endif

#endif // OPENBLOCKS_PLATFORM_H
