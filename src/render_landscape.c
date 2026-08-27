// Landscape (desktop) renderer: draws the 3-column layout into a fixed 640x480
// offscreen canvas that present() letterboxes into the window. Compiles to an
// empty object off OB_LANDSCAPE (i.e. on Android / iOS).
#include "render_internal.h"

#ifdef OB_LANDSCAPE

#include "recorder.h"
#include <raylib.h>
#include <stddef.h> // NULL

RenderTexture2D canvas;              // declared extern in render_internal.h; the
                                     // fixed 640x480 canvas is now used only by
                                     // the recorder (fixed-size video).

// Continuous scale that fits the 640x480 layout in the window (never below 1x,
// so 640x480 is the minimum). Fractional is fine — a Camera2D re-rasterizes the
// layout crisply at any scale.
static float fit_scale(void) {
    float sw = (float)GetScreenWidth()  / BASE_WIDTH;
    float sh = (float)GetScreenHeight() / BASE_HEIGHT;
    float s = (sw < sh) ? sw : sh;
    return (s < 1.0f) ? 1.0f : s;
}

// Render `draw(ctx)` — which issues 640x480-logical gfx calls — to the window,
// scaled + centered at the window's native resolution via a Camera2D (so text
// and shapes stay crisp when enlarged, instead of upscaling a 640x480 texture).
// When recording, the same scene is also drawn into the fixed 640x480 canvas so
// captured video stays a constant size regardless of window size.
static void present_scaled(void (*draw)(void*), void* ctx) {
    if (recorder_active()) {
        BeginTextureMode(canvas);
        gfx_clear(BLACK);
        draw(ctx);
        EndTextureMode();
        recorder_capture(&canvas);   // clean 640x480 frame (no REC indicator)
    }

    float scale = fit_scale();
    float ox = (GetScreenWidth()  - BASE_WIDTH  * scale) / 2.0f;
    float oy = (GetScreenHeight() - BASE_HEIGHT * scale) / 2.0f;

    gfx_begin_frame();
    gfx_clear(BLACK);               // letterbox bars stay black
    Camera2D cam = { .offset = (Vector2){ox, oy}, .target = (Vector2){0, 0},
                     .rotation = 0.0f, .zoom = scale };
    BeginMode2D(cam);
    draw(ctx);
    if (recorder_active()) {        // window-only indicator, in 640x480 space
        DrawCircle(16, 14, 5.0f, RED);
        gfx_text("REC", 24, 8, 12, RED);
    }
    EndMode2D();
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

// Scene draw callbacks for present_scaled (issue 640x480-logical draws; defined
// here, after the draw helpers they call).
typedef struct {
    const Game* g;
    const char* ot; const char* os; Color otc;
} LandSceneCtx;
static void draw_land_scene_cb(void* p) {
    LandSceneCtx* c = (LandSceneCtx*)p;
    draw_game_landscape(c->g);
    if (c->ot) draw_center_panel_landscape(c->ot, c->os, c->otc);
}

typedef struct {
    MenuLayout m; const char* title; const char* const* items;
    int count, selected, gap;
} LandMenuCtx;
static void draw_land_menu_cb(void* p) {
    LandMenuCtx* c = (LandMenuCtx*)p;
    draw_menu_panel(c->m, c->title, c->items, c->count, c->selected, c->gap, false);
}

// One gameplay scene with an optional centered overlay, scaled to the window.
static void draw_scene_landscape(const Game* game, const char* overlay_title,
                                 const char* overlay_sub, Color overlay_tc) {
    LandSceneCtx c = { game, overlay_title, overlay_sub, overlay_tc };
    present_scaled(draw_land_scene_cb, &c);
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

    LandMenuCtx c = { m, title, items, count, selected, gap_before };
    present_scaled(draw_land_menu_cb, &c);
}

#endif // OB_LANDSCAPE
