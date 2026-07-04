#include "render.h"
#include "recorder.h"
#include <raylib.h>

#define CELL_SIZE 20
#define BORDER_WIDTH 2

#ifndef PLATFORM_ANDROID
// Desktop renders to a fixed off-screen canvas that present() integer-scales and
// letterboxes into the window. Android renders straight to the screen instead.
static int current_scale = 1;
static RenderTexture2D canvas;
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
                DrawRectangle(px + 1, py + 1, cell_size - 2, cell_size - 2, color);
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
                DrawRectangle(ox + x * cell + 1, oy + y * cell + 1,
                              cell - 2, cell - 2, color);
            }
        }
    }
}

void render_init(void) {
    // Fixed 640x480 window (not resizable); Alt+Enter toggles fullscreen.
    InitWindow(BASE_WIDTH, BASE_HEIGHT, "openblocks");
    SetExitKey(KEY_NULL); // Escape is handled by the game, not the window
    SetTargetFPS(60);

#ifndef PLATFORM_ANDROID
    canvas = LoadRenderTexture(BASE_WIDTH, BASE_HEIGHT);
#endif
}

void render_toggle_fullscreen(void) {
#ifdef PLATFORM_ANDROID
    // Android apps are always fullscreen; nothing to toggle.
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
#ifndef PLATFORM_ANDROID
    UnloadRenderTexture(canvas);
#endif
    CloseWindow();
}

#ifndef PLATFORM_ANDROID
static int calculate_scale(void) {
    int window_w = GetScreenWidth();
    int window_h = GetScreenHeight();

    int scale_w = window_w / BASE_WIDTH;
    int scale_h = window_h / BASE_HEIGHT;
    int scale = (scale_w < scale_h) ? scale_w : scale_h;

    return (scale < 1) ? 1 : scale;
}

// Blit the fixed-resolution canvas to the (possibly resized) window, centered
// and integer-scaled.
static void present(void) {
    // Record the clean canvas (the recording indicator below is drawn only to
    // the window, so it never appears in the captured video).
    recorder_capture(&canvas);

    current_scale = calculate_scale();

    int scaled_width = BASE_WIDTH * current_scale;
    int scaled_height = BASE_HEIGHT * current_scale;
    int offset_x = (GetScreenWidth() - scaled_width) / 2;
    int offset_y = (GetScreenHeight() - scaled_height) / 2;

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(canvas.texture,
        (Rectangle){0, 0, BASE_WIDTH, -BASE_HEIGHT},
        (Rectangle){offset_x, offset_y, scaled_width, scaled_height},
        (Vector2){0, 0}, 0, WHITE);

    // On-screen recording indicator (window-only, not part of the video).
    if (recorder_active()) {
        int s = current_scale;
        DrawCircle(offset_x + 16 * s, offset_y + 14 * s, 5.0f * s, RED);
        DrawText("REC", offset_x + 24 * s, offset_y + 8 * s, 12 * s, RED);
    }

    EndDrawing();
}
#endif // PLATFORM_ANDROID: calculate_scale/present are desktop-only

#ifdef PLATFORM_ANDROID

// Android renders directly at the device's real resolution (no fixed canvas,
// no letterbox): the title bar is pinned to the top, a proportional HUD sits
// below it, and the 10x20 playfield uses the largest SQUARE cell that fits the
// remaining space, centered. Everything derives from the live screen size, so
// the layout fills any phone. A row of on-screen buttons sits in a bottom
// control bar (draw_touch_buttons / render_touch_button_rects below).

// Height of the bottom on-screen control bar.
static int control_bar_h(int h) { return h / 6; }

void render_touch_button_rects(Rectangle rects[BTN_COUNT]) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int bar_h = control_bar_h(h);
    int bar_y = h - bar_h;
    int gap = w / 40;
    int bw = (w - gap * (BTN_COUNT + 1)) / BTN_COUNT;
    int bh = bar_h - gap * 2;
    for (int i = 0; i < BTN_COUNT; i++) {
        rects[i] = (Rectangle){ (float)(gap + i * (bw + gap)),
                                (float)(bar_y + gap), (float)bw, (float)bh };
    }
}

// Draw the four control buttons with simple vector icons (DrawPoly triangles are
// orientation-agnostic, so no vertex-winding concerns). A button lights up while
// a finger rests on it.
static void draw_touch_buttons(void) {
    Rectangle r[BTN_COUNT];
    render_touch_button_rects(r);
    int touches = GetTouchPointCount();

    for (int i = 0; i < BTN_COUNT; i++) {
        bool pressed = false;
        for (int t = 0; t < touches; t++) {
            if (CheckCollisionPointRec(GetTouchPosition(t), r[i])) { pressed = true; break; }
        }
        DrawRectangleRounded(r[i], 0.25f, 8,
                             pressed ? (Color){70, 70, 90, 255} : (Color){35, 35, 45, 255});
        DrawRectangleRoundedLinesEx(r[i], 0.25f, 8, 2.0f, LIGHTGRAY);

        float cx = r[i].x + r[i].width / 2.0f;
        float cy = r[i].y + r[i].height / 2.0f;
        float s = (r[i].height < r[i].width ? r[i].height : r[i].width) * 0.30f;
        Vector2 c = {cx, cy};
        switch (i) {
        case BTN_LEFT:  DrawPoly(c, 3, s, 180, WHITE); break; // triangle points left
        case BTN_RIGHT: DrawPoly(c, 3, s, 0,   WHITE); break; // points right
        case BTN_ROTATE:
            DrawRing(c, s * 0.55f, s * 0.85f, 40, 320, 32, WHITE); // C-shaped arrow
            DrawPoly((Vector2){cx, cy - s * 0.7f}, 3, s * 0.34f, 0, WHITE); // arrowhead
            break;
        case BTN_DROP:
            DrawPoly((Vector2){cx, cy - s * 0.2f}, 3, s, 90, WHITE); // points down
            DrawRectangle((int)(cx - s), (int)(cy + s * 0.9f), (int)(s * 2), 3, WHITE); // floor
            break;
        }
    }
}

static void draw_game(const Game* game) {
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    ClearBackground(BLACK);

    int margin   = w / 24;               // side margin
    int title_h  = h / 22;               // top title bar
    int hud_h    = h / 9;                // HUD band under the title
    int bar_h    = control_bar_h(h);     // bottom control-button bar
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

    // Title bar pinned to the top.
    int title_fs = title_h * 3 / 5;
    DrawRectangle(0, 0, w, title_h, DARKGRAY);
    DrawText("OPENBLOCKS", (w - MeasureText("OPENBLOCKS", title_fs)) / 2,
             (title_h - title_fs) / 2, title_fs, WHITE);

    // HUD band: SCORE / LINES / LEVEL across the left, NEXT preview at the right.
    int hud_y    = title_h;
    int label_fs = hud_h / 5;
    int value_fs = hud_h * 2 / 5;
    int lab_y    = hud_y + hud_h / 6;
    int val_y    = hud_y + hud_h * 45 / 100;
    DrawText("SCORE", margin, lab_y, label_fs, WHITE);
    DrawText(TextFormat("%06u", (unsigned int)game->score), margin, val_y, value_fs, YELLOW);
    DrawText("LINES", w * 30 / 100, lab_y, label_fs, WHITE);
    DrawText(TextFormat("%3d", game->lines_cleared), w * 30 / 100, val_y, value_fs, YELLOW);
    DrawText("LEVEL", w * 52 / 100, lab_y, label_fs, WHITE);
    DrawText(TextFormat("%2d", game->level), w * 52 / 100, val_y, value_fs, YELLOW);

    // NEXT preview box (square mini-cells), right-aligned in the HUD band.
    int ncell  = hud_h / 6;
    int nbox_w = ncell * 6, nbox_h = ncell * 4;
    int nbox_x = w - margin - nbox_w;
    int nbox_y = hud_y + label_fs + hud_h / 12;
    DrawText("NEXT", nbox_x, hud_y + hud_h / 12, label_fs, WHITE);
    DrawRectangleLines(nbox_x, nbox_y, nbox_w, nbox_h, LIGHTGRAY);
    DrawRectangle(nbox_x + 1, nbox_y + 1, nbox_w - 2, nbox_h - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, nbox_x, nbox_y, nbox_w, nbox_h, ncell, next_color);

    // Playfield: border, background, grid.
    DrawRectangleLines(play_x - BORDER_WIDTH, play_y - BORDER_WIDTH,
                       field_w + BORDER_WIDTH * 2, field_h + BORDER_WIDTH * 2, LIGHTGRAY);
    DrawRectangle(play_x, play_y, field_w, field_h, BOARD_BG);
    for (int y = 0; y <= PLAYFIELD_HEIGHT; y++)
        DrawLine(play_x, play_y + y * cell, play_x + field_w, play_y + y * cell, GRID_LINE);
    for (int x = 0; x <= PLAYFIELD_WIDTH; x++)
        DrawLine(play_x + x * cell, play_y, play_x + x * cell, play_y + field_h, GRID_LINE);

    // Locked cells.
    for (int y = 0; y < PLAYFIELD_HEIGHT; y++) {
        for (int x = 0; x < PLAYFIELD_WIDTH; x++) {
            if (game->cells[y][x]) {
                int piece_type = game->cells[y][x] - 1;
                Color col = color_from_piece(piece_type, game->level);
                DrawRectangle(play_x + x * cell + 1, play_y + y * cell + 1,
                              cell - 2, cell - 2, col);
            }
        }
    }

    // Active piece during play; the four-line clear flashes cleared rows white.
    if (game->phase == PHASE_PLAY) {
        Color piece_color = color_from_piece(game->current_piece.type, game->level);
        draw_piece_ex(&game->current_piece, play_x, play_y, cell, piece_color);
    } else if (game->clear_count >= 4 && (game->clear_step % 2 == 0)) {
        for (int i = 0; i < game->clear_count; i++)
            DrawRectangle(play_x, play_y + game->clear_rows[i] * cell, field_w, cell,
                          (Color){255, 255, 255, 90});
    }

    draw_touch_buttons();
}

#else

// Draw the gameplay scene into the currently-active render target.
static void draw_game(const Game* game) {
    ClearBackground(BLACK);

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
    DrawRectangle(0, 0, BASE_WIDTH, 24, DARKGRAY);
    DrawText("OPENBLOCKS", (BASE_WIDTH - MeasureText("OPENBLOCKS", 16)) / 2, 4, 16, WHITE);

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
    DrawText("STATISTICS", stat_x, 40, 12, WHITE);
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
        DrawText(TextFormat("%03d", game->piece_counts[type]),
                 stat_x + 56, row_y + 18 - 8, 16, YELLOW); // centered on icon
    }

    // Center: playfield
    DrawRectangleLines(play_x - BORDER_WIDTH, play_y - BORDER_WIDTH,
                       field_w + BORDER_WIDTH * 2,
                       PLAYFIELD_HEIGHT * CELL_SIZE + BORDER_WIDTH * 2, LIGHTGRAY);

    DrawRectangle(play_x, play_y, field_w, PLAYFIELD_HEIGHT * CELL_SIZE, BOARD_BG);

    for (int y = 0; y <= PLAYFIELD_HEIGHT; y++) {
        DrawLine(play_x, play_y + y * CELL_SIZE, play_x + field_w, play_y + y * CELL_SIZE, GRID_LINE);
    }
    for (int x = 0; x <= PLAYFIELD_WIDTH; x++) {
        DrawLine(play_x + x * CELL_SIZE, play_y, play_x + x * CELL_SIZE, play_y + PLAYFIELD_HEIGHT * CELL_SIZE, GRID_LINE);
    }

    for (int y = 0; y < PLAYFIELD_HEIGHT; y++) {
        for (int x = 0; x < PLAYFIELD_WIDTH; x++) {
            if (game->cells[y][x]) {
                int piece_type = game->cells[y][x] - 1;
                Color col = color_from_piece(piece_type, game->level);
                int px = play_x + x * CELL_SIZE;
                int py = play_y + y * CELL_SIZE;
                DrawRectangle(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2, col);
            }
        }
    }

    // The active piece is only drawn during normal play. During the clear wipe
    // the just-locked piece is already part of the board (and is being erased).
    if (game->phase == PHASE_PLAY) {
        Color piece_color = color_from_piece(game->current_piece.type, game->level);
        draw_piece_ex(&game->current_piece, play_x, play_y, CELL_SIZE, piece_color);
    } else {
        // A four-line clear flashes the cleared rows white for emphasis.
        if (game->clear_count >= 4 && (game->clear_step % 2 == 0)) {
            for (int i = 0; i < game->clear_count; i++) {
                int py = play_y + game->clear_rows[i] * CELL_SIZE;
                DrawRectangle(play_x, py, PLAYFIELD_WIDTH * CELL_SIZE, CELL_SIZE,
                              (Color){255, 255, 255, 90});
            }
        }
    }

    // Right column: lines, score, level, next
    DrawText("LINES", right_x, 40, 12, WHITE);
    DrawText(TextFormat("%3d", game->lines_cleared), right_x, 56, 20, YELLOW);

    DrawText("SCORE", right_x, 100, 12, WHITE);
    DrawText(TextFormat("%06u", (unsigned int)game->score), right_x, 116, 20, YELLOW);

    DrawText("LEVEL", right_x, 160, 12, WHITE);
    DrawText(TextFormat("%2d", game->level), right_x, 176, 20, YELLOW);

    DrawText("NEXT", right_x, 224, 12, WHITE);
    int box_w = 6 * CELL_SIZE; // 6 cells wide so the I-piece fits comfortably
    int box_h = 4 * CELL_SIZE;
    int box_y = 244;
    DrawRectangleLines(right_x, box_y, box_w, box_h, LIGHTGRAY);
    DrawRectangle(right_x + 1, box_y + 1, box_w - 2, box_h - 2, BOARD_BG);
    Color next_color = color_from_piece(game->next_piece.type, game->level);
    draw_piece_centered(game->next_piece.type, 0, right_x, box_y, box_w, box_h, CELL_SIZE, next_color);
}

#endif // PLATFORM_ANDROID

// A floating, centered panel with a title and an optional subtitle, drawn over
// a dimmed background. Shared by the pause and game-over overlays. Sizes are
// proportional to the real screen on Android, fixed to the canvas on desktop.
static void draw_center_panel(const char* title, const char* subtitle, Color title_color) {
#ifdef PLATFORM_ANDROID
    int w = GetScreenWidth(), h = GetScreenHeight();
    int panel_w = w * 72 / 100, panel_h = h * 16 / 100;
    int ts = panel_h * 28 / 100, ss = panel_h * 13 / 100;
    int title_dy = panel_h * 24 / 100, sub_dy = panel_h * 62 / 100;
#else
    int w = BASE_WIDTH, h = BASE_HEIGHT;
    int panel_w = 340, panel_h = 120;
    int ts = 30, ss = 14;
    int title_dy = 28, sub_dy = 76;
#endif
    int cx = w / 2;
    int px = cx - panel_w / 2;
    int py = (h - panel_h) / 2;

    DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 170}); // dim
    DrawRectangle(px, py, panel_w, panel_h, (Color){15, 15, 25, 255});
    DrawRectangleLines(px, py, panel_w, panel_h, LIGHTGRAY);

    DrawText(title, cx - MeasureText(title, ts) / 2, py + title_dy, ts, title_color);
    if (subtitle) {
        DrawText(subtitle, cx - MeasureText(subtitle, ss) / 2, py + sub_dy, ss, GRAY);
    }
}

#ifdef PLATFORM_ANDROID

// Android draws straight to the screen (draw_game clears and lays out at the
// real device resolution); no offscreen canvas or letterbox.
void render_frame(const Game* game) {
    BeginDrawing();
    draw_game(game);
    EndDrawing();
}

void render_pause(const Game* game) {
    BeginDrawing();
    draw_game(game);
    draw_center_panel("GAME PAUSED", "Tap to resume", YELLOW);
    EndDrawing();
}

void render_game_over(const Game* game) {
    BeginDrawing();
    draw_game(game);
    draw_center_panel("GAME OVER", "Tap to return to menu", RED);
    EndDrawing();
}

void render_menu(const char* title, const char* const* items, int count,
                 int selected, int gap_before) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    BeginDrawing();
    ClearBackground(BLACK);

    int cx = w / 2;
    int title_size = h / 16;
    int line_h = h / 20;
    int item_fs = h / 28;
    int extra = (gap_before >= 0) ? 1 : 0;

    int panel_w = w * 72 / 100;
    int content_h = title_size + line_h + (count + extra) * line_h;
    int panel_h = content_h + line_h * 2;
    int px = cx - panel_w / 2;
    int py = (h - panel_h) / 2;

    DrawRectangle(px, py, panel_w, panel_h, (Color){15, 15, 25, 255});
    DrawRectangleLines(px, py, panel_w, panel_h, LIGHTGRAY);

    DrawText(title, cx - MeasureText(title, title_size) / 2, py + line_h, title_size, WHITE);

    int y = py + line_h + title_size + line_h;
    for (int i = 0; i < count; i++) {
        if (gap_before == i) y += line_h;
        const char* label = items[i];
        int lw = MeasureText(label, item_fs);
        Color col = (i == selected) ? YELLOW : GRAY;
        if (i == selected) {
            DrawText(">", cx - lw / 2 - item_fs * 3 / 2, y, item_fs, YELLOW);
            DrawText("<", cx + lw / 2 + item_fs / 2, y, item_fs, YELLOW);
        }
        DrawText(label, cx - lw / 2, y, item_fs, col);
        y += line_h;
    }

    EndDrawing();
}

#else

void render_frame(const Game* game) {
    BeginTextureMode(canvas);
    draw_game(game);
    EndTextureMode();
    present();
}

void render_pause(const Game* game) {
    BeginTextureMode(canvas);
    draw_game(game);
    draw_center_panel("GAME PAUSED", "Press any key to resume", YELLOW);
    EndTextureMode();
    present();
}

void render_game_over(const Game* game) {
    BeginTextureMode(canvas);
    draw_game(game);
    draw_center_panel("GAME OVER", "Press any key to return to menu", RED);
    EndTextureMode();
    present();
}

void render_menu(const char* title, const char* const* items, int count,
                 int selected, int gap_before) {
    BeginTextureMode(canvas);
    ClearBackground(BLACK);

    int cx = BASE_WIDTH / 2;
    int line_h = 30;
    int extra = (gap_before >= 0) ? 1 : 0;
    int title_size = 44;
    int items_top = 0;

    int panel_w = 320;
    int content_h = title_size + 40 + (count + extra) * line_h;
    int panel_h = content_h + 60;
    int px = cx - panel_w / 2;
    int py = (BASE_HEIGHT - panel_h) / 2;

    DrawRectangle(px, py, panel_w, panel_h, (Color){15, 15, 25, 255});
    DrawRectangleLines(px, py, panel_w, panel_h, LIGHTGRAY);

    DrawText(title, cx - MeasureText(title, title_size) / 2, py + 28, title_size, WHITE);

    items_top = py + 28 + title_size + 28;
    int y = items_top;
    for (int i = 0; i < count; i++) {
        if (gap_before == i) y += line_h; // blank line before this item
        const char* label = items[i];
        int size = 20;
        int lw = MeasureText(label, size);
        Color col = (i == selected) ? YELLOW : GRAY;
        if (i == selected) {
            DrawText(">", cx - lw / 2 - 26, y, size, YELLOW);
            DrawText("<", cx + lw / 2 + 14, y, size, YELLOW);
        }
        DrawText(label, cx - lw / 2, y, size, col);
        y += line_h;
    }

    EndTextureMode();
    present();
}

#endif // PLATFORM_ANDROID

bool render_window_should_close(void) {
    return WindowShouldClose();
}
