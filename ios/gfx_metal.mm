// Native Metal backend for the gfx primitive layer (iOS) — no raylib.
//
// Immediate-mode design: each frame the gfx_* calls append triangles (colored,
// or textured from a font atlas) to a CPU vertex list in pixel coordinates;
// gfx_end_frame uploads them and issues one draw. A single pipeline handles both
// solid fills (sampling a white texel in the atlas) and text (sampling glyph
// coverage), so there is one shader and one draw call per frame.
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <vector>
#include <string.h>
#include <math.h>

#import "gfx.h"
#import "gfx_metal.h"

// --- Vertex + atlas layout --------------------------------------------------
struct GVert { float x, y, u, v, r, g, b, a; }; // 32 bytes; matches packed MSL

#define ATLAS_COLS 16
#define ATLAS_ROWS 6
#define ATLAS_CW   29          // glyph cell width  (px)
#define ATLAS_CH   64          // glyph cell height (px) — the text "em"
#define ATLAS_W    (ATLAS_COLS * ATLAS_CW)
#define ATLAS_H    (ATLAS_ROWS * ATLAS_CH)
#define GLYPH_FIRST 0x20
#define GLYPH_LAST  0x7E
#define WHITE_CELL  95         // last cell (col15,row5): a solid white texel

static id<MTLDevice>          s_device;
static id<MTLCommandQueue>    s_queue;
static CAMetalLayer*          s_layer;
static id<MTLRenderPipelineState> s_pipeline;
static id<MTLSamplerState>    s_sampler;
static id<MTLTexture>         s_atlas;

static std::vector<GVert> s_verts;
static float s_cr = 0, s_cg = 0, s_cb = 0, s_ca = 1; // clear colour
static int   s_fw = 1, s_fh = 1;                      // full drawable (px)
static int   s_ox = 0, s_oy = 0;                      // safe-area origin (px)

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
    float a = atlas.sample(samp, in.uv).a;
    return float4(in.color.rgb, in.color.a * a);
}
)";

// --- Setup ------------------------------------------------------------------
static void build_font_atlas(void) {
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(ATLAS_W, ATLAS_H), NO, 1.0);
    UIFont* font = [UIFont monospacedSystemFontOfSize:46 weight:UIFontWeightBold];
    NSDictionary* attrs = @{ NSFontAttributeName: font,
                             NSForegroundColorAttributeName: [UIColor whiteColor] };
    for (int i = 0; i <= (GLYPH_LAST - GLYPH_FIRST); i++) {
        unichar ch = (unichar)(GLYPH_FIRST + i);
        NSString* s = [NSString stringWithCharacters:&ch length:1];
        CGSize sz = [s sizeWithAttributes:attrs];
        int col = i % ATLAS_COLS, row = i / ATLAS_COLS;
        CGFloat x = col * ATLAS_CW + (ATLAS_CW - sz.width) / 2.0;
        CGFloat y = row * ATLAS_CH + (ATLAS_CH - sz.height) / 2.0;
        [s drawAtPoint:CGPointMake(x, y) withAttributes:attrs];
    }
    // Solid white texel cell for filled primitives.
    CGContextRef cg = UIGraphicsGetCurrentContext();
    CGContextSetRGBFillColor(cg, 1, 1, 1, 1);
    int wc = WHITE_CELL % ATLAS_COLS, wr = WHITE_CELL / ATLAS_COLS;
    CGContextFillRect(cg, CGRectMake(wc * ATLAS_CW, wr * ATLAS_CH, ATLAS_CW, ATLAS_CH));
    UIImage* img = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    // Rasterize the UIImage to RGBA bytes and upload.
    CGImageRef cgi = img.CGImage;
    int W = (int)CGImageGetWidth(cgi), H = (int)CGImageGetHeight(cgi);
    uint8_t* data = (uint8_t*)calloc((size_t)W * H * 4, 1);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bctx = CGBitmapContextCreate(data, W, H, 8, W * 4, cs,
                                              kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(bctx, CGRectMake(0, 0, W, H), cgi);
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:W height:H mipmapped:NO];
    s_atlas = [s_device newTextureWithDescriptor:td];
    [s_atlas replaceRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0
                 withBytes:data bytesPerRow:W * 4];
    CGContextRelease(bctx);
    CGColorSpaceRelease(cs);
    free(data);
}

void gfx_metal_attach(CAMetalLayer* layer) {
    s_device = MTLCreateSystemDefaultDevice();
    s_queue  = [s_device newCommandQueue];
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
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
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
    int c = WHITE_CELL % ATLAS_COLS, r = WHITE_CELL / ATLAS_COLS;
    *u = (c + 0.5f) * ATLAS_CW / (float)ATLAS_W;
    *v = (r + 0.5f) * ATLAS_CH / (float)ATLAS_H;
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
    id<MTLCommandBuffer> cmd = [s_queue commandBuffer];

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = drawable.texture;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor  = MTLClearColorMake(s_cr, s_cg, s_cb, s_ca);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];

    if (!s_verts.empty() && s_pipeline) {
        id<MTLBuffer> vb = [s_device newBufferWithBytes:s_verts.data()
                                                 length:s_verts.size() * sizeof(GVert)
                                                options:MTLResourceStorageModeShared];
        float uni[4] = { (float)s_fw, (float)s_fh, (float)s_ox, (float)s_oy };
        [enc setRenderPipelineState:s_pipeline];
        [enc setVertexBuffer:vb offset:0 atIndex:0];
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

static float roundness_radius(Rectangle r, float roundness) {
    float m = (r.width < r.height) ? r.width : r.height;
    float rad = roundness * m / 2.0f;
    if (rad < 0) rad = 0;
    return rad;
}

// Filled rounded rectangle: center + edge rects + 4 corner triangle-fans.
void gfx_rounded_rect(Rectangle r, float roundness, int segments, Color c) {
    float rad = roundness_radius(r, roundness);
    if (rad <= 0.5f) { gfx_rect((int)r.x, (int)r.y, (int)r.width, (int)r.height, c); return; }
    if (segments < 2) segments = 2;
    float x = r.x, y = r.y, w = r.width, h = r.height;
    quad_solid(x + rad, y, w - 2 * rad, h, c);       // center column
    quad_solid(x, y + rad, rad, h - 2 * rad, c);     // left strip
    quad_solid(x + w - rad, y + rad, rad, h - 2 * rad, c); // right strip
    // corners: centers and starting angles (radians)
    float cx[4] = { x + rad, x + w - rad, x + w - rad, x + rad };
    float cy[4] = { y + rad, y + rad, y + h - rad, y + h - rad };
    float a0[4] = { (float)M_PI, (float)(1.5 * M_PI), 0.0f, (float)(0.5 * M_PI) };
    for (int k = 0; k < 4; k++) {
        float step = (float)(0.5 * M_PI) / segments;
        for (int s = 0; s < segments; s++) {
            float a = a0[k] + s * step, b = a + step;
            tri_solid(cx[k], cy[k],
                      cx[k] + cosf(a) * rad, cy[k] + sinf(a) * rad,
                      cx[k] + cosf(b) * rad, cy[k] + sinf(b) * rad, c);
        }
    }
}

// Rounded outline: straight edge quads inset by the corner radius (corners left
// as small gaps — a subtle border, adequate for the control buttons).
void gfx_rounded_rect_lines(Rectangle r, float roundness, int segments, float t, Color c) {
    (void)segments;
    float rad = roundness_radius(r, roundness);
    if (t < 1) t = 1;
    int ti = (int)(t + 0.5f);
    gfx_rect((int)(r.x + rad), (int)r.y, (int)(r.width - 2 * rad), ti, c);
    gfx_rect((int)(r.x + rad), (int)(r.y + r.height - ti), (int)(r.width - 2 * rad), ti, c);
    gfx_rect((int)r.x, (int)(r.y + rad), ti, (int)(r.height - 2 * rad), c);
    gfx_rect((int)(r.x + r.width - ti), (int)(r.y + rad), ti, (int)(r.height - 2 * rad), c);
}

// Regular polygon (triangle fan), matching raylib DrawPoly (rotation in degrees).
void gfx_poly(Vector2 center, int sides, float radius, float rotation, Color c) {
    if (sides < 3) sides = 3;
    float step = (float)(2.0 * M_PI) / sides;
    float rot = rotation * (float)M_PI / 180.0f;
    for (int i = 0; i < sides; i++) {
        float a = rot + i * step, b = rot + (i + 1) * step;
        tri_solid(center.x, center.y,
                  center.x + cosf(a) * radius, center.y + sinf(a) * radius,
                  center.x + cosf(b) * radius, center.y + sinf(b) * radius, c);
    }
}

// Ring sector between inner/outer radius over [startAngle,endAngle] (degrees).
void gfx_ring(Vector2 center, float inner, float outer,
              float startAngle, float endAngle, int segments, Color c) {
    if (segments < 1) segments = 1;
    float a0 = startAngle * (float)M_PI / 180.0f;
    float a1 = endAngle   * (float)M_PI / 180.0f;
    float step = (a1 - a0) / segments;
    for (int i = 0; i < segments; i++) {
        float a = a0 + i * step, b = a0 + (i + 1) * step;
        float cia = cosf(a), sia = sinf(a), cib = cosf(b), sib = sinf(b);
        float ix0 = center.x + cia * inner, iy0 = center.y + sia * inner;
        float ox0 = center.x + cia * outer, oy0 = center.y + sia * outer;
        float ix1 = center.x + cib * inner, iy1 = center.y + sib * inner;
        float ox1 = center.x + cib * outer, oy1 = center.y + sib * outer;
        tri_solid(ix0, iy0, ox0, oy0, ox1, oy1, c);
        tri_solid(ix0, iy0, ox1, oy1, ix1, iy1, c);
    }
}

static inline float glyph_advance(int font_size) {
    return font_size * (float)ATLAS_CW / (float)ATLAS_CH;
}

void gfx_text(const char* text, int x, int y, int font_size, Color c) {
    float adv = glyph_advance(font_size);
    float gw = adv, gh = (float)font_size;
    float pen = (float)x;
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        unsigned char ch = *p;
        if (ch >= GLYPH_FIRST && ch <= GLYPH_LAST && ch != ' ') {
            int gi = ch - GLYPH_FIRST;
            int col = gi % ATLAS_COLS, row = gi / ATLAS_COLS;
            float u0 = col * ATLAS_CW / (float)ATLAS_W;
            float v0 = row * ATLAS_CH / (float)ATLAS_H;
            float u1 = (col + 1) * ATLAS_CW / (float)ATLAS_W;
            float v1 = (row + 1) * ATLAS_CH / (float)ATLAS_H;
            // two textured triangles
            push(pen,      y,      u0, v0, c); push(pen + gw, y,      u1, v0, c); push(pen + gw, y + gh, u1, v1, c);
            push(pen,      y,      u0, v0, c); push(pen + gw, y + gh, u1, v1, c); push(pen,      y + gh, u0, v1, c);
        }
        pen += adv;
    }
}

int gfx_measure_text(const char* text, int font_size) {
    return (int)(strlen(text) * glyph_advance(font_size) + 0.5f);
}
