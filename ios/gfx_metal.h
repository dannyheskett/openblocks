#ifndef OPENBLOCKS_GFX_METAL_H
#define OPENBLOCKS_GFX_METAL_H

#import <QuartzCore/CAMetalLayer.h>

// Wire the Metal backend to the view's layer, and tell it the drawable size in
// pixels. Called from the iOS app shell (ios_main.mm). The gfx_* primitives
// (gfx.h) are implemented on top of this in gfx_metal.mm.
void gfx_metal_attach(CAMetalLayer* layer);
void gfx_metal_resize(int width_px, int height_px);

#endif // OPENBLOCKS_GFX_METAL_H
