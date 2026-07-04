#ifndef OPENBLOCKS_GFX_H
#define OPENBLOCKS_GFX_H

#include "ob_types.h"

// Immediate-mode 2D drawing primitives — the entire drawing surface the shared
// game renderer needs. Two backends implement this identically-behaving API:
//   gfx_raylib.c — wraps raylib (desktop / web / android). Behaviour-identical
//                  to the direct raylib calls it replaces.
//   gfx_metal.mm — native Metal (iOS), no raylib.
// The portrait/shared code in render.c calls these instead of raylib directly,
// so the layout logic is shared and only the primitives are swapped per platform.
// (No gfx_circle: the only circle/texture draws are in the landscape-only
// present() path, which never compiles on the touch platforms.)

#ifdef __cplusplus
extern "C" {
#endif

void gfx_begin_frame(void);
void gfx_end_frame(void);
void gfx_clear(Color color);

void gfx_rect(int x, int y, int w, int h, Color color);
void gfx_rect_lines(int x, int y, int w, int h, Color color);
void gfx_line(int x1, int y1, int x2, int y2, Color color);

void gfx_rounded_rect(Rectangle rec, float roundness, int segments, Color color);
void gfx_rounded_rect_lines(Rectangle rec, float roundness, int segments,
                            float line_thick, Color color);

void gfx_poly(Vector2 center, int sides, float radius, float rotation, Color color);
void gfx_ring(Vector2 center, float inner_radius, float outer_radius,
              float start_angle, float end_angle, int segments, Color color);

void gfx_text(const char* text, int x, int y, int font_size, Color color);
int  gfx_measure_text(const char* text, int font_size);

#ifdef __cplusplus
}
#endif

#endif // OPENBLOCKS_GFX_H
