// Portrait (touch) renderer: adaptive layout drawn straight to the screen at the
// device's real resolution — title bar, HUD band, a square-cell playfield, and a
// bottom row of on-screen buttons. Compiles to an empty object off OB_PORTRAIT.
#include "render_internal.h"
#include <stddef.h> // NULL

#ifdef OB_PORTRAIT

// Height of the bottom on-screen control bar. Slimmed from h/7 so the playfield
// reclaims the height; buttons stay ~0.72*bar (comfortably above the 44pt min).
static int control_bar_h(int h) { return h / 8; }

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

// The menu/pause button sits at the right end of the bottom control row.
void render_menu_button_rect(Rectangle* out) {
    bottom_row_layout(NULL, out);
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

// Side-rail gameplay layout: HUD in the side gutters. Right for screens where
// the field is HEIGHT-limited (squarer phones like an SE, tablets): the gutters
// are naturally wide there, so the rails get room without costing field size.
static void draw_game_rails(const Game* game, int cell, int top_pad, int avail_h) {
    int w = GetScreenWidth();
    int field_w = cell * PLAYFIELD_WIDTH;
    int field_h = cell * PLAYFIELD_HEIGHT;
    int play_x  = (w - field_w) / 2;
    int play_y  = top_pad + (avail_h - field_h) / 2;
    int gutter  = play_x;                // symmetric side gutter width (== play_x)

    // Side-gutter HUD rails, top-aligned with and hugging the playfield. SCORE /
    // LINES / LEVEL stack in the left rail; the NEXT preview anchors the right.
    // The rail *content* is capped at cell*4 so it never balloons when the window
    // is wide/landscape (portrait renderer on a sideways tablet or Controls: On):
    // there the field stays height-limited and the raw gutter grows without bound.
    int railw = gutter;
    if (railw > cell * 4) railw = cell * 4;
    int rail_pad = railw / 8 + 2;
    int budget   = railw - 2 * rail_pad;             // usable content width per rail
    int label_fs = railw / 6;
    int value_fs = railw * 5 / 16;
    int lx       = play_x - railw + rail_pad;        // left rail hugs field's left

    // SCORE first: six digits, so shrink its font to fit the (narrow) rail.
    const char* score = TextFormat("%06u", (unsigned int)game->score);
    int score_fs = value_fs;
    while (score_fs > 8 && gfx_measure_text(score, score_fs) > budget) score_fs--;
    gfx_text("SCORE", lx, play_y, label_fs, (Color){150, 156, 170, 255});
    gfx_text(score, lx, play_y + label_fs + label_fs / 3, score_fs, YELLOW);
    int y = play_y + label_fs + label_fs / 3 + score_fs + value_fs / 2;
    y = draw_gutter_stat("LINES", TextFormat("%d", game->lines_cleared),
                         lx, y, label_fs, value_fs);
    draw_gutter_stat("LEVEL", TextFormat("%d", game->level),
                     lx, y + value_fs / 2, label_fs, value_fs);

    // NEXT preview (square mini-cells) in the right rail, hugging the field. With
    // the menu button now on the bottom row, NEXT can sit at the field top.
    int ncell  = budget / 4;
    int nbox_w = ncell * 4, nbox_h = ncell * 4;
    int rx     = play_x + field_w + rail_pad;        // right rail hugs field's right
    int nbox_y = play_y + label_fs + label_fs / 3;
    gfx_text("NEXT", rx, play_y, label_fs, (Color){150, 156, 170, 255});
    gfx_rect_lines(rx, nbox_y, nbox_w, nbox_h, LIGHTGRAY);
    gfx_rect(rx + 1, nbox_y + 1, nbox_w - 2, nbox_h - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, rx, nbox_y, nbox_w, nbox_h, ncell, next_color);

    draw_playfield(game, play_x, play_y, cell);
}

// Top-band gameplay layout: one HUD row (SCORE / LINES / LEVEL columns + the
// NEXT preview) sitting on the field's top edge. Right for tall 20:9 phones,
// where the field is WIDTH-limited and the spare height makes the band nearly
// free — the field gets full width and the HUD is sized off screen height, so
// it stays readable no matter how skinny the side margins are.
static void draw_game_band(const Game* game, int cell, int top_pad, int avail_h,
                           int hud_h, int hud_gap) {
    int w = GetScreenWidth();
    int field_w = cell * PLAYFIELD_WIDTH;
    int field_h = cell * PLAYFIELD_HEIGHT;
    int play_x  = (w - field_w) / 2;

    // Centre the band+field block in the available height; band hugs the field.
    int block_h = hud_h + hud_gap + field_h;
    int band_y  = top_pad + (avail_h - block_h) / 2;
    int play_y  = band_y + hud_h + hud_gap;

    int label_fs = hud_h * 21 / 100;
    int value_fs = hud_h * 38 / 100;

    // NEXT column right-aligned to the field edge: label over a square preview
    // box that fills the band height remaining under the label.
    int nbox   = hud_h - (label_fs + label_fs / 3);
    int ncell  = nbox / 4;
    nbox = ncell * 4;
    int nx     = play_x + field_w - nbox;
    int nbox_y = band_y + label_fs + label_fs / 3;
    gfx_text("NEXT", nx, band_y, label_fs, (Color){150, 156, 170, 255});
    gfx_rect_lines(nx, nbox_y, nbox, nbox, LIGHTGRAY);
    gfx_rect(nx + 1, nbox_y + 1, nbox - 2, nbox - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, nx, nbox_y, nbox, nbox, ncell, next_color);

    // Stat columns left-aligned to the field edge; SCORE gets the wide first
    // column (six digits), LINES / LEVEL follow at fixed fractions of the field.
    int sx = play_x;
    int lines_x = play_x + field_w * 42 / 100;
    int level_x = play_x + field_w * 65 / 100;
    const char* score = TextFormat("%06u", (unsigned int)game->score);
    int score_fs = value_fs;
    while (score_fs > 8 &&
           gfx_measure_text(score, score_fs) > lines_x - sx - value_fs / 2) score_fs--;
    gfx_text("SCORE", sx, band_y, label_fs, (Color){150, 156, 170, 255});
    gfx_text(score, sx, band_y + label_fs + label_fs / 3, score_fs, YELLOW);
    draw_gutter_stat("LINES", TextFormat("%d", game->lines_cleared),
                     lines_x, band_y, label_fs, value_fs);
    draw_gutter_stat("LEVEL", TextFormat("%d", game->level),
                     level_x, band_y, label_fs, value_fs);

    draw_playfield(game, play_x, play_y, cell);
}

static void draw_game_portrait(const Game* game) {
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    gfx_clear(BLACK);

    int outer    = w / 30;               // minimal screen-edge margin
    int top_pad  = h / 40;               // small top margin (no title bar)
    int bar_h    = control_bar_h(h);     // bottom control bar (always in portrait)
    int bottom_m = h / 45;
    int avail_h  = h - top_pad - bar_h - bottom_m;
    int cell_h   = avail_h / PLAYFIELD_HEIGHT;

    // Two layouts, picked per screen shape by which yields the bigger field:
    //  - side rails need real gutter width to be readable, so their candidate
    //    cell reserves w/8 per side;
    //  - the top band needs hud_h of height, but only the w/30 edge margin.
    // On tall 20:9 phones the band wins (spare height, scarce width); on squarer
    // screens (SE-class phones, tablets) the rails win. The band gets a small
    // preference (7%) because readable HUD beats a sliver of extra cell size.
    int rail_min  = w / 8;
    int side_m    = (outer > rail_min) ? outer : rail_min;
    int cw_side   = (w - 2 * side_m) / PLAYFIELD_WIDTH;
    int cell_side = (cw_side < cell_h) ? cw_side : cell_h;

    int hud_h   = h / 11;
    int hud_gap = h / 70;
    int cw_band = (w - 2 * outer) / PLAYFIELD_WIDTH;
    int ch_band = (avail_h - hud_h - hud_gap) / PLAYFIELD_HEIGHT;
    int cell_band = (cw_band < ch_band) ? cw_band : ch_band;

    if (cell_band * 100 >= cell_side * 93) {
        if (cell_band < 1) cell_band = 1;
        draw_game_band(game, cell_band, top_pad, avail_h, hud_h, hud_gap);
    } else {
        if (cell_side < 1) cell_side = 1;
        draw_game_rails(game, cell_side, top_pad, avail_h);
    }

    draw_touch_buttons();
    draw_menu_button();
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
