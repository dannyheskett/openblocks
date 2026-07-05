// Portrait (touch) renderer: adaptive layout drawn straight to the screen at the
// device's real resolution — title bar, HUD band, a square-cell playfield, and a
// bottom row of on-screen buttons. Compiles to an empty object off OB_PORTRAIT.
#include "render_internal.h"
#include <stddef.h> // NULL

#ifdef OB_PORTRAIT

// Height of the bottom on-screen control bar (a bit slimmer than a sixth).
static int control_bar_h(int h) { return h / 7; }

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
    int th = h / 22;                 // title bar height
    int size = th * 4 / 5;
    int pad  = (th - size) / 2;
    *out = (Rectangle){ (float)(w - size - th), (float)pad, (float)size, (float)size };
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

static void draw_game_portrait(const Game* game) {
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    gfx_clear(BLACK);

    int margin   = w / 24;               // side margin
    int title_h  = h / 22;               // top title bar
    int hud_h    = h / 9;                // HUD band under the title
    int bar_h    = control_bar_h(h);   // bottom control bar (always in portrait)
    int bottom_m = h / 40;
    int avail_h  = h - title_h - hud_h - bar_h - bottom_m;

    // Largest square cell that fits 10 wide x 20 tall in the space that remains.
    int cell_w = (w - 2 * margin) / PLAYFIELD_WIDTH;
    int cell_h = avail_h / PLAYFIELD_HEIGHT;
    int cell   = (cell_w < cell_h) ? cell_w : cell_h;
    if (cell < 1) cell = 1;
    int field_w = cell * PLAYFIELD_WIDTH;
    int field_h = cell * PLAYFIELD_HEIGHT;
    int play_x  = (w - field_w) / 2;
    int play_y  = title_h + hud_h + (avail_h - field_h) / 2;

    // Title bar pinned to the top, with the menu/pause button at its right end.
    int title_fs = title_h * 3 / 5;
    gfx_rect(0, 0, w, title_h, DARKGRAY);
    gfx_text("OPENBLOCKS", (w - gfx_measure_text("OPENBLOCKS", title_fs)) / 2,
             (title_h - title_fs) / 2, title_fs, WHITE);
    draw_menu_button();

    // HUD band: SCORE / LINES / LEVEL across the left, NEXT preview at the right.
    int hud_y    = title_h;
    int label_fs = hud_h / 5;
    int value_fs = hud_h * 2 / 5;
    int lab_y    = hud_y + hud_h / 6;
    int val_y    = hud_y + hud_h * 45 / 100;
    gfx_text("SCORE", margin, lab_y, label_fs, WHITE);
    gfx_text(TextFormat("%06u", (unsigned int)game->score), margin, val_y, value_fs, YELLOW);
    gfx_text("LINES", w * 30 / 100, lab_y, label_fs, WHITE);
    gfx_text(TextFormat("%3d", game->lines_cleared), w * 30 / 100, val_y, value_fs, YELLOW);
    gfx_text("LEVEL", w * 52 / 100, lab_y, label_fs, WHITE);
    gfx_text(TextFormat("%2d", game->level), w * 52 / 100, val_y, value_fs, YELLOW);

    // NEXT preview box (square mini-cells), right-aligned in the HUD band.
    int ncell  = hud_h / 6;
    int nbox_w = ncell * 6, nbox_h = ncell * 4;
    int nbox_x = w - margin - nbox_w;
    int nbox_y = hud_y + label_fs + hud_h / 12;
    gfx_text("NEXT", nbox_x, hud_y + hud_h / 12, label_fs, WHITE);
    gfx_rect_lines(nbox_x, nbox_y, nbox_w, nbox_h, LIGHTGRAY);
    gfx_rect(nbox_x + 1, nbox_y + 1, nbox_w - 2, nbox_h - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, nbox_x, nbox_y, nbox_w, nbox_h, ncell, next_color);

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
