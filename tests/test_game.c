// Unit tests for openblocks' game logic. These exercise the simulation in
// isolation — no raylib, no window — by including game.c directly so the
// file-static helpers (scoring, gravity, line detection/collapse) are visible.
//
// Built and run by `make test`. A non-zero exit means a failure.

#include "game.c"

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

int main(void) {
    printf("test_game: scoring, gravity, line-clear\n");
    test_scoring();
    test_gravity();
    test_line_clear();
    if (failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
