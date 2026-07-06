// Portrait (touch) renderer: adaptive layout drawn straight to the screen at the
// device's real resolution — title bar, HUD band, a square-cell playfield, and a
// bottom row of on-screen buttons. Compiles to an empty object off OB_PORTRAIT.
#include "render_internal.h"
#include <stddef.h> // NULL

#ifdef OB_PORTRAIT

// Height of the bottom on-screen control bar. Slimmed from h/7 so the playfield
// reclaims the height; buttons stay ~0.72*bar (comfortably above the 44pt min).
static int control_bar_h(int h) { return h / 8; }

// Height of the single compact top bar (title + score + menu button). Replaces
// the old title bar + separate HUD band; the freed height grows the playfield,
// and the stats that used to sit in the HUD band move into the side gutters.
static int title_bar_h(int h) { return h / 13; }

void render_touch_button_rects(Rectangle rects[BTN_COUNT]) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int bar_h = control_bar_h(h);
    int bar_y = h - bar_h;

    // A centered row of square buttons, smaller than the bar so they read as
    // compact keys rather than filling the whole strip.
    int bsize = (int)(bar_h * 0.72f);
    int gap  = w / 26;
    int side = w / 20;                    // keep the row off the screen edges
    int max_total = w - 2 * side;
    int total = BTN_COUNT * bsize + (BTN_COUNT - 1) * gap;
    if (total > max_total) {
        bsize = (max_total - (BTN_COUNT - 1) * gap) / BTN_COUNT;
        total = BTN_COUNT * bsize + (BTN_COUNT - 1) * gap;
    }
    int startx = (w - total) / 2;
    int by = bar_y + (bar_h - bsize) / 2;  // also leaves a gap below for the nav pill
    for (int i = 0; i < BTN_COUNT; i++) {
        rects[i] = (Rectangle){ (float)(startx + i * (bsize + gap)),
                                (float)by, (float)bsize, (float)bsize };
    }
}

// The menu/pause button sits at the right end of the top title bar.
void render_menu_button_rect(Rectangle* out) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int th = title_bar_h(h);         // top bar height
    int size = th * 4 / 5;           // square key, vertically centered in the bar
    int pad  = (th - size) / 2;
    *out = (Rectangle){ (float)(w - size - w / 30), (float)pad, (float)size, (float)size };
}

// Draw the menu/pause button: a small rounded key with a 3-bar "menu" glyph.
static void draw_menu_button(void) {
    Rectangle r;
    render_menu_button_rect(&r);
    int touches = GetTouchPointCount();
    bool pressed = false;
    for (int t = 0; t < touches; t++) {
        if (CheckCollisionPointRec(GetTouchPosition(t), r)) { pressed = true; break; }
    }
    gfx_rounded_rect(r, 0.30f, 8, pressed ? (Color){70, 74, 90, 255} : (Color){40, 42, 52, 255});
    Color ic = pressed ? (Color){220, 224, 232, 255} : (Color){170, 176, 190, 255};
    int bx = (int)(r.x + r.width * 0.26f), bw = (int)(r.width * 0.48f);
    int bh = (int)(r.height * 0.09f) + 1;
    for (int i = 0; i < 3; i++) {
        gfx_rect(bx, (int)(r.y + r.height * (0.30f + 0.20f * i)), bw, bh, ic);
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
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    gfx_clear(BLACK);

    int outer    = w / 30;               // minimal screen-edge margin
    int title_h  = title_bar_h(h);       // single compact top bar
    int bar_h    = control_bar_h(h);     // bottom control bar (always in portrait)
    int bottom_m = h / 45;
    int avail_h  = h - title_h - bar_h - bottom_m;

    // Largest square cell that fits 10 wide x 20 tall in the reclaimed height.
    // The field is height-limited on phones, so it leaves side gutters we fill
    // with the HUD rather than stacking a HUD band that would steal that height.
    int cell_w = (w - 2 * outer) / PLAYFIELD_WIDTH;
    int cell_h = avail_h / PLAYFIELD_HEIGHT;
    int cell   = (cell_w < cell_h) ? cell_w : cell_h;
    if (cell < 1) cell = 1;
    int field_w = cell * PLAYFIELD_WIDTH;
    int field_h = cell * PLAYFIELD_HEIGHT;
    int play_x  = (w - field_w) / 2;
    int play_y  = title_h + (avail_h - field_h) / 2;
    int gutter  = play_x;                // symmetric side gutter width (== play_x)

    // Top bar: title at the left, SCORE right-aligned before the menu button.
    int title_fs = title_h * 2 / 5;
    int bar_lab  = title_h / 5;
    gfx_rect(0, 0, w, title_h, DARKGRAY);
    gfx_text("OPENBLOCKS", outer, (title_h - title_fs) / 2, title_fs, WHITE);
    Rectangle mb; render_menu_button_rect(&mb);
    const char* score = TextFormat("%06u", (unsigned int)game->score);
    int score_w = gfx_measure_text(score, title_fs);
    int score_x = (int)mb.x - w / 30 - score_w;
    gfx_text("SCORE", score_x, (title_h - title_fs) / 2 - bar_lab, bar_lab,
             (Color){150, 156, 170, 255});
    gfx_text(score, score_x, (title_h - title_fs) / 2 + bar_lab / 2, title_fs, YELLOW);
    draw_menu_button();

    // Side-gutter HUD rails, top-aligned with and hugging the playfield. LINES /
    // LEVEL stack in the left rail; the NEXT preview anchors the right rail.
    // SCORE stays in the top bar (six digits need more width than a rail offers).
    // The rail *content* is capped at cell*4 so it never balloons when the window
    // is wide/landscape (portrait renderer on a sideways tablet or Controls: On):
    // there the field stays height-limited and the raw gutter grows without bound.
    int railw = gutter;
    if (railw > cell * 4) railw = cell * 4;
    int rail_pad  = railw / 8 + 2;
    int label_fs  = railw / 6;
    int value_fs  = railw * 5 / 16;
    int lx        = play_x - railw + rail_pad;       // left rail hugs field's left
    int y        = play_y;
    y = draw_gutter_stat("LINES", TextFormat("%d", game->lines_cleared),
                         lx, y, label_fs, value_fs);
    draw_gutter_stat("LEVEL", TextFormat("%d", game->level),
                     lx, y + value_fs / 2, label_fs, value_fs);

    // NEXT preview (square mini-cells) in the right rail, hugging the field.
    int ncell  = (railw - 2 * rail_pad) / 4;
    int nbox_w = ncell * 4, nbox_h = ncell * 4;
    int rx     = play_x + field_w + rail_pad;        // right rail hugs field's right
    int nbox_y = play_y + label_fs + label_fs / 3;
    gfx_text("NEXT", rx, play_y, label_fs, (Color){150, 156, 170, 255});
    gfx_rect_lines(rx, nbox_y, nbox_w, nbox_h, LIGHTGRAY);
    gfx_rect(rx + 1, nbox_y + 1, nbox_w - 2, nbox_h - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, rx, nbox_y, nbox_w, nbox_h, ncell, next_color);

    draw_playfield(game, play_x, play_y, cell);

    draw_touch_buttons();
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
