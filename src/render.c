#include "render.h"
#include "gfx.h"
#include "recorder.h"
#include <stddef.h>  // NULL
#if !defined(PLATFORM_IOS)
#include <raylib.h>  // window/timing + the landscape RenderTexture path; absent on iOS
#endif

#define CELL_SIZE 20
#define BORDER_WIDTH 2

#ifdef OB_LANDSCAPE
// The landscape renderer draws to a fixed off-screen canvas that present()
// integer-scales and letterboxes into the window.
static int current_scale = 1;
static RenderTexture2D canvas;
#endif

// render_use_portrait() reports the active renderer. Native builds have exactly
// one (compile-time constant); the web build has both and picks at runtime.
#if defined(OB_RUNTIME_RENDERER)
static bool s_portrait_mode = false;
void render_set_portrait(bool portrait) { s_portrait_mode = portrait; }
bool render_use_portrait(void) { return s_portrait_mode; }
#elif defined(OB_PORTRAIT)
bool render_use_portrait(void) { return true; }   // Android: portrait only
#else
bool render_use_portrait(void) { return false; }  // desktop native: landscape only
#endif

// Repeated playfield colours, named for clarity.
static const Color BOARD_BG  = {20, 20, 20, 255}; // playfield / NEXT-box fill
static const Color GRID_LINE = {40, 40, 40, 255}; // faint playfield grid lines

// Per-piece palette-slot assignment and the 10 per-level palettes (declared in
// game.h). slot 2 is white; slots 0 and 1 are the two block colours per level.
const int PIECE_COLOR_SLOT[NUM_PIECES] = {
    0, // I
    2, // O  -> white slot
    1, // T
    0, // S
    1, // Z
    1, // J
    0, // L
};

const Color3 LEVEL_PALETTES[10][3] = {
    {{ 80,  80, 240}, {  0, 200, 240}, {240, 240, 240}}, // L0 blue / cyan
    {{  0, 200,  60}, {180, 240, 100}, {240, 240, 240}}, // L1 green / lime
    {{200,   0, 200}, {240, 120, 240}, {240, 240, 240}}, // L2 purple / pink
    {{ 60,  80, 240}, {200,  40,  40}, {240, 240, 240}}, // L3 blue / red
    {{  0, 200,  60}, {200,   0, 200}, {240, 240, 240}}, // L4 green / purple
    {{  0, 200, 200}, { 80,  80, 240}, {240, 240, 240}}, // L5 cyan / blue
    {{200,  40,  40}, {120, 120, 120}, {240, 240, 240}}, // L6 red / gray
    {{120,   0, 200}, {200,   0,  80}, {240, 240, 240}}, // L7 violet / magenta
    {{200,   0,   0}, { 60,  80, 240}, {240, 240, 240}}, // L8 red / blue
    {{240, 140,   0}, {200,  40,  40}, {240, 240, 240}}, // L9 orange / red
};

// Piece geometry is shared from game.c (declared in game.h) so collision and
// rendering can never disagree.

static void draw_piece_ex(const Piece* piece, int offset_x, int offset_y, int cell_size, Color color) {
    const uint8_t (*shape)[4] = PIECE_DATA[piece->type][piece->rotation];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (shape[y][x]) {
                int px = offset_x + (piece->x + x) * cell_size;
                int py = offset_y + (piece->y + y) * cell_size;
                gfx_rect(px + 1, py + 1, cell_size - 2, cell_size - 2, color);
            }
        }
    }
}

// A piece's colour is its fixed palette slot looked up in the current level's
// 3-colour palette, which cycles every 10 levels.
static Color color_from_piece(int piece_type, int level) {
    int slot = PIECE_COLOR_SLOT[piece_type];
    Color3 c = LEVEL_PALETTES[level % 10][slot];
    return (Color){c.r, c.g, c.b, 255};
}

// Draw a piece's shape (in the given rotation) centered inside a box. Computes
// the piece's actual bounding box first so every piece is centered the same way
// regardless of how it sits in its 4x4 grid. Used by the NEXT preview and the
// statistics icons.
static void draw_piece_centered(int piece_type, int rotation, int box_x, int box_y,
                                int box_w, int box_h, int cell, Color color) {
    const uint8_t (*shape)[4] = PIECE_DATA[piece_type][rotation];

    int minx = 4, maxx = -1, miny = 4, maxy = -1;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (shape[y][x]) {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    if (maxx < 0) return; // empty (shouldn't happen)

    int w_px = (maxx - minx + 1) * cell;
    int h_px = (maxy - miny + 1) * cell;
    int ox = box_x + (box_w - w_px) / 2 - minx * cell;
    int oy = box_y + (box_h - h_px) / 2 - miny * cell;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            if (shape[y][x]) {
                gfx_rect(ox + x * cell + 1, oy + y * cell + 1,
                              cell - 2, cell - 2, color);
            }
        }
    }
}

// Playfield at (play_x, play_y) with square cell size `cell`: border, background,
// grid, locked cells, the active piece, and the four-line-clear flash. Shared by
// both renderers, which differ only in the cell size and where the field sits.
static void draw_playfield(const Game* game, int play_x, int play_y, int cell) {
    int field_w = cell * PLAYFIELD_WIDTH;
    int field_h = cell * PLAYFIELD_HEIGHT;

    gfx_rect_lines(play_x - BORDER_WIDTH, play_y - BORDER_WIDTH,
                   field_w + BORDER_WIDTH * 2, field_h + BORDER_WIDTH * 2, LIGHTGRAY);
    gfx_rect(play_x, play_y, field_w, field_h, BOARD_BG);
    for (int y = 0; y <= PLAYFIELD_HEIGHT; y++)
        gfx_line(play_x, play_y + y * cell, play_x + field_w, play_y + y * cell, GRID_LINE);
    for (int x = 0; x <= PLAYFIELD_WIDTH; x++)
        gfx_line(play_x + x * cell, play_y, play_x + x * cell, play_y + field_h, GRID_LINE);

    for (int y = 0; y < PLAYFIELD_HEIGHT; y++) {
        for (int x = 0; x < PLAYFIELD_WIDTH; x++) {
            if (game->cells[y][x]) {
                Color col = color_from_piece(game->cells[y][x] - 1, game->level);
                gfx_rect(play_x + x * cell + 1, play_y + y * cell + 1, cell - 2, cell - 2, col);
            }
        }
    }

    // Active piece during play; the four-line clear flashes cleared rows white.
    if (game->phase == PHASE_PLAY) {
        Color piece_color = color_from_piece(game->current_piece.type, game->level);
        draw_piece_ex(&game->current_piece, play_x, play_y, cell, piece_color);
    } else if (game->clear_count >= 4 && (game->clear_step % 2 == 0)) {
        for (int i = 0; i < game->clear_count; i++)
            gfx_rect(play_x, play_y + game->clear_rows[i] * cell, field_w, cell,
                     (Color){255, 255, 255, 90});
    }
}

void render_init(void) {
#if defined(PLATFORM_IOS)
    // iOS: UIKit owns the window/surface and drives the loop (CADisplayLink); the
    // Metal layer is attached separately by the app shell. Nothing to do here.
#else
#if defined(PLATFORM_ANDROID)
    // Request immersive fullscreen so the app draws under the status bar / camera
    // cutout (paired with windowLayoutInDisplayCutoutMode=shortEdges in the theme)
    // — otherwise the surface is letterboxed below the status bar.
    SetConfigFlags(FLAG_FULLSCREEN_MODE);
#elif defined(PLATFORM_WEB)
    // Let the GL canvas follow the browser viewport (the HTML shell sizes it);
    // GetScreenWidth/Height then track it so the layout re-fits on resize/rotate.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#endif
    // Fixed 640x480 window (not resizable); Alt+Enter toggles fullscreen.
    InitWindow(BASE_WIDTH, BASE_HEIGHT, "openblocks");
    SetExitKey(KEY_NULL); // Escape is handled by the game, not the window
    SetTargetFPS(60);

#ifdef OB_LANDSCAPE
    canvas = LoadRenderTexture(BASE_WIDTH, BASE_HEIGHT);
#endif
#endif // PLATFORM_IOS
}

void render_toggle_fullscreen(void) {
#if defined(PLATFORM_ANDROID) || defined(PLATFORM_IOS)
    // Android / iOS apps are always fullscreen; nothing to toggle.
    (void)0;
}
#else
    // Borderless fullscreen at the monitor's resolution; present() integer-
    // scales and centers the fixed canvas inside it.
    if (!IsWindowFullscreen()) {
        int mon = GetCurrentMonitor();
        SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
        ToggleFullscreen();
    } else {
        ToggleFullscreen();
        SetWindowSize(BASE_WIDTH, BASE_HEIGHT);
    }
}
#endif // PLATFORM_ANDROID

void render_cleanup(void) {
#ifdef OB_LANDSCAPE
    UnloadRenderTexture(canvas);
#endif
#if !defined(PLATFORM_IOS)
    CloseWindow();
#endif
}

#ifdef OB_LANDSCAPE
#ifndef PLATFORM_WEB
// Desktop native: crisp integer scaling (1x, 2x, 3x, ...).
static int calculate_scale(void) {
    int scale_w = GetScreenWidth() / BASE_WIDTH;
    int scale_h = GetScreenHeight() / BASE_HEIGHT;
    int scale = (scale_w < scale_h) ? scale_w : scale_h;
    return (scale < 1) ? 1 : scale;
}
#endif

// Blit the fixed-resolution canvas to the window, centered and scaled.
static void present(void) {
    // Record the clean canvas (the recording indicator below is drawn only to
    // the window, so it never appears in the captured video).
    recorder_capture(&canvas);

#ifdef PLATFORM_WEB
    // Web: scale continuously to fill the viewport. Browser chrome usually leaves
    // us just under an integer step, so snapping down (like the desktop app)
    // would render tiny; nearest-neighbor keeps it crisp at fractional scale.
    float sw = (float)GetScreenWidth()  / BASE_WIDTH;
    float sh = (float)GetScreenHeight() / BASE_HEIGHT;
    float scale = (sw < sh) ? sw : sh;
    if (scale < 1.0f) scale = 1.0f;
#else
    float scale = (float)calculate_scale();
#endif
    current_scale = (int)scale;

    float scaled_width  = BASE_WIDTH  * scale;
    float scaled_height = BASE_HEIGHT * scale;
    float offset_x = (GetScreenWidth()  - scaled_width)  / 2.0f;
    float offset_y = (GetScreenHeight() - scaled_height) / 2.0f;

    gfx_begin_frame();
    gfx_clear(BLACK);
    DrawTexturePro(canvas.texture,
        (Rectangle){0, 0, BASE_WIDTH, -BASE_HEIGHT},
        (Rectangle){offset_x, offset_y, scaled_width, scaled_height},
        (Vector2){0, 0}, 0, WHITE);

    // On-screen recording indicator (window-only, not part of the video).
    if (recorder_active()) {
        int s = current_scale;
        DrawCircle(offset_x + 16 * s, offset_y + 14 * s, 5.0f * s, RED);
        gfx_text("REC", offset_x + 24 * s, offset_y + 8 * s, 12 * s, RED);
    }

    gfx_end_frame();
}
#endif // OB_LANDSCAPE: canvas / calculate_scale / present

#ifdef OB_PORTRAIT

// The portrait renderer draws directly at the device's real resolution (no
// fixed canvas, no letterbox): the title bar is pinned to the top, a
// proportional HUD sits below it, and the 10x20 playfield uses the largest
// SQUARE cell that fits the remaining space, centered. Everything derives from
// the live screen size, so the layout fills any phone. A row of on-screen
// buttons sits in a bottom control bar (draw_touch_buttons / render_touch_
// button_rects below).

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
// vector icons (DrawPoly triangles are orientation-agnostic — no winding
// concerns). Buttons brighten slightly while a finger rests on them.
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

#endif // OB_PORTRAIT

#ifdef OB_LANDSCAPE

// Draw the gameplay scene into the currently-active render target (canvas).
static void draw_game_landscape(const Game* game) {
    gfx_clear(BLACK);

    // 3-column layout (hand-tuned for 640x480):
    // Left: STATISTICS | Center: playfield | Right: LINES/SCORE/LEVEL/NEXT.
    int field_w = PLAYFIELD_WIDTH * CELL_SIZE;          // 200
    int col_w  = 6 * CELL_SIZE;                          // side columns match NEXT box = 120
    int gap    = 30;                                     // equal gap field <-> each column
    int play_x = (BASE_WIDTH - field_w) / 2;            // centered playfield = 220
    int play_y = 56;
    // Symmetric side columns: equidistant from the centered field and from the
    // window edges, so left/right whitespace mirror each other.
    int stat_x  = play_x - gap - col_w;                 // 70
    int right_x = play_x + field_w + gap;               // 450

    // Top border/title
    gfx_rect(0, 0, BASE_WIDTH, 24, DARKGRAY);
    gfx_text("OPENBLOCKS", (BASE_WIDTH - gfx_measure_text("OPENBLOCKS", 16)) / 2, 4, 16, WHITE);

    // Left column: piece statistics — a running count of each piece spawned,
    // in a fixed order.
    static const int STAT_ORDER[NUM_PIECES] = {
        PIECE_T, PIECE_J, PIECE_Z, PIECE_O, PIECE_S, PIECE_L, PIECE_I
    };
    // J and L spawn vertically in PIECE_DATA; show them flat in the stats list
    // (rotation 1) so every icon reads as a tidy horizontal piece.
    static const int STAT_ROTATION[NUM_PIECES] = {
        [PIECE_I] = 0, [PIECE_O] = 0, [PIECE_T] = 0, [PIECE_S] = 0,
        [PIECE_Z] = 0, [PIECE_J] = 1, [PIECE_L] = 1,
    };
    gfx_text("STATISTICS", stat_x, 40, 12, WHITE);
    // Spread the 7 rows evenly across the playfield's height so the left column
    // shares the field's vertical rhythm. Icon box is 40x36; count text is
    // vertically centered against it.
    int rows_top = play_y + 8;
    int row_step = (PLAYFIELD_HEIGHT * CELL_SIZE - 36) / (NUM_PIECES - 1); // 60
    for (int i = 0; i < NUM_PIECES; i++) {
        int type = STAT_ORDER[i];
        int row_y = rows_top + i * row_step;
        Color c = color_from_piece(type, game->level);
        draw_piece_centered(type, STAT_ROTATION[type], stat_x, row_y, 40, 36, 9, c);
        gfx_text(TextFormat("%03d", game->piece_counts[type]),
                 stat_x + 56, row_y + 18 - 8, 16, YELLOW); // centered on icon
    }

    // Center: playfield
    draw_playfield(game, play_x, play_y, CELL_SIZE);

    // Right column: lines, score, level, next
    gfx_text("LINES", right_x, 40, 12, WHITE);
    gfx_text(TextFormat("%3d", game->lines_cleared), right_x, 56, 20, YELLOW);

    gfx_text("SCORE", right_x, 100, 12, WHITE);
    gfx_text(TextFormat("%06u", (unsigned int)game->score), right_x, 116, 20, YELLOW);

    gfx_text("LEVEL", right_x, 160, 12, WHITE);
    gfx_text(TextFormat("%2d", game->level), right_x, 176, 20, YELLOW);

    gfx_text("NEXT", right_x, 224, 12, WHITE);
    int box_w = 6 * CELL_SIZE; // 6 cells wide so the I-piece fits comfortably
    int box_h = 4 * CELL_SIZE;
    int box_y = 244;
    gfx_rect_lines(right_x, box_y, box_w, box_h, LIGHTGRAY);
    gfx_rect(right_x + 1, box_y + 1, box_w - 2, box_h - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, right_x, box_y, box_w, box_h, CELL_SIZE, next_color);
}

#endif // OB_LANDSCAPE

// A floating, centered panel with a title and an optional subtitle, drawn over a
// dimmed background. Shared by the pause and game-over overlays.
static void draw_center_panel_at(int w, int h, int panel_w, int panel_h, int ts,
                                 int ss, int title_dy, int sub_dy, const char* title,
                                 const char* subtitle, Color title_color) {
    int cx = w / 2;
    int px = cx - panel_w / 2;
    int py = (h - panel_h) / 2;

    gfx_rect(0, 0, w, h, (Color){0, 0, 0, 170}); // dim
    gfx_rect(px, py, panel_w, panel_h, (Color){15, 15, 25, 255});
    gfx_rect_lines(px, py, panel_w, panel_h, LIGHTGRAY);

    gfx_text(title, cx - gfx_measure_text(title, ts) / 2, py + title_dy, ts, title_color);
    if (subtitle) {
        gfx_text(subtitle, cx - gfx_measure_text(subtitle, ss) / 2, py + sub_dy, ss, GRAY);
    }
}

#ifdef OB_PORTRAIT
static void draw_center_panel_portrait(const char* title, const char* subtitle, Color tc) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int base = (w < h) ? w : h;   // keep the dialog compact even in a wide window
    int pw = base * 82 / 100;
    int ph = base * 26 / 100;
    draw_center_panel_at(w, h, pw, ph, ph * 28 / 100, ph * 13 / 100,
                         ph * 24 / 100, ph * 62 / 100, title, subtitle, tc);
}
#endif
#ifdef OB_LANDSCAPE
static void draw_center_panel_landscape(const char* title, const char* subtitle, Color tc) {
    draw_center_panel_at(BASE_WIDTH, BASE_HEIGHT, 340, 120, 30, 14, 28, 76, title, subtitle, tc);
}
#endif

// Menu item rectangles captured by the last render_menu(), for touch hit-testing
// (Android/iOS). Populated by the portrait render_menu; stays empty on desktop.
static Rectangle s_menu_item_rects[8];
static int s_menu_item_count = 0;

// Computed menu geometry (each renderer fills this from its own sizing), passed
// to the shared draw_menu_panel so the panel + title + item list are drawn once.
typedef struct {
    int cx, px, py, panel_w, panel_h;
    int title_size, title_y, items_y, line_h, item_fs;
} MenuLayout;

// Draw the menu panel, centred title, and item list with selection markers.
// `capture` records each row's rectangle for touch hit-testing (portrait);
// landscape passes false (keyboard-only).
static void draw_menu_panel(MenuLayout m, const char* title, const char* const* items,
                            int count, int selected, int gap_before, bool capture) {
    gfx_rect(m.px, m.py, m.panel_w, m.panel_h, (Color){15, 15, 25, 255});
    gfx_rect_lines(m.px, m.py, m.panel_w, m.panel_h, LIGHTGRAY);
    gfx_text(title, m.cx - gfx_measure_text(title, m.title_size) / 2, m.title_y, m.title_size, WHITE);

    s_menu_item_count = capture ? ((count < 8) ? count : 8) : 0;
    int y = m.items_y;
    for (int i = 0; i < count; i++) {
        if (gap_before == i) y += m.line_h;
        const char* label = items[i];
        int lw = gfx_measure_text(label, m.item_fs);
        Color col = (i == selected) ? YELLOW : GRAY;
        if (i == selected) {
            gfx_text(">", m.cx - lw / 2 - m.item_fs * 3 / 2, y, m.item_fs, YELLOW);
            gfx_text("<", m.cx + lw / 2 + m.item_fs / 2, y, m.item_fs, YELLOW);
        }
        gfx_text(label, m.cx - lw / 2, y, m.item_fs, col);
        if (capture && i < 8) {
            s_menu_item_rects[i] = (Rectangle){ (float)m.px, (float)(y - (m.line_h - m.item_fs) / 2),
                                                (float)m.panel_w, (float)m.line_h };
        }
        y += m.line_h;
    }
}

#ifdef OB_PORTRAIT

// The portrait renderer draws straight to the screen at native resolution. One
// gameplay scene with an optional centered overlay (pause / game over).
static void draw_scene_portrait(const Game* game, const char* overlay_title,
                                const char* overlay_sub, Color overlay_tc) {
    gfx_begin_frame();
    draw_game_portrait(game);
    if (overlay_title) draw_center_panel_portrait(overlay_title, overlay_sub, overlay_tc);
    gfx_end_frame();
}

static void render_frame_portrait(const Game* g)     { draw_scene_portrait(g, NULL, NULL, WHITE); }
static void render_pause_portrait(const Game* g)     { draw_scene_portrait(g, "GAME PAUSED", "Tap to resume", YELLOW); }
static void render_game_over_portrait(const Game* g) { draw_scene_portrait(g, "GAME OVER", "Tap to return to menu", RED); }

static void render_menu_portrait(const char* title, const char* const* items, int count,
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

#ifdef OB_LANDSCAPE

// Landscape draws into the fixed offscreen canvas, which present() letterboxes
// into the window. One gameplay scene with an optional centered overlay.
static void draw_scene_landscape(const Game* game, const char* overlay_title,
                                 const char* overlay_sub, Color overlay_tc) {
    BeginTextureMode(canvas);
    draw_game_landscape(game);
    if (overlay_title) draw_center_panel_landscape(overlay_title, overlay_sub, overlay_tc);
    EndTextureMode();
    present();
}

static void render_frame_landscape(const Game* g)     { draw_scene_landscape(g, NULL, NULL, WHITE); }
static void render_pause_landscape(const Game* g)     { draw_scene_landscape(g, "GAME PAUSED", "Press any key to resume", YELLOW); }
static void render_game_over_landscape(const Game* g) { draw_scene_landscape(g, "GAME OVER", "Press any key to return to menu", RED); }

static void render_menu_landscape(const char* title, const char* const* items, int count,
                                  int selected, int gap_before) {
    int cx = BASE_WIDTH / 2;
    int line_h = 30, item_fs = 20, title_size = 44;
    int extra = (gap_before >= 0) ? 1 : 0;
    int panel_w = 320;
    int panel_h = title_size + 40 + (count + extra) * line_h + 60;
    int px = cx - panel_w / 2, py = (BASE_HEIGHT - panel_h) / 2;
    MenuLayout m = { .cx = cx, .px = px, .py = py, .panel_w = panel_w, .panel_h = panel_h,
                     .title_size = title_size, .title_y = py + 28,
                     .items_y = py + 28 + title_size + 28,
                     .line_h = line_h, .item_fs = item_fs };

    BeginTextureMode(canvas);
    gfx_clear(BLACK);
    draw_menu_panel(m, title, items, count, selected, gap_before, false);
    EndTextureMode();
    present();
}

#endif // OB_LANDSCAPE

// Public entry points dispatch to the active renderer (compile-time on native
// builds that have only one; runtime on web, which has both).
#if defined(OB_RUNTIME_RENDERER)
  #define OB_DISPATCH(fn, ...) do { if (s_portrait_mode) fn##_portrait(__VA_ARGS__); else fn##_landscape(__VA_ARGS__); } while (0)
#elif defined(OB_PORTRAIT)
  #define OB_DISPATCH(fn, ...) fn##_portrait(__VA_ARGS__)
#else
  #define OB_DISPATCH(fn, ...) fn##_landscape(__VA_ARGS__)
#endif

void render_frame(const Game* game)     { OB_DISPATCH(render_frame, game); }
void render_pause(const Game* game)     { OB_DISPATCH(render_pause, game); }
void render_game_over(const Game* game) { OB_DISPATCH(render_game_over, game); }
void render_menu(const char* title, const char* const* items, int count,
                 int selected, int gap_before) {
    OB_DISPATCH(render_menu, title, items, count, selected, gap_before);
}

int render_menu_hit_test(Vector2 p) {
    for (int i = 0; i < s_menu_item_count; i++) {
        if (CheckCollisionPointRec(p, s_menu_item_rects[i])) return i;
    }
    return -1;
}

bool render_window_should_close(void) {
    return WindowShouldClose();
}

bool render_window_focused(void) {
    return IsWindowFocused();
}
