// raylib backend for the gfx primitive layer: each entry point is a 1:1 wrapper
// over the raylib call it replaces, so desktop / web / android rendering is
// byte-for-byte identical to before the gfx layer was introduced. iOS uses
// gfx_metal.mm instead and never compiles this file.
#include "gfx.h"
#include <raylib.h>

void gfx_begin_frame(void) { BeginDrawing(); }
void gfx_end_frame(void)   { EndDrawing(); }
void gfx_clear(Color color) { ClearBackground(color); }

void gfx_rect(int x, int y, int w, int h, Color color) {
    DrawRectangle(x, y, w, h, color);
}
void gfx_rect_lines(int x, int y, int w, int h, Color color) {
    DrawRectangleLines(x, y, w, h, color);
}
void gfx_line(int x1, int y1, int x2, int y2, Color color) {
    DrawLine(x1, y1, x2, y2, color);
}

void gfx_text(const char* text, int x, int y, int font_size, Color color) {
    DrawText(text, x, y, font_size, color);
}
int gfx_measure_text(const char* text, int font_size) {
    return MeasureText(text, font_size);
}
