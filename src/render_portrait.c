// Portrait (touch) renderer: adaptive layout drawn straight to the screen at the
// device's real resolution — a thin OPENBLOCKS title bar, HUD band, and a
// square-cell playfield, all controlled by gestures (no on-screen buttons).
// Compiles to an empty object off OB_PORTRAIT.
#include "render_internal.h"
#include <stddef.h> // NULL

#ifdef OB_PORTRAIT

// Thin full-width title bar across the very top, matching the landscape
// renderer's (24px tall with 16px text on the 480px canvas — keep that ratio).
static int title_fs(int h)    { int fs = h / 45; return (fs < 10) ? 10 : fs; }
static int title_bar_h(int h) { int fs = title_fs(h); return fs + fs / 2; }

// Global margin: the breathing room applied on every edge (playfield left /
// right / bottom, below the title bar) and as the gap between the bottom of
// the HUD band and the top of the playfield. Scales with the short screen
// dimension so it stays proportional across resolutions and aspect ratios.
static int outer_margin(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int m = ((w < h) ? w : h) / 28;
    return (m < 6) ? 6 : m;
}

// --- Shared portrait layout -------------------------------------------------
// Title bar, then one HUD band above the field: SCORE / LINES / LEVEL columns
// (uniform font, labels over values) with the NEXT preview box right-aligned.
// A pause key sits above the band's right edge, in the top-right corner spot
// mobile players expect.
typedef struct {
    int fs, hud_h, band_y;       // uniform font size; band height and top y
    int cell, px, py, field_w;   // playfield geometry
    int nbox;                    // NEXT box side (== hud_h, snapped to 4 cells)
    Rectangle pause;             // pause key
} BandLayout;

static void band_layout(BandLayout* L) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int m       = outer_margin();
    int tb_h    = title_bar_h(h);
    int avail_h = h - tb_h - 2 * m;   // margin below the bar and at the bottom

    // Pause key: a comfortable tap target even on small screens, tucked above
    // the band's right edge.
    int pause_side = h / 16;
    int pause_gap  = h / 100;
    int pause_blk  = pause_side + pause_gap;

    // Uniform font: start at the approved size, shrink only if the three stat
    // columns plus the NEXT box can't fit across the field width.
    int fs = h / 38;
    int cell, field_w, nbox;
    for (;; fs--) {
        int hud_h = 2 * fs + fs / 3;
        int cw = (w - 2 * m) / PLAYFIELD_WIDTH;
        int ch = (avail_h - hud_h - m - pause_blk) / PLAYFIELD_HEIGHT;
        cell = (cw < ch) ? cw : ch;
        if (cell < 1) cell = 1;
        field_w = cell * PLAYFIELD_WIDTH;
        nbox = hud_h / 4 * 4;
        int cols = gfx_measure_text("SCORE", fs) + gfx_measure_text("LINES", fs) +
                   gfx_measure_text("LEVEL", fs);
        if (fs <= 8 || cols + nbox + 3 * (fs / 2) <= field_w) { L->fs = fs; break; }
    }
    L->hud_h  = 2 * L->fs + L->fs / 3;
    L->cell   = cell;
    L->field_w = field_w;
    L->nbox   = nbox;
    L->px     = (w - field_w) / 2;
    // The HUD -> playfield gap is the global margin; leftover height centers
    // the whole block between the title bar and the bottom margin.
    int block = pause_blk + L->hud_h + m + cell * PLAYFIELD_HEIGHT;
    int top   = tb_h + m + (avail_h - block) / 2;
    L->band_y = top + pause_blk;
    L->py     = L->band_y + L->hud_h + m;
    L->pause  = (Rectangle){ (float)(L->px + field_w - pause_side), (float)top,
                             (float)pause_side, (float)pause_side };
}

int render_portrait_cell(void) {
    BandLayout L;
    band_layout(&L);
    return L.cell;
}

// The menu/pause button: the top-right corner key above the HUD band.
void render_menu_button_rect(Rectangle* out) {
    BandLayout L;
    band_layout(&L);
    *out = L.pause;
}

// Draw the menu/pause button as a rounded key with a 3-bar "menu" glyph.
// Brightens while a finger rests on it.
static void draw_menu_button(void) {
    Rectangle r;
    render_menu_button_rect(&r);
    int touches = GetTouchPointCount();
    bool pressed = false;
    for (int t = 0; t < touches; t++) {
        if (CheckCollisionPointRec(GetTouchPosition(t), r)) { pressed = true; break; }
    }
    Color fill = pressed ? (Color){44, 47, 58, 255} : (Color){24, 26, 32, 255};
    Color edge = pressed ? (Color){90, 96, 112, 255} : (Color){48, 52, 62, 255};
    Color ic   = pressed ? (Color){215, 219, 228, 255} : (Color){150, 156, 170, 255};
    gfx_rounded_rect(r, 0.30f, 10, fill);
    gfx_rounded_rect_lines(r, 0.30f, 10, 1.5f, edge);
    int bx = (int)(r.x + r.width * 0.28f), bw = (int)(r.width * 0.44f);
    int bh = (int)(r.height * 0.09f) + 1;
    for (int i = 0; i < 3; i++) {
        gfx_rect(bx, (int)(r.y + r.height * (0.32f + 0.18f * i)), bw, bh, ic);
    }
}

// Thin full-width title bar at the very top, matching the landscape renderer.
static void draw_title_bar(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int fs = title_fs(h), tb_h = title_bar_h(h);
    gfx_rect(0, 0, w, tb_h, DARKGRAY);
    gfx_text("OPENBLOCKS", (w - gfx_measure_text("OPENBLOCKS", fs)) / 2,
             (tb_h - fs) / 2, fs, WHITE);
}

static void draw_game_portrait(const Game* game) {
    gfx_clear(BLACK);
    draw_title_bar();
    BandLayout L;
    band_layout(&L);
    int fs = L.fs, line_gap = L.fs / 3;
    int val_y = L.band_y + fs + line_gap;

    // NEXT preview box, right-aligned to the field edge; the piece is centered
    // in the box by its true bounds (draw_piece_centered).
    int nx = L.px + L.field_w - L.nbox;
    gfx_rect_lines(nx, L.band_y, L.nbox, L.nbox, LIGHTGRAY);
    gfx_rect(nx + 1, L.band_y + 1, L.nbox - 2, L.nbox - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, nx, L.band_y, L.nbox, L.nbox,
                        L.nbox / 4, next_color);

    // Stat columns (uniform font): SCORE / LINES / LEVEL, labels over values,
    // spread evenly across the width left of the NEXT box.
    int score_w = gfx_measure_text("SCORE", fs);
    int lines_w = gfx_measure_text("LINES", fs);
    int level_w = gfx_measure_text("LEVEL", fs);
    int gap = (L.field_w - L.nbox - score_w - lines_w - level_w) / 3;
    int x = L.px;
    gfx_text("SCORE", x, L.band_y, fs, (Color){150, 156, 170, 255});
    gfx_text(TextFormat("%06u", (unsigned int)game->score), x, val_y, fs, YELLOW);
    x += score_w + gap;
    gfx_text("LINES", x, L.band_y, fs, (Color){150, 156, 170, 255});
    gfx_text(TextFormat("%d", game->lines_cleared), x, val_y, fs, YELLOW);
    x += lines_w + gap;
    gfx_text("LEVEL", x, L.band_y, fs, (Color){150, 156, 170, 255});
    gfx_text(TextFormat("%d", game->level), x, val_y, fs, YELLOW);

    draw_playfield(game, L.px, L.py, L.cell);

    draw_menu_button();  // top-right pause key
}

static void draw_center_panel_portrait(const char* title, const char* subtitle, Color tc) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int base = (w < h) ? w : h;   // keep the dialog compact even in a wide window
    int pw = base * 82 / 100;
    int ph = base * 26 / 100;
    draw_center_panel_at(w, h, pw, ph, ph * 28 / 100, ph * 13 / 100,
                         ph * 24 / 100, ph * 62 / 100, title, subtitle, tc);
}

// One gameplay scene with an optional centered overlay (pause / game over).
static void draw_scene_portrait(const Game* game, const char* overlay_title,
                                const char* overlay_sub, Color overlay_tc) {
    gfx_begin_frame();
    draw_game_portrait(game);
    if (overlay_title) draw_center_panel_portrait(overlay_title, overlay_sub, overlay_tc);
    gfx_end_frame();
}

void render_frame_portrait(const Game* g)     { draw_scene_portrait(g, NULL, NULL, WHITE); }
void render_pause_portrait(const Game* g)     { draw_scene_portrait(g, "GAME PAUSED", "Tap to resume", YELLOW); }
void render_game_over_portrait(const Game* g) { draw_scene_portrait(g, "GAME OVER", "Tap to return to menu", RED); }

void render_menu_portrait(const char* title, const char* const* items, int count,
                          int selected, int gap_before) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int line_h = h / 20, item_fs = h / 28;
    int extra = (gap_before >= 0) ? 1 : 0;
    int base = (w < h) ? w : h;              // keep the panel compact in a wide window
    int panel_w = base * 82 / 100;

    // Shrink the title if it would overrun the panel (wide tablets).
    int title_size = h / 16;
    while (title_size > 12 && gfx_measure_text(title, title_size) > panel_w - line_h) title_size -= 2;

    int panel_h = title_size + line_h + (count + extra) * line_h + line_h * 2;
    int px = w / 2 - panel_w / 2, py = (h - panel_h) / 2;
    MenuLayout m = { .cx = w / 2, .px = px, .py = py, .panel_w = panel_w, .panel_h = panel_h,
                     .title_size = title_size, .title_y = py + line_h,
                     .items_y = py + line_h + title_size + line_h,
                     .line_h = line_h, .item_fs = item_fs };

    gfx_begin_frame();
    gfx_clear(BLACK);
    draw_menu_panel(m, title, items, count, selected, gap_before, true);
    gfx_end_frame();
}

#endif // OB_PORTRAIT
