#include "game.h"
#include "input.h"
#include <stdlib.h>
#include <string.h>

// Canonical piece geometry (shared with render.c via game.h). Each rotation
// state is a 4x4 grid; rotation 0 is the spawn orientation. There are no wall
// kicks, so rotation simply cycles through these states in place.
const uint8_t PIECE_DATA[NUM_PIECES][4][4][4] = {
    // I
    {
        {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0}},
        {{0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0}},
        {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}},
    },
    // O
    {
        {{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
    },
    // T
    {
        {{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
    // S
    {
        {{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0}},
        {{1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
    // Z
    {
        {{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0}},
    },
    // J
    {
        {{0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0}},
        {{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {1,0,0,0}, {1,0,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0}},
    },
    // L
    {
        {{1,0,0,0}, {1,0,0,0}, {1,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
    },
};

static Piece piece_create(PieceType type, int x, int y) {
    return (Piece){.type = type, .x = x, .y = y, .rotation = 0};
}

// Randomizer: roll an 8-sided value; if it lands on the spare slot (7) or
// matches the previous piece, reroll once over the 7 real pieces. This biases
// against immediate repeats without eliminating them entirely.
static PieceType random_piece(int last_piece) {
    int roll = rand() % (NUM_PIECES + 1);
    if (roll == NUM_PIECES || roll == last_piece) {
        roll = rand() % NUM_PIECES;
    }
    return (PieceType)roll;
}

static void get_piece_blocks(const Piece* piece, int blocks[4][2]) {
    const uint8_t (*shape)[4] = PIECE_DATA[piece->type][piece->rotation];
    int idx = 0;
    for (int y = 0; y < 4 && idx < 4; y++) {
        for (int x = 0; x < 4 && idx < 4; x++) {
            if (shape[y][x]) {
                blocks[idx][0] = piece->x + x;
                blocks[idx][1] = piece->y + y;
                idx++;
            }
        }
    }
}

static bool can_place(const Game* game, const Piece* piece) {
    int blocks[4][2];
    get_piece_blocks(piece, blocks);

    for (int i = 0; i < 4; i++) {
        int x = blocks[i][0];
        int y = blocks[i][1];

        if (x < 0 || x >= PLAYFIELD_WIDTH || y < 0 || y >= PLAYFIELD_HEIGHT) {
            return false;
        }
        if (game->cells[y][x]) {
            return false;
        }
    }
    return true;
}

static void lock_piece(Game* game, const Piece* piece) {
    int blocks[4][2];
    get_piece_blocks(piece, blocks);

    uint8_t piece_color = (uint8_t)(piece->type + 1);
    for (int i = 0; i < 4; i++) {
        int x = blocks[i][0];
        int y = blocks[i][1];
        if (y >= 0 && y < PLAYFIELD_HEIGHT && x >= 0 && x < PLAYFIELD_WIDTH) {
            game->cells[y][x] = piece_color;
        }
    }
}

// Record the y indices of every completed row into rows[] (top to bottom).
static int detect_full_rows(const Game* game, int rows[4]) {
    int n = 0;
    for (int y = 0; y < PLAYFIELD_HEIGHT && n < 4; y++) {
        bool full = true;
        for (int x = 0; x < PLAYFIELD_WIDTH; x++) {
            if (!game->cells[y][x]) { full = false; break; }
        }
        if (full) rows[n++] = y;
    }
    return n;
}

// Drop every non-cleared row down to fill the gaps left by the cleared rows,
// emptying the rows that scroll in at the top.
static void collapse_rows(Game* game) {
    uint8_t out[PLAYFIELD_HEIGHT][PLAYFIELD_WIDTH] = {{0}};
    int dst = PLAYFIELD_HEIGHT - 1;
    for (int y = PLAYFIELD_HEIGHT - 1; y >= 0; y--) {
        bool cleared = false;
        for (int i = 0; i < game->clear_count; i++) {
            if (game->clear_rows[i] == y) { cleared = true; break; }
        }
        if (!cleared) {
            memcpy(out[dst], game->cells[y], PLAYFIELD_WIDTH);
            dst--;
        }
    }
    memcpy(game->cells, out, sizeof(out));
}

// Gravity, in frames-per-row, indexed by level (tuned by feel). The loop runs
// at 60 FPS, so level 0 drops every 50 frames (~0.83s); the fall rate
// accelerates with each level down to a hard floor of 1 frame/row.
static int get_fall_frames(int level) {
    static const int frames[30] = {
        50, 44, 39, 34, 29, 24, 19, 14, 9, 7,
         6,  5,  5,  4,  4,  3,  3,  3, 2, 2,
         2,  2,  2,  2,  2,  1,  1,  1, 1, 1,
    };
    if (level < 0) level = 0;
    if (level >= 30) return 1;
    return frames[level];
}

#define SCORE_MAX 999999u // 6-digit display limit

// Add points to the score, saturating at the 6-digit maximum.
static void score_add(uint32_t* score, uint32_t points) {
    uint32_t s = *score + points;
    *score = (s > SCORE_MAX) ? SCORE_MAX : s;
}

// Award line-clear points: the base value for N lines scaled by (level + 1),
// using the level *before* it advances. Base values: 50/150/400/1000.
static void score_add_lines(uint32_t* score, int lines, int level) {
    static const uint32_t base[5] = {0, 50, 150, 400, 1000};
    if (lines < 1 || lines > 4) return;
    score_add(score, base[lines] * (uint32_t)(level + 1));
}

// The game is a fixed-size POD and there is only ever one instance, so a single
// static value backs the pointer-returning API instead of a heap allocation
// (which also removes the out-of-memory exit path). game_destroy is a no-op kept
// for API symmetry; main.c's NULL check still distinguishes "no game yet" from a
// live game, since the pointer is non-NULL only after game_create() is called.
Game* game_create(void) {
    static Game game;
    memset(&game, 0, sizeof game); // score, board, counters, phase — all start at zero

    game.last_piece = random_piece(-1);
    game.next_piece = piece_create(random_piece(game.last_piece), 3, 0);
    game.current_piece = piece_create((PieceType)game.last_piece, 3, 0);
    game.piece_counts[game.current_piece.type]++; // count the first piece in play

    return &game;
}

void game_destroy(Game* game) {
    (void)game; // static instance; nothing to free
}

static void spawn_next(Game* game) {
    game->current_piece = game->next_piece;
    game->last_piece = game->next_piece.type;
    game->piece_counts[game->current_piece.type]++; // statistics tally
    game->next_piece = piece_create(random_piece(game->last_piece), 3, 0);
}

// Spawn the next piece; if it can't be placed, the stack has reached the top and
// the game is over. Shared by the lock path and the line-clear finish so the two
// can't drift.
static void spawn_or_topout(Game* game) {
    spawn_next(game);
    if (!can_place(game, &game->current_piece)) {
        game->game_over = true;
        game->events |= EV_GAMEOVER;
    }
}

// Begin the line-clear wipe for the rows just completed. The blocks stay on the
// board and erase over the animation; scoring and collapse happen at the end.
static void start_clear(Game* game, int rows[4], int count) {
    game->phase = PHASE_CLEARING;
    game->clear_count = count;
    for (int i = 0; i < count; i++) game->clear_rows[i] = rows[i];
    game->clear_step = 0;
    game->clear_timer = 0;
    if (count >= 4)     game->events |= EV_QUAD;
    else if (count > 0) game->events |= EV_LINE;
}

// Collapse the cleared rows, award score, advance level, and spawn the next
// piece. Called once the wipe animation has fully erased the rows.
static void finish_clear(Game* game) {
    int lines = game->clear_count;
    collapse_rows(game);

    game->lines_cleared += lines;
    score_add_lines(&game->score, lines, game->level); // level BEFORE advancing
    int new_level = game->lines_cleared / 10; // one level per 10 lines
    if (new_level > game->level) game->events |= EV_LEVELUP;
    game->level = new_level;

    game->phase = PHASE_PLAY;
    game->clear_count = 0;

    spawn_or_topout(game);
}

// Advance the wipe: every CLEAR_STEP_FRAMES frames, erase one more column pair
// from the center of each completed row outward (columns 4&5, then 3&6, ...).
static void update_clear_animation(Game* game) {
    if (++game->clear_timer < CLEAR_STEP_FRAMES) return;
    game->clear_timer = 0;

    int left = 4 - game->clear_step;
    int right = 5 + game->clear_step;
    for (int i = 0; i < game->clear_count; i++) {
        int y = game->clear_rows[i];
        game->cells[y][left] = 0;
        game->cells[y][right] = 0;
    }

    game->clear_step++;
    if (game->clear_step >= CLEAR_STEPS) {
        finish_clear(game);
    }
}

// Lock the current piece where it sits, credit pending drop points, then either
// start the line-clear wipe or spawn the next piece (topping out if it can't
// spawn). Shared by gravity-driven locking and hard drop.
static void settle_current_piece(Game* game) {
    lock_piece(game, &game->current_piece);
    game->events |= EV_LOCK;

    // Drop points (soft and hard) are credited once, here at lock (1 per cell).
    score_add(&game->score, (uint32_t)game->drop_points);
    game->drop_points = 0;

    int rows[4];
    int count = detect_full_rows(game, rows);
    if (count > 0) {
        // Play the wipe animation; collapse and spawn happen at its end.
        start_clear(game, rows, count);
    } else {
        spawn_or_topout(game);
    }
}

void game_update(Game* game) {
    if (game->game_over) return;

    if (game->phase == PHASE_CLEARING) {
        update_clear_animation(game);
        return;
    }

    // Soft drop: holding Down drops the piece once every 2 frames (~30 rows/sec).
    // It never slows the piece down, so at high levels where gravity is already
    // faster we keep gravity's rate.
    int gravity = get_fall_frames(game->level);
    int interval = gravity;
    if (game->soft_drop && gravity > 2) {
        interval = 2;
    }

    game->fall_frames++;
    if (game->fall_frames >= interval) {
        game->fall_frames = 0;

        Piece next = game->current_piece;
        next.y++;

        if (can_place(game, &next)) {
            game->current_piece = next;
            if (game->soft_drop) {
                game->drop_points++; // 1 per soft-dropped cell, tallied for lock
            }
        } else {
            settle_current_piece(game);
        }
    }
}

// Try to shift the current piece horizontally by dx. There are no wall kicks,
// so a blocked shift is simply ignored. Returns true if the piece moved.
static bool try_shift(Game* game, int dx) {
    Piece next = game->current_piece;
    next.x += dx;
    if (can_place(game, &next)) {
        game->current_piece = next;
        return true;
    }
    return false;
}

// Horizontal delayed auto-shift and soft-drop, evaluated once per frame from
// held key state. The first press shifts immediately and arms a 16-frame delay;
// while the key stays held, each subsequent shift repeats every 6 frames.
void game_handle_held(Game* game, bool left, bool right, bool down) {
    game->events = 0; // start of frame: clear last frame's events

    // No piece is under player control while the wipe animation plays.
    if (game->game_over || game->phase == PHASE_CLEARING) {
        game->soft_drop = false;
        game->das_dir = 0;
        return;
    }

    game->soft_drop = down;

    int dir = (right ? 1 : 0) - (left ? 1 : 0); // 0 if both or neither held
    if (dir == 0) {
        game->das_dir = 0;
        return;
    }

    if (dir != game->das_dir) {
        // Fresh press or direction change: shift now, arm the long delay.
        if (try_shift(game, dir)) game->events |= EV_MOVE;
        game->das_dir = dir;
        game->das_counter = 16;
    } else if (--game->das_counter <= 0) {
        // Held: auto-repeat at the short interval (no per-step blip).
        try_shift(game, dir);
        game->das_counter = 6;
    }
}

void game_input(Game* game, InputType input_type) {
    if (game->game_over || game->phase == PHASE_CLEARING) return;

    // Only discrete, edge-triggered actions flow through here. Horizontal
    // movement and soft drop are handled per-frame in game_handle_held().
    if (input_type == INPUT_ROTATE) {
        Piece next = game->current_piece;
        next.rotation = (next.rotation + 1) % 4;
        if (can_place(game, &next)) {
            game->current_piece = next;
            game->events |= EV_ROTATE;
        }
    } else if (input_type == INPUT_HARD_DROP) {
        // Fall straight down to the lowest valid row, crediting 1 point per cell
        // (as soft drop does), then lock immediately.
        int dropped = 0;
        for (;;) {
            Piece test = game->current_piece;
            test.y++;
            if (!can_place(game, &test)) break;
            game->current_piece = test;
            dropped++;
        }
        game->drop_points += dropped;
        game->fall_frames = 0; // don't also apply gravity to the new piece this frame
        settle_current_piece(game);
    }
}

bool game_is_over(Game* game) {
    return game->game_over;
}
