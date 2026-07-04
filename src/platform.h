#ifndef OPENBLOCKS_PLATFORM_H
#define OPENBLOCKS_PLATFORM_H

// OB_TOUCH selects the touch-first mobile frontend: the adaptive portrait
// layout, on-screen control buttons, and tap-driven menus. It is enabled on
// Android and on the WebAssembly build (which targets mobile browsers but also
// accepts keyboard for desktop browsers). Desktop native builds leave it unset
// and use the fixed-canvas landscape layout with keyboard input.
//
// raylib defines PLATFORM_ANDROID / PLATFORM_WEB for its own sources; our build
// passes the matching -D for the game translation units.
#if defined(PLATFORM_ANDROID) || defined(PLATFORM_WEB)
#define OB_TOUCH 1
#endif

#endif // OPENBLOCKS_PLATFORM_H
