// Common renderer TU: shared state and draw helpers, window/loop lifecycle, and
// the public entry points that dispatch to the active renderer. The two
// renderers live in render_portrait.c and render_landscape.c.
#include "render_internal.h"
#if !defined(PLATFORM_IOS)
#include <raylib.h>  // window/timing (InitWindow, …); absent on iOS
#endif

// render_use_portrait() reports the active renderer. Native builds have exactly
// one (compile-time constant); the web build has both and picks at runtime.
#if defined(OB_RUNTIME_RENDERER)
static bool s_portrait_mode = false;
void render_set_portrait(bool portrait) { s_portrait_mode = portrait; }
bool render_use_portrait(void) { return s_portrait_mode; }
#elif defined(OB_PORTRAIT)
bool render_use_portrait(void) { return true; }   // Android / iOS: portrait only
#else
bool render_use_portrait(void) { return false; }  // desktop native: landscape only
#endif

// Portrait on-screen button row: opt-in (gestures are the primary controls).
static bool s_touch_buttons = false;
void render_set_touch_buttons(bool shown) { s_touch_buttons = shown; }
bool render_touch_buttons_shown(void) { return s_touch_buttons; }

// Repeated playfield colours, named for clarity (shared with both renderers).
const Color BOARD_BG  = {20, 20, 20, 255}; // playfield / NEXT-box fill
const Color GRID_LINE = {40, 40, 40, 255}; // faint playfield grid lines

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

// A piece's colour is its fixed palette slot looked up in the current level's
// 3-colour palette, which cycles every 10 levels.
Color color_from_piece(int piece_type, int level) {
    int slot = PIECE_COLOR_SLOT[piece_type];
    Color3 c = LEVEL_PALETTES[level % 10][slot];
    return (Color){c.r, c.g, c.b, 255};
}

// Piece geometry is shared from game.c (declared in game.h) so collision and
// rendering can never disagree.
void draw_piece_ex(const Piece* piece, int offset_x, int offset_y, int cell_size, Color color) {
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

// Draw a piece's shape (in the given rotation) centered inside a box. Computes
// the piece's actual bounding box first so every piece is centered the same way
// regardless of how it sits in its 4x4 grid. Used by the NEXT preview and the
// statistics icons.
void draw_piece_centered(int piece_type, int rotation, int box_x, int box_y,
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
void draw_playfield(const Game* game, int play_x, int play_y, int cell) {
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

// A floating, centered panel with a title and an optional subtitle, drawn over a
// dimmed background. Shared by the pause and game-over overlays.
void draw_center_panel_at(int w, int h, int panel_w, int panel_h, int ts,
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

// Menu item rectangles captured by the last render_menu() (for touch hit-testing
// on portrait); written by draw_menu_panel, read by render_menu_hit_test.
static Rectangle s_menu_item_rects[8];
static int s_menu_item_count = 0;

// Draw the menu panel, centred title, and item list with selection markers.
// `capture` records each row's rectangle for touch hit-testing (portrait);
// landscape passes false (keyboard-only).
void draw_menu_panel(MenuLayout m, const char* title, const char* const* items,
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

// --- Lifecycle -------------------------------------------------------------
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
#if defined(PLATFORM_ANDROID)
    // Request 0x0: raylib's Android backend then renders at the device's native
    // resolution. Any fixed size here gets aspect-letterboxed into the display
    // (with GetScreenWidth/Height reporting the request, not the device), which
    // shrank the whole game into a 640x480 box in the middle of the screen.
    InitWindow(0, 0, "openblocks");
#else
    // Fixed 640x480 window (not resizable); Alt+Enter toggles fullscreen.
    InitWindow(BASE_WIDTH, BASE_HEIGHT, "openblocks");
#endif
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
#endif // PLATFORM_ANDROID / PLATFORM_IOS

void render_cleanup(void) {
#ifdef OB_LANDSCAPE
    UnloadRenderTexture(canvas);
#endif
#if !defined(PLATFORM_IOS)
    CloseWindow();
#endif
}

// --- Public entry points ---------------------------------------------------
// Dispatch to the active renderer: compile-time on native builds that have only
// one, runtime on web, which has both.
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
