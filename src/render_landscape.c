// Landscape (desktop) renderer: draws the 3-column layout into a fixed 640x480
// offscreen canvas that present() letterboxes into the window. Compiles to an
// empty object off OB_LANDSCAPE (i.e. on Android / iOS).
#include "render_internal.h"

#ifdef OB_LANDSCAPE

#include "recorder.h"
#include <raylib.h>
#include <stddef.h> // NULL

RenderTexture2D canvas;              // declared extern in render_internal.h
static int current_scale = 1;

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

static void draw_center_panel_landscape(const char* title, const char* subtitle, Color tc) {
    draw_center_panel_at(BASE_WIDTH, BASE_HEIGHT, 340, 120, 30, 14, 28, 76, title, subtitle, tc);
}

// One gameplay scene (into the canvas) with an optional centered overlay.
static void draw_scene_landscape(const Game* game, const char* overlay_title,
                                 const char* overlay_sub, Color overlay_tc) {
    BeginTextureMode(canvas);
    draw_game_landscape(game);
    if (overlay_title) draw_center_panel_landscape(overlay_title, overlay_sub, overlay_tc);
    EndTextureMode();
    present();
}

void render_frame_landscape(const Game* g)     { draw_scene_landscape(g, NULL, NULL, WHITE); }
void render_pause_landscape(const Game* g)     { draw_scene_landscape(g, "GAME PAUSED", "Press any key to resume", YELLOW); }
void render_game_over_landscape(const Game* g) { draw_scene_landscape(g, "GAME OVER", "Press any key to return to menu", RED); }

void render_menu_landscape(const char* title, const char* const* items, int count,
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
