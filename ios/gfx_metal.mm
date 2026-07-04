// Native Metal backend for the gfx primitive layer (iOS) — no raylib.
//
// Phase 2 (this file, for now): frame lifecycle + clear only, which is enough to
// prove the whole Metal path works end-to-end in the Simulator (device, layer,
// drawable acquisition, a render pass, and present). The drawing primitives
// (gfx_rect/gfx_text/…) land in Phase 3 as a batched quad pipeline; for now they
// are no-ops so the interface links.
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#import "gfx.h"
#import "gfx_metal.h"

static id<MTLDevice>        s_device;
static id<MTLCommandQueue>  s_queue;
static CAMetalLayer*        s_layer;
static id<CAMetalDrawable>  s_drawable;
static id<MTLCommandBuffer> s_cmd;
static MTLClearColor        s_clear;

void gfx_metal_attach(CAMetalLayer* layer) {
    s_device = MTLCreateSystemDefaultDevice();
    s_queue  = [s_device newCommandQueue];
    layer.device          = s_device;
    layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    s_layer  = layer;
}

void gfx_metal_resize(int width_px, int height_px) {
    (void)width_px; (void)height_px; // drawableSize is set on the layer by the view
}

void gfx_begin_frame(void) {
    s_drawable = [s_layer nextDrawable];
    s_cmd      = [s_queue commandBuffer];
    s_clear    = MTLClearColorMake(0, 0, 0, 1);
}

void gfx_clear(Color c) {
    s_clear = MTLClearColorMake(c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
    if (!s_drawable) return;
    // A render pass whose only job is to clear the drawable to s_clear.
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = s_drawable.texture;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor  = s_clear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = [s_cmd renderCommandEncoderWithDescriptor:rp];
    [enc endEncoding];
}

void gfx_end_frame(void) {
    if (s_drawable) [s_cmd presentDrawable:s_drawable];
    [s_cmd commit];
    s_drawable = nil;
    s_cmd      = nil;
}

// --- Drawing primitives: Phase 3 (batched quads). No-ops for now. -----------
void gfx_rect(int x, int y, int w, int h, Color c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void gfx_rect_lines(int x, int y, int w, int h, Color c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void gfx_line(int x1, int y1, int x2, int y2, Color c) { (void)x1;(void)y1;(void)x2;(void)y2;(void)c; }
void gfx_rounded_rect(Rectangle r, float rn, int seg, Color c) { (void)r;(void)rn;(void)seg;(void)c; }
void gfx_rounded_rect_lines(Rectangle r, float rn, int seg, float th, Color c) { (void)r;(void)rn;(void)seg;(void)th;(void)c; }
void gfx_poly(Vector2 ctr, int sides, float rad, float rot, Color c) { (void)ctr;(void)sides;(void)rad;(void)rot;(void)c; }
void gfx_ring(Vector2 ctr, float in, float out, float a0, float a1, int seg, Color c) { (void)ctr;(void)in;(void)out;(void)a0;(void)a1;(void)seg;(void)c; }
void gfx_text(const char* t, int x, int y, int fs, Color c) { (void)t;(void)x;(void)y;(void)fs;(void)c; }
int  gfx_measure_text(const char* t, int fs) { (void)t;(void)fs; return 0; }
