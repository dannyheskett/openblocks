// Portrait (touch) renderer: adaptive layout drawn straight to the screen at the
// device's real resolution — a thin OPENBLOCKS title bar, HUD band, and a
// square-cell playfield, all controlled by gestures (no on-screen buttons).
// Compiles to an empty object off OB_PORTRAIT.
#include "render_internal.h"
#include "safe_area.h"
#include <stddef.h> // NULL

#ifdef OB_PORTRAIT

// Thin full-width title bar across the very top, matching the landscape
// renderer's (24px tall with 16px text on the 480px canvas — keep that ratio).
static int title_fs(int h)    { int fs = h / 45; return (fs < 10) ? 10 : fs; }
static int title_bar_h(int h) { int fs = title_fs(h); return fs + fs / 2; }

// Effective top-bar height: the thin title bar, grown to clear the display
// cutout (front camera) when the surface draws under it, so neither the
// wordmark nor the playfield below ever sits beneath the camera.
static int top_bar_h(int h) {
    int top, cl, cr;
    safe_area_get(&top, &cl, &cr);
    int tb = title_bar_h(h);
    return (top > tb) ? top : tb;
}

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
// There is no on-screen pause key — a two-finger tap pauses (see input.c).
typedef struct {
    int fs, hud_h, band_y;       // uniform font size; band height and top y
    int cell, px, py, field_w;   // playfield geometry
    int nbox;                    // NEXT box side (== hud_h, snapped to 4 cells)
} BandLayout;

static void band_layout(BandLayout* L) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int m       = outer_margin();
    int tb_h    = top_bar_h(h);
    int avail_h = h - tb_h - 2 * m;   // margin below the bar and at the bottom

    // Uniform font: start at the approved size, shrink only if the three stat
    // columns plus the NEXT box can't fit across the field width.
    int fs = h / 38;
    int cell, field_w, nbox;
    for (;; fs--) {
        int hud_h = 2 * fs + fs / 3;
        int cw = (w - 2 * m) / PLAYFIELD_WIDTH;
        int ch = (avail_h - hud_h - m) / PLAYFIELD_HEIGHT;
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
    int block = L->hud_h + m + cell * PLAYFIELD_HEIGHT;
    L->band_y = tb_h + m + (avail_h - block) / 2;
    L->py     = L->band_y + L->hud_h + m;
}

int render_portrait_cell(void) {
    BandLayout L;
    band_layout(&L);
    return L.cell;
}

// Full-width title bar at the very top. When a display cutout (front camera)
// sits in the bar, the "OPENBLOCKS" wordmark is laid out around it:
//   - cutout absent            -> centered full word (the desktop/web look).
//   - room both sides          -> "OPEN" left of the camera, "BLOCKS" right.
//   - lopsided (corner camera) -> whole word on the roomier side, if it fits.
//   - wide notch, nothing fits -> bar only, no wordmark.
static void draw_title_bar(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int fs = title_fs(h), tb_h = top_bar_h(h);
    int ty = (tb_h - fs) / 2;   // wordmark vertically centered in the bar
    gfx_rect(0, 0, w, tb_h, DARKGRAY);

    int top, cl, cr;
    safe_area_get(&top, &cl, &cr);
    int full = gfx_measure_text("OPENBLOCKS", fs);

    // No horizontal extent reported. With no top inset either, there is no
    // cutout at all -> original centered wordmark. If there IS a top inset we
    // just couldn't localize, leave the bar bare rather than risk centering the
    // word under the camera.
    if (cr <= cl) {
        if (top <= 0)
            gfx_text("OPENBLOCKS", (w - full) / 2, ty, fs, WHITE);
        return;
    }

    int pad        = fs / 2;        // clearance kept between text and the camera
    int left_room  = cl;            // px available left of the cutout
    int right_room = w - cr;        // px available right of the cutout
    int open_w     = gfx_measure_text("OPEN", fs);
    int blocks_w   = gfx_measure_text("BLOCKS", fs);

    if (left_room >= open_w + pad && right_room >= blocks_w + pad) {
        // Split the word around the camera.
        gfx_text("OPEN",   cl - pad - open_w, ty, fs, WHITE);
        gfx_text("BLOCKS", cr + pad,          ty, fs, WHITE);
    } else if (right_room >= full + pad || left_room >= full + pad) {
        // Corner camera: keep the word whole on whichever side has more room.
        if (right_room >= left_room)
            gfx_text("OPENBLOCKS", cr + pad, ty, fs, WHITE);
        else
            gfx_text("OPENBLOCKS", cl - pad - full, ty, fs, WHITE);
    }
    // else: wide notch — leave the bar bare.
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
