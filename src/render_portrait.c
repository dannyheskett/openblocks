// Portrait (touch) renderer: adaptive layout drawn straight to the screen at the
// device's real resolution — title bar, HUD band, a square-cell playfield, and a
// bottom row of on-screen buttons. Compiles to an empty object off OB_PORTRAIT.
#include "render_internal.h"
#include <stddef.h> // NULL

#ifdef OB_PORTRAIT

// Height of the bottom on-screen control bar. Slimmed from h/7 so the playfield
// reclaims the height; buttons stay ~0.72*bar (comfortably above the 44pt min).
// Zero when the button row is hidden (gesture controls, the default) — the
// playfield takes the height instead.
static int control_bar_h(int h) {
    return render_touch_buttons_shown() ? h / 8 : 0;
}

// --- Shared portrait layout -------------------------------------------------
// One HUD band above the field: SCORE / LINES / LEVEL columns (uniform font,
// labels over values) with the NEXT preview box right-aligned. In gesture mode
// a pause key sits in the screen's top-right corner above the band (the spot
// mobile players expect); in button mode the pause key lives in the bottom row.
typedef struct {
    int fs, hud_h, band_y;       // uniform font size; band height and top y
    int cell, px, py, field_w;   // playfield geometry
    int nbox;                    // NEXT box side (== hud_h, snapped to 4 cells)
    Rectangle pause;             // pause key (gesture mode; unused with buttons)
} BandLayout;

static void band_layout(BandLayout* L) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int outer    = w / 30;
    int top_pad  = h / 40;
    int bar_h    = control_bar_h(h);
    int bottom_m = h / 45;
    int avail_h  = h - top_pad - bar_h - bottom_m;
    int hud_gap  = h / 70;
    bool gesture = !render_touch_buttons_shown();

    // Pause key row (gesture mode only): a comfortable tap target even on small
    // screens, tucked above the band's right edge.
    int pause_side = h / 16;
    int pause_gap  = h / 100;
    int pause_blk  = gesture ? pause_side + pause_gap : 0;

    // Uniform font: start at the approved size, shrink only if the three stat
    // columns plus the NEXT box can't fit across the field width.
    int fs = h / 38;
    int cell, field_w, nbox;
    for (;; fs--) {
        int hud_h = 2 * fs + fs / 3;
        int cw = (w - 2 * outer) / PLAYFIELD_WIDTH;
        int ch = (avail_h - hud_h - hud_gap - pause_blk) / PLAYFIELD_HEIGHT;
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
    int block = pause_blk + L->hud_h + hud_gap + cell * PLAYFIELD_HEIGHT;
    int top   = top_pad + (avail_h - block) / 2;
    L->band_y = top + pause_blk;
    L->py     = L->band_y + L->hud_h + hud_gap;
    L->pause  = (Rectangle){ (float)(L->px + field_w - pause_side), (float)top,
                             (float)pause_side, (float)pause_side };
}

int render_portrait_cell(void) {
    BandLayout L;
    band_layout(&L);
    return L.cell;
}

// Shared bottom-row geometry: the four game keys plus the menu/pause button,
// computed together so they never overlap. There is no title bar in portrait, so
// the menu button lives at the right end of the control row; the four game keys
// are centred in the width that remains to its left. Both render_touch_button_rects
// (game keys) and render_menu_button_rect (menu key) read from here, keeping the
// draw code and input hit-testing in lockstep. Pass NULL for either output.
static void bottom_row_layout(Rectangle game[BTN_COUNT], Rectangle* menu) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int bar_h = control_bar_h(h);
    int bar_y = h - bar_h;

    // Reserve a right-hand slot for the menu key; centre the game keys in the rest.
    int msize    = (int)(bar_h * 0.56f);   // a touch smaller than the game keys
    int medge    = w / 30;                 // menu key's right margin
    int reserved = msize + 2 * medge;
    int usable   = w - reserved;

    int bsize = (int)(bar_h * 0.72f);
    int gap   = w / 26;
    int side  = w / 20;                    // keep the row off the screen edges
    int max_total = usable - 2 * side;
    int total = BTN_COUNT * bsize + (BTN_COUNT - 1) * gap;
    if (total > max_total) {
        bsize = (max_total - (BTN_COUNT - 1) * gap) / BTN_COUNT;
        total = BTN_COUNT * bsize + (BTN_COUNT - 1) * gap;
    }
    int startx = (usable - total) / 2;
    int by = bar_y + (bar_h - bsize) / 2;
    if (game) {
        for (int i = 0; i < BTN_COUNT; i++) {
            game[i] = (Rectangle){ (float)(startx + i * (bsize + gap)),
                                   (float)by, (float)bsize, (float)bsize };
        }
    }
    if (menu) {
        int my = bar_y + (bar_h - msize) / 2;
        *menu = (Rectangle){ (float)(w - msize - medge), (float)my,
                             (float)msize, (float)msize };
    }
}

void render_touch_button_rects(Rectangle rects[BTN_COUNT]) {
    bottom_row_layout(rects, NULL);
}

// The menu/pause button: right end of the bottom control row when the button
// row is shown, else the top-right corner key above the HUD band.
void render_menu_button_rect(Rectangle* out) {
    if (render_touch_buttons_shown()) {
        bottom_row_layout(NULL, out);
    } else {
        BandLayout L;
        band_layout(&L);
        *out = L.pause;
    }
}

// Draw the menu/pause button as a rounded key matching the game keys' styling,
// with a 3-bar "menu" glyph. Brightens while a finger rests on it.
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

// Draw the four control buttons with subtle, low-contrast styling and clean
// vector icons. Buttons brighten slightly while a finger rests on them.
static void draw_touch_buttons(void) {
    Rectangle r[BTN_COUNT];
    render_touch_button_rects(r);
    int touches = GetTouchPointCount();

    for (int i = 0; i < BTN_COUNT; i++) {
        bool pressed = false;
        for (int t = 0; t < touches; t++) {
            if (CheckCollisionPointRec(GetTouchPosition(t), r[i])) { pressed = true; break; }
        }
        Color fill = pressed ? (Color){44, 47, 58, 255} : (Color){24, 26, 32, 255};
        Color edge = pressed ? (Color){90, 96, 112, 255} : (Color){48, 52, 62, 255};
        Color icon = pressed ? (Color){215, 219, 228, 255} : (Color){150, 156, 170, 255};
        gfx_rounded_rect(r[i], 0.30f, 10, fill);
        gfx_rounded_rect_lines(r[i], 0.30f, 10, 1.5f, edge);

        float cx = r[i].x + r[i].width / 2.0f;
        float cy = r[i].y + r[i].height / 2.0f;
        float s = r[i].height * 0.26f;
        Vector2 c = {cx, cy};
        switch (i) {
        case BTN_LEFT:  gfx_poly(c, 3, s, 180, icon); break; // points left
        case BTN_RIGHT: gfx_poly(c, 3, s, 0,   icon); break; // points right
        case BTN_ROTATE:
            gfx_ring(c, s * 0.52f, s * 0.82f, 40, 320, 32, icon);           // C-shaped arrow
            gfx_poly((Vector2){cx, cy - s * 0.67f}, 3, s * 0.32f, 0, icon); // arrowhead
            break;
        case BTN_DROP:
            gfx_poly((Vector2){cx, cy - s * 0.25f}, 3, s, 90, icon);        // points down
            gfx_rect((int)(cx - s * 0.9f), (int)(cy + s * 0.85f),
                          (int)(s * 1.8f), 3, icon);                        // floor line
            break;
        }
    }
}

// A small labelled stat drawn in a side gutter rail: dim label over a bright
// value, both left-aligned at x. Returns the y just below the value.
static int draw_gutter_stat(const char* label, const char* value, int x,
                            int y, int label_fs, int value_fs) {
    gfx_text(label, x, y, label_fs, (Color){150, 156, 170, 255});
    gfx_text(value, x, y + label_fs + label_fs / 3, value_fs, YELLOW);
    return y + label_fs + label_fs / 3 + value_fs;
}

static void draw_game_portrait(const Game* game) {
    gfx_clear(BLACK);
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

    if (render_touch_buttons_shown()) draw_touch_buttons();
    draw_menu_button();  // bottom row (buttons) or top-right pause key (gestures)
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
