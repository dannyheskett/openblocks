// Landscape (desktop) renderer: the 3-column layout, hand-tuned in a 640x480
// logical space and scaled to fit whatever view it is drawn into, centred with
// black bars. Frames go out through present() (present.c), which draws the
// window at its native resolution and, while recording, the fixed 640x480
// capture. Compiles to an empty object off OB_LANDSCAPE (i.e. on Android / iOS).
#include "render_internal.h"

#ifdef OB_LANDSCAPE

#include "present.h"
#include <raylib.h>
#include <rlgl.h>
#include <stddef.h> // NULL

// A 640x480-logical draw, scaled to fit a view_w x view_h view and centred.
// The transform is pushed onto the current matrix rather than replacing it (as
// a Camera2D would), so it composes with the recorder's supersampling scale.
// Text and shapes are re-rasterized at the final scale, so they stay crisp.
typedef struct { void (*draw)(void*); void* ctx; } Scaled;

static void scaled_scene(void* p, int view_w, int view_h) {
    Scaled* s = (Scaled*)p;
    float sw = (float)view_w / BASE_WIDTH, sh = (float)view_h / BASE_HEIGHT;
    float scale = (sw < sh) ? sw : sh;
    gfx_clear(BLACK);               // bars around the scaled layout stay black
    rlPushMatrix();
    rlTranslatef((view_w - BASE_WIDTH * scale) / 2.0f,
                 (view_h - BASE_HEIGHT * scale) / 2.0f, 0.0f);
    rlScalef(scale, scale, 1.0f);
    s->draw(s->ctx);
    rlPopMatrix();
}

static void present_scaled(void (*draw)(void*), void* ctx) {
    Scaled s = { draw, ctx };
    present(scaled_scene, &s);
}

// Draw the gameplay scene in 640x480 logical coordinates.
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

// One gameplay scene with an optional centered overlay, scaled to the window.
static void draw_scene_landscape(const Game* game, const char* overlay_title,
                                 const char* overlay_sub, Color overlay_tc) {
    LandSceneCtx c = { game, overlay_title, overlay_sub, overlay_tc };
    present_scaled(draw_land_scene_cb, &c);
}

void render_frame_landscape(const Game* g)     { draw_scene_landscape(g, NULL, NULL, WHITE); }
void render_pause_landscape(const Game* g)     { draw_scene_landscape(g, "GAME PAUSED", "Press any key to resume", YELLOW); }
void render_game_over_landscape(const Game* g) { draw_scene_landscape(g, "GAME OVER", "Press any key to return to menu", RED); }

#endif // OB_LANDSCAPE
