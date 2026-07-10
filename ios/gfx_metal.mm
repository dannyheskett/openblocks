// Native Metal backend for the gfx primitive layer (iOS) — no raylib.
//
// Immediate-mode design: each frame the gfx_* calls append triangles (colored,
// or textured from a font atlas) to a CPU vertex list in pixel coordinates;
// gfx_end_frame uploads them and issues one draw. A single pipeline handles both
// solid fills (sampling a white texel in the atlas) and text (sampling glyph
// coverage), so there is one shader and one draw call per frame.
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vector>
#include <string.h>
#include <math.h>

#import "gfx.h"
#import "gfx_metal.h"
#include "font_atlas.h"  // raylib's default pixel font (generated), for parity

// --- Vertex layout ----------------------------------------------------------
struct GVert { float x, y, u, v, r, g, b, a; }; // 32 bytes; matches packed MSL

static id<MTLDevice>          s_device;
static id<MTLCommandQueue>    s_queue;
static CAMetalLayer*          s_layer;
static id<MTLRenderPipelineState> s_pipeline;
static id<MTLSamplerState>    s_sampler;
static id<MTLTexture>         s_atlas;
static int                    s_glyph_index[256]; // codepoint -> ob_font_glyphs index

static std::vector<GVert> s_verts;
static float s_cr = 0, s_cg = 0, s_cb = 0, s_ca = 1; // clear colour
static int   s_fw = 1, s_fh = 1;                      // full drawable (px)
static int   s_ox = 0, s_oy = 0;                      // safe-area origin (px)

// Triple-buffered vertex buffers, reused across frames (grown on demand) instead
// of allocating one per frame; the semaphore stops us overwriting a buffer the
// GPU is still reading.
#define OB_INFLIGHT 3
static id<MTLBuffer>        s_vbuf[OB_INFLIGHT];
static NSUInteger           s_vcap[OB_INFLIGHT];
static int                  s_frame_idx = 0;
static dispatch_semaphore_t s_inflight;

static const char* kShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Vertex   { packed_float2 pos; packed_float2 uv; packed_float4 color; };
struct Uniforms { float2 viewport; float2 origin; };
struct VOut     { float4 position [[position]]; float2 uv; float4 color; };
vertex VOut v_main(uint vid [[vertex_id]],
                   const device Vertex* verts [[buffer(0)]],
                   constant Uniforms& u [[buffer(1)]]) {
    Vertex v = verts[vid];
    float2 p = v.pos + u.origin; // shift the game's (0,0) to the safe-area corner
    float2 ndc = float2(p.x / u.viewport.x * 2.0 - 1.0,
                        1.0 - p.y / u.viewport.y * 2.0);
    VOut o; o.position = float4(ndc, 0.0, 1.0); o.uv = v.uv; o.color = v.color; return o;
}
fragment float4 f_main(VOut in [[stage_in]],
                       texture2d<float> atlas [[texture(0)]],
                       sampler samp [[sampler(0)]]) {
    float a = atlas.sample(samp, in.uv).r; // single-channel (R8) coverage
    return float4(in.color.rgb, in.color.a * a);
}
)";

// --- Setup ------------------------------------------------------------------
static void build_font_atlas(void) {
    // Upload raylib's default-font alpha atlas as a single-channel texture, with
    // the bottom-right pixel forced opaque to serve as the "white texel" that
    // solid primitives sample. Nearest sampling keeps the pixel font crisp (like
    // raylib) and makes that texel read exactly 1.0.
    static unsigned char atlas[OB_FONT_ATLAS_W * OB_FONT_ATLAS_H];
    memcpy(atlas, ob_font_atlas_alpha, sizeof(atlas));
    atlas[OB_FONT_ATLAS_W * OB_FONT_ATLAS_H - 1] = 255;

    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                           width:OB_FONT_ATLAS_W
                                                          height:OB_FONT_ATLAS_H mipmapped:NO];
    s_atlas = [s_device newTextureWithDescriptor:td];
    [s_atlas replaceRegion:MTLRegionMake2D(0, 0, OB_FONT_ATLAS_W, OB_FONT_ATLAS_H)
               mipmapLevel:0 withBytes:atlas bytesPerRow:OB_FONT_ATLAS_W];

    // Codepoint -> glyph-array index (fallback '?').
    for (int i = 0; i < 256; i++) s_glyph_index[i] = -1;
    for (int i = 0; i < OB_FONT_GLYPH_COUNT; i++) {
        int v = ob_font_glyphs[i].value;
        if (v >= 0 && v < 256) s_glyph_index[v] = i;
    }
}

// Glyph index for a codepoint, falling back to '?' then 0.
static int glyph_of(int cp) {
    int gi = (cp >= 0 && cp < 256) ? s_glyph_index[cp] : -1;
    if (gi < 0) gi = s_glyph_index[(unsigned char)'?'];
    if (gi < 0) gi = 0;
    return gi;
}

void gfx_metal_attach(CAMetalLayer* layer) {
    s_device = MTLCreateSystemDefaultDevice();
    s_queue  = [s_device newCommandQueue];
    s_inflight = dispatch_semaphore_create(OB_INFLIGHT);
    layer.device          = s_device;
    layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    s_layer  = layer;

    NSError* err = nil;
    id<MTLLibrary> lib = [s_device newLibraryWithSource:[NSString stringWithUTF8String:kShader]
                                                options:nil error:&err];
    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction   = [lib newFunctionWithName:@"v_main"];
    pd.fragmentFunction = [lib newFunctionWithName:@"f_main"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    s_pipeline = [s_device newRenderPipelineStateWithDescriptor:pd error:&err];

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterNearest; // crisp pixel font, like raylib
    sd.magFilter = MTLSamplerMinMagFilterNearest;
    s_sampler = [s_device newSamplerStateWithDescriptor:sd];

    build_font_atlas();
}

void gfx_metal_set_viewport(int full_w, int full_h, int origin_x, int origin_y) {
    s_fw = full_w > 0 ? full_w : 1;
    s_fh = full_h > 0 ? full_h : 1;
    s_ox = origin_x;
    s_oy = origin_y;
}

// --- Vertex helpers ---------------------------------------------------------
static inline void uv_white(float* u, float* v) {
    // Centre of the forced-opaque bottom-right texel (nearest-sampled).
    *u = (OB_FONT_ATLAS_W - 0.5f) / (float)OB_FONT_ATLAS_W;
    *v = (OB_FONT_ATLAS_H - 0.5f) / (float)OB_FONT_ATLAS_H;
}

static inline void push(float x, float y, float u, float v, Color c) {
    GVert g = { x, y, u, v, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    s_verts.push_back(g);
}

// Solid triangle (uv fixed to the white texel).
static void tri_solid(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    float u, v; uv_white(&u, &v);
    push(x0, y0, u, v, c); push(x1, y1, u, v, c); push(x2, y2, u, v, c);
}

static void quad_solid(float x, float y, float w, float h, Color c) {
    tri_solid(x, y, x + w, y, x + w, y + h, c);
    tri_solid(x, y, x + w, y + h, x, y + h, c);
}

// --- Frame lifecycle --------------------------------------------------------
void gfx_begin_frame(void) {
    s_verts.clear();
    s_cr = s_cg = s_cb = 0; s_ca = 1;
}

void gfx_clear(Color c) {
    s_cr = c.r / 255.0f; s_cg = c.g / 255.0f; s_cb = c.b / 255.0f; s_ca = c.a / 255.0f;
}

void gfx_end_frame(void) {
    id<CAMetalDrawable> drawable = [s_layer nextDrawable];
    if (!drawable) return;

    dispatch_semaphore_wait(s_inflight, DISPATCH_TIME_FOREVER);
    int fi = s_frame_idx;
    s_frame_idx = (s_frame_idx + 1) % OB_INFLIGHT;

    id<MTLCommandBuffer> cmd = [s_queue commandBuffer];
    __block dispatch_semaphore_t sem = s_inflight;
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull b) { (void)b; dispatch_semaphore_signal(sem); }];

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = drawable.texture;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor  = MTLClearColorMake(s_cr, s_cg, s_cb, s_ca);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];

    if (!s_verts.empty() && s_pipeline) {
        NSUInteger need = s_verts.size() * sizeof(GVert);
        if (s_vcap[fi] < need) {
            s_vbuf[fi] = [s_device newBufferWithLength:need options:MTLResourceStorageModeShared];
            s_vcap[fi] = need;
        }
        memcpy(s_vbuf[fi].contents, s_verts.data(), need);
        float uni[4] = { (float)s_fw, (float)s_fh, (float)s_ox, (float)s_oy };
        [enc setRenderPipelineState:s_pipeline];
        [enc setVertexBuffer:s_vbuf[fi] offset:0 atIndex:0];
        [enc setVertexBytes:uni length:sizeof(uni) atIndex:1];
        [enc setFragmentTexture:s_atlas atIndex:0];
        [enc setFragmentSamplerState:s_sampler atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:s_verts.size()];
    }
    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
}

// --- Primitives -------------------------------------------------------------
void gfx_rect(int x, int y, int w, int h, Color c) { quad_solid(x, y, w, h, c); }

void gfx_rect_lines(int x, int y, int w, int h, Color c) {
    quad_solid(x, y, w, 1, c);             // top
    quad_solid(x, y + h - 1, w, 1, c);     // bottom
    quad_solid(x, y, 1, h, c);             // left
    quad_solid(x + w - 1, y, 1, h, c);     // right
}

void gfx_line(int x1, int y1, int x2, int y2, Color c) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) { quad_solid(x1, y1, 1, 1, c); return; }
    float nx = -dy / len * 0.5f, ny = dx / len * 0.5f; // half-thickness normal (1px)
    tri_solid(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, c);
    tri_solid(x1 + nx, y1 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, c);
}

// Text: a faithful port of raylib's DrawText -> DrawTextEx (spacing = fontSize /
// baseSize as an int, scaleFactor = fontSize / baseSize), drawing each glyph's
// atlas rect at its offset. Produces pixel-identical output to the other
// platforms.
void gfx_text(const char* text, int x, int y, int font_size, Color c) {
    int fs = font_size < OB_FONT_BASE_SIZE ? OB_FONT_BASE_SIZE : font_size;
    int spacing = fs / OB_FONT_BASE_SIZE;
    float scale = (float)fs / OB_FONT_BASE_SIZE;
    float pen = (float)x;
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        int cp = *p;
        OBGlyph g = ob_font_glyphs[glyph_of(cp)];
        if (cp != ' ') {
            float gx = pen + g.ox * scale, gy = y + g.oy * scale;
            float gw = g.rw * scale,       gh = g.rh * scale;
            float u0 = g.rx / (float)OB_FONT_ATLAS_W, v0 = g.ry / (float)OB_FONT_ATLAS_H;
            float u1 = (g.rx + g.rw) / (float)OB_FONT_ATLAS_W;
            float v1 = (g.ry + g.rh) / (float)OB_FONT_ATLAS_H;
            push(gx,      gy,      u0, v0, c); push(gx + gw, gy,      u1, v0, c); push(gx + gw, gy + gh, u1, v1, c);
            push(gx,      gy,      u0, v0, c); push(gx + gw, gy + gh, u1, v1, c); push(gx,      gy + gh, u0, v1, c);
        }
        float adv = (g.adv != 0) ? (float)g.adv : g.rw; // DrawTextEx uses recs.width when advanceX==0
        pen += adv * scale + spacing;
    }
}

// MeasureText -> MeasureTextEx: sum advances (recs.width + offsetX when
// advanceX==0), scaled, plus inter-glyph spacing.
int gfx_measure_text(const char* text, int font_size) {
    int fs = font_size < OB_FONT_BASE_SIZE ? OB_FONT_BASE_SIZE : font_size;
    int spacing = fs / OB_FONT_BASE_SIZE;
    float scale = (float)fs / OB_FONT_BASE_SIZE;
    float tw = 0.0f;
    int count = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        OBGlyph g = ob_font_glyphs[glyph_of(*p)];
        tw += (g.adv != 0) ? (float)g.adv : (g.rw + g.ox);
        count++;
    }
    return (int)(tw * scale + (count > 0 ? (count - 1) : 0) * spacing);
}
