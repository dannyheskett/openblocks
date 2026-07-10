// Unit tests for openblocks' game logic. These exercise the simulation in
// isolation — no raylib, no window — by including game.c directly so the
// file-static helpers (scoring, gravity, line detection/collapse) are visible.
//
// Built and run by `make test`. A non-zero exit means a failure.

#include "game.c"
#include "tick.c"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// --- Scoring ---------------------------------------------------------------
static void test_scoring(void) {
    uint32_t s;

    // Soft-drop points are a raw cell count; they must add as plain decimal
    // (the historical packed-BCD path mangled counts >= 10).
    s = 0; score_add(&s, 20);             CHECK(s == 20);
    s = 0; score_add(&s, 9); score_add(&s, 9); CHECK(s == 18);

    // Line-clear base values at level 0: 50 / 150 / 400 / 1000.
    s = 0; score_add_lines(&s, 1, 0);     CHECK(s == 50);
    s = 0; score_add_lines(&s, 2, 0);     CHECK(s == 150);
    s = 0; score_add_lines(&s, 3, 0);     CHECK(s == 400);
    s = 0; score_add_lines(&s, 4, 0);     CHECK(s == 1000);

    // Scaled by (level + 1): a quad at level 9 scores 1000 * 10.
    s = 0; score_add_lines(&s, 4, 9);     CHECK(s == 10000);

    // Out-of-range line counts are ignored.
    s = 42; score_add_lines(&s, 0, 0);    CHECK(s == 42);
    s = 42; score_add_lines(&s, 5, 0);    CHECK(s == 42);

    // Saturates at the 6-digit maximum.
    s = SCORE_MAX - 5; score_add(&s, 100); CHECK(s == SCORE_MAX);
    s = SCORE_MAX;     score_add(&s, 1);   CHECK(s == SCORE_MAX);
}

// --- Gravity curve ---------------------------------------------------------
static void test_gravity(void) {
    CHECK(get_fall_frames(0) == 50);    // slowest at level 0
    CHECK(get_fall_frames(-5) == 50);   // negative levels clamp to 0
    CHECK(get_fall_frames(100) == 1);   // floors at 1 frame/row
    // Monotonically non-increasing across the whole defined range.
    for (int lvl = 1; lvl <= 60; lvl++) {
        CHECK(get_fall_frames(lvl) <= get_fall_frames(lvl - 1));
        CHECK(get_fall_frames(lvl) >= 1);
    }
}

// --- Line detection & collapse ---------------------------------------------
static void test_line_clear(void) {
    Game g;
    int rows[4];

    // Empty board: no full rows.
    memset(&g, 0, sizeof g);
    CHECK(detect_full_rows(&g, rows) == 0);

    // Fill the bottom row completely; it should be detected.
    memset(&g, 0, sizeof g);
    for (int x = 0; x < PLAYFIELD_WIDTH; x++) {
        g.cells[PLAYFIELD_HEIGHT - 1][x] = 1;
    }
    CHECK(detect_full_rows(&g, rows) == 1);
    CHECK(rows[0] == PLAYFIELD_HEIGHT - 1);

    // A single gap means the row is not full.
    g.cells[PLAYFIELD_HEIGHT - 1][3] = 0;
    CHECK(detect_full_rows(&g, rows) == 0);

    // Collapse: bottom row full, one block resting just above it. After the
    // clear the lone block falls to the bottom and the rest is empty.
    memset(&g, 0, sizeof g);
    for (int x = 0; x < PLAYFIELD_WIDTH; x++) {
        g.cells[PLAYFIELD_HEIGHT - 1][x] = 2; // full bottom row
    }
    g.cells[PLAYFIELD_HEIGHT - 2][4] = 7;     // marker block above it
    g.clear_count = 1;
    g.clear_rows[0] = PLAYFIELD_HEIGHT - 1;
    collapse_rows(&g);
    CHECK(g.cells[PLAYFIELD_HEIGHT - 1][4] == 7); // marker dropped to the floor
    CHECK(g.cells[PLAYFIELD_HEIGHT - 2][4] == 0); // its old cell is now empty
    CHECK(g.cells[PLAYFIELD_HEIGHT - 1][0] == 0); // cleared row is gone
}

// --- Randomizer ------------------------------------------------------------
static void test_randomizer(void) {
    srand(12345);
    const int N = 60000;
    for (int last = 0; last < NUM_PIECES; last++) {
        int repeats = 0, oob = 0;
        for (int i = 0; i < N; i++) {
            int p = (int)random_piece(last);
            if (p < 0 || p >= NUM_PIECES) oob++;
            if (p == last) repeats++;
        }
        CHECK(oob == 0); // never returns a piece outside [0, NUM_PIECES)
        // Immediate repeats are possible but biased down: only the reroll path
        // (~2/8 of rolls) can land on `last`, so the rate (~1/28) is well under
        // uniform (1/7 ≈ 0.143). Assert comfortably below 1/10.
        CHECK(repeats < N / 10);
    }
    // The initial spawn passes last == -1; results still stay in range.
    for (int i = 0; i < 1000; i++) {
        int p = (int)random_piece(-1);
        CHECK(p >= 0 && p < NUM_PIECES);
    }
}

// --- Collision (walls, floor, stacked cells; no wall kicks) -----------------
static void test_collision(void) {
    Game g;
    memset(&g, 0, sizeof g);

    Piece i0 = piece_create(PIECE_I, 0, 0);      // horizontal I: row 1, cols 0-3
    CHECK(can_place(&g, &i0));
    Piece ileft = piece_create(PIECE_I, -1, 0);  // col -1 off the left wall
    CHECK(!can_place(&g, &ileft));
    Piece iright = piece_create(PIECE_I, 7, 0);  // cols 7-10 -> col 10 off the right
    CHECK(!can_place(&g, &iright));
    Piece iedge = piece_create(PIECE_I, 6, 0);   // cols 6-9 -> just fits
    CHECK(can_place(&g, &iedge));

    // Vertical I (rotation 1) is 4 rows tall: y=16 fits (16-19), y=17 hits floor.
    Piece ivfit   = (Piece){PIECE_I, 0, 16, 1};
    CHECK(can_place(&g, &ivfit));
    Piece ivfloor = (Piece){PIECE_I, 0, 17, 1};
    CHECK(!can_place(&g, &ivfloor));

    // An occupied cell blocks placement.
    g.cells[1][5] = 1;
    Piece over = piece_create(PIECE_I, 2, 0);    // row 1, cols 2-5 -> hits col 5
    CHECK(!can_place(&g, &over));
}

// --- Rotation (cycles in place; blocked rotation is a no-op) ----------------
static void test_rotation(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.current_piece = piece_create(PIECE_T, 3, 5); // open space: every rotation fits

    game_input(&g, INPUT_ROTATE);
    CHECK(g.current_piece.rotation == 1);
    CHECK(g.events & EV_ROTATE);
    game_input(&g, INPUT_ROTATE); CHECK(g.current_piece.rotation == 2);
    game_input(&g, INPUT_ROTATE); CHECK(g.current_piece.rotation == 3);
    game_input(&g, INPUT_ROTATE); CHECK(g.current_piece.rotation == 0); // wraps mod 4

    // Blocked: a horizontal I resting on the floor can't rotate to vertical.
    memset(&g, 0, sizeof g);
    g.current_piece = (Piece){PIECE_I, 3, 18, 0};
    g.events = 0;
    game_input(&g, INPUT_ROTATE);
    CHECK(g.current_piece.rotation == 0);   // unchanged
    CHECK(!(g.events & EV_ROTATE));          // no event when the rotation is rejected
}

// --- Horizontal auto-shift (DAS) -------------------------------------------
static void test_das(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.current_piece = piece_create(PIECE_O, 3, 0);

    // Fresh press: shift immediately, arm the 16-frame delay.
    game_handle_held(&g, false, true, false);
    CHECK(g.current_piece.x == 4);
    CHECK(g.events & EV_MOVE);
    CHECK(g.das_dir == 1);

    // Held 15 more frames: still within the arming delay, no repeat.
    for (int i = 0; i < 15; i++) game_handle_held(&g, false, true, false);
    CHECK(g.current_piece.x == 4);
    // 16th held frame: first auto-shift fires.
    game_handle_held(&g, false, true, false);
    CHECK(g.current_piece.x == 5);
    // Then it repeats every 6 frames.
    for (int i = 0; i < 5; i++) game_handle_held(&g, false, true, false);
    CHECK(g.current_piece.x == 5);
    game_handle_held(&g, false, true, false);
    CHECK(g.current_piece.x == 6);

    // Direction change re-shifts immediately and re-arms.
    game_handle_held(&g, true, false, false);
    CHECK(g.current_piece.x == 5);
    CHECK(g.das_dir == -1);

    // Both directions hold cancels out: no move, direction cleared.
    int x = g.current_piece.x;
    game_handle_held(&g, true, true, false);
    CHECK(g.current_piece.x == x);
    CHECK(g.das_dir == 0);
}

// --- Gravity stepping + soft drop ------------------------------------------
static void test_gravity_softdrop(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.current_piece = piece_create(PIECE_O, 4, 0);
    g.level = 0; // 50 frames per row

    for (int i = 0; i < 49; i++) game_update(&g);
    CHECK(g.current_piece.y == 0);   // hasn't reached the gravity interval yet
    game_update(&g);
    CHECK(g.current_piece.y == 1);   // 50th frame steps down one row
    CHECK(g.fall_frames == 0);

    // Soft drop: interval collapses to 2 frames and credits 1 point per cell.
    memset(&g, 0, sizeof g);
    g.current_piece = piece_create(PIECE_O, 4, 0);
    g.soft_drop = true;
    game_update(&g); CHECK(g.current_piece.y == 0); // frame 1
    game_update(&g);
    CHECK(g.current_piece.y == 1);                  // frame 2 steps down
    CHECK(g.drop_points == 1);                       // one soft-dropped cell tallied
}

// --- Hard drop -------------------------------------------------------------
static void test_hard_drop(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.current_piece = piece_create(PIECE_O, 4, 0);

    game_input(&g, INPUT_HARD_DROP);
    // The 2-tall O lands on rows 18-19: 18 cells fallen, 1 point credited per cell.
    CHECK(g.score == 18);
    CHECK(g.fall_frames == 0);
    CHECK(g.cells[19][4] == PIECE_O + 1); // locked into the board
    CHECK(g.cells[18][5] == PIECE_O + 1);
    CHECK(!g.game_over);

    // No double gravity: the freshly spawned piece doesn't also fall this frame.
    game_update(&g);
    CHECK(g.fall_frames == 1);
    CHECK(g.current_piece.y == 0);
}

// --- Two-phase line-clear pipeline -----------------------------------------
static void test_clear_pipeline(void) {
    Game g;
    int rows[4];
    memset(&g, 0, sizeof g);

    // Bottom row full except cols 4,5; an O dropped there completes exactly it.
    for (int x = 0; x < PLAYFIELD_WIDTH; x++) g.cells[PLAYFIELD_HEIGHT - 1][x] = 1;
    g.cells[PLAYFIELD_HEIGHT - 1][4] = 0;
    g.cells[PLAYFIELD_HEIGHT - 1][5] = 0;
    g.current_piece = piece_create(PIECE_O, 4, PLAYFIELD_HEIGHT - 2);

    settle_current_piece(&g);
    CHECK(g.phase == PHASE_CLEARING);
    CHECK(g.clear_count == 1);
    CHECK(g.clear_rows[0] == PLAYFIELD_HEIGHT - 1);
    CHECK(g.events & EV_LOCK);
    CHECK(g.events & EV_LINE);
    CHECK(!(g.events & EV_QUAD));

    // Drive the wipe to completion (5 steps * 4 frames), then collapse/score/spawn.
    for (int i = 0; i < CLEAR_STEPS * CLEAR_STEP_FRAMES; i++) game_update(&g);
    CHECK(g.phase == PHASE_PLAY);
    CHECK(g.lines_cleared == 1);
    CHECK(g.score == 50);          // 50 base * (level 0 + 1)
    CHECK(g.level == 0);
    CHECK(detect_full_rows(&g, rows) == 0);              // the row is gone
    CHECK(g.cells[PLAYFIELD_HEIGHT - 1][4] == PIECE_O + 1); // row above collapsed down
}

// --- Level advance (one per 10 lines), scored at the pre-advance level ------
static void test_levelup(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.lines_cleared = 9; // the next single line crosses into level 1

    for (int x = 0; x < PLAYFIELD_WIDTH; x++) g.cells[PLAYFIELD_HEIGHT - 1][x] = 1;
    g.cells[PLAYFIELD_HEIGHT - 1][4] = 0;
    g.cells[PLAYFIELD_HEIGHT - 1][5] = 0;
    g.current_piece = piece_create(PIECE_O, 4, PLAYFIELD_HEIGHT - 2);

    settle_current_piece(&g);
    for (int i = 0; i < CLEAR_STEPS * CLEAR_STEP_FRAMES; i++) game_update(&g);
    CHECK(g.lines_cleared == 10);
    CHECK(g.level == 1);
    CHECK(g.events & EV_LEVELUP);
    CHECK(g.score == 50); // scored with the level *before* it advanced (0)
}

// --- Top-out (spawn can't be placed) ---------------------------------------
static void test_topout(void) {
    Game g;
    memset(&g, 0, sizeof g);
    // Occupy the spawn region (cols 3-6, rows 0-1) without forming full rows, so
    // the piece spawned after the lock cannot be placed.
    for (int x = 3; x <= 6; x++) { g.cells[0][x] = 1; g.cells[1][x] = 1; }
    g.current_piece = piece_create(PIECE_O, 0, PLAYFIELD_HEIGHT - 2); // locks bottom-left

    settle_current_piece(&g);
    CHECK(g.game_over);
    CHECK(g.events & EV_GAMEOVER);
}

// --- Fixed-timestep accumulator (tick.c) -----------------------------------
static void test_sim_clock(void) {
    SimClock c = {0};

    // Exactly one step's worth of time -> one step, nothing banked.
    CHECK(sim_clock_advance(&c, SIM_DT) == 1);
    CHECK(c.accum == 0.0);

    // Two steps' worth -> two steps.
    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, 2 * SIM_DT) == 2);

    // Half a step banks with no step; the following half completes one.
    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, SIM_DT / 2) == 0);
    CHECK(sim_clock_advance(&c, SIM_DT / 2) == 1);

    // A 120 Hz display (half-steps) averages to exactly 60 steps over one second.
    c.accum = 0.0;
    int total = 0;
    for (int i = 0; i < 120; i++) total += sim_clock_advance(&c, SIM_DT / 2);
    CHECK(total == 60);

    // The sub-step remainder is carried across frames.
    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, SIM_DT * 3 / 2) == 1);   // one step, half a step banked
    CHECK(c.accum > SIM_DT * 0.4 && c.accum < SIM_DT * 0.6);

    // Spiral guard: a long stall runs at most SIM_MAX_STEPS and drops the backlog.
    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, 100.0) == SIM_MAX_STEPS);
    CHECK(c.accum == 0.0);

    // A stalled / backward clock contributes nothing.
    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, -1.0) == 0);
    CHECK(c.accum == 0.0);

    c.accum = 1.0;
    sim_clock_reset(&c);
    CHECK(c.accum == 0.0);
}

int main(void) {
    printf("test_game: scoring, gravity, line-clear, randomizer, collision,\n");
    printf("           rotation, DAS, soft drop, hard drop, clear, level-up, top-out,\n");
    printf("           fixed-timestep accumulator\n");
    srand(12345);
    test_scoring();
    test_gravity();
    test_line_clear();
    test_randomizer();
    test_collision();
    test_rotation();
    test_das();
    test_gravity_softdrop();
    test_hard_drop();
    test_clear_pipeline();
    test_levelup();
    test_topout();
    test_sim_clock();
    if (failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
