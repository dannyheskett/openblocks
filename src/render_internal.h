#ifndef OPENBLOCKS_RENDER_INTERNAL_H
#define OPENBLOCKS_RENDER_INTERNAL_H

// Private interface shared between the renderer translation units:
//   render.c           — common state, shared draw helpers, lifecycle, dispatch
//   render_portrait.c  — the portrait (touch) renderer    (body under OB_PORTRAIT)
//   render_landscape.c — the landscape (desktop) renderer (body under OB_LANDSCAPE)
// render_portrait/landscape.c compile to empty objects on platforms that don't
// use them, so all three can sit in the build's source list unconditionally.

#include "render.h"
#include "gfx.h"

#define CELL_SIZE 20
#define BORDER_WIDTH 2

// Playfield colours (defined in render.c), used by both renderers' NEXT boxes.
extern const Color BOARD_BG;
extern const Color GRID_LINE;

// Shared draw helpers (defined in render.c).
Color color_from_piece(int piece_type, int level);
void draw_piece_ex(const Piece* piece, int offset_x, int offset_y, int cell_size, Color color);
void draw_piece_centered(int piece_type, int rotation, int box_x, int box_y,
                         int box_w, int box_h, int cell, Color color);
void draw_playfield(const Game* game, int play_x, int play_y, int cell);
void draw_center_panel_at(int w, int h, int panel_w, int panel_h, int ts, int ss,
                          int title_dy, int sub_dy, const char* title,
                          const char* subtitle, Color title_color);

// Computed menu geometry + the shared menu drawer (defined in render.c). Each
// renderer fills the layout from its own sizing.
typedef struct {
    int cx, px, py, panel_w, panel_h;
    int title_size, title_y, items_y, line_h, item_fs;
} MenuLayout;
void draw_menu_panel(MenuLayout m, const char* title, const char* const* items,
                     int count, int selected, int gap_before, bool capture);

// Per-renderer entry points (defined in render_portrait/landscape.c), called by
// the OB_DISPATCH macro in render.c.
#ifdef OB_PORTRAIT
void render_frame_portrait(const Game* game);
void render_pause_portrait(const Game* game);
void render_game_over_portrait(const Game* game);
void render_menu_portrait(const char* title, const char* const* items, int count,
                          int selected, int gap_before);
#endif
#ifdef OB_LANDSCAPE
extern RenderTexture2D canvas; // created in render_init, blitted by present()
void render_frame_landscape(const Game* game);
void render_pause_landscape(const Game* game);
void render_game_over_landscape(const Game* game);
void render_menu_landscape(const char* title, const char* const* items, int count,
                           int selected, int gap_before);
#endif

#endif // OPENBLOCKS_RENDER_INTERNAL_H
