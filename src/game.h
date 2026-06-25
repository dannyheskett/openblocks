#ifndef OPENBLOCKS_GAME_H
#define OPENBLOCKS_GAME_H

#include "input.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t r, g, b;
} Color3;

#define PLAYFIELD_WIDTH 10
#define PLAYFIELD_HEIGHT 20
#define NUM_PIECES 7

typedef enum {
    PIECE_I = 0,
    PIECE_O,
    PIECE_T,
    PIECE_S,
    PIECE_Z,
    PIECE_J,
    PIECE_L,
} PieceType;

typedef struct {
    PieceType type;
    int x;
    int y;
    int rotation;
} Piece;

// --- Color model ----------------------------------------------------------
// Pieces are not coloured individually. Each level has a 3-colour palette (two
// block colours + white), and the palette cycles every 10 levels (level % 10).
// Each piece is statically assigned to one of the 3 palette slots. Both tables
// are defined in render.c (their sole consumer).

// Which of the 3 per-level palette slots each piece uses (0, 1, or 2 = white).
extern const int PIECE_COLOR_SLOT[NUM_PIECES];

// 10 level palettes, each {slot0, slot1, slot2}. slot2 is white on every level;
// slots 0 and 1 are the two block colours that change from level to level.
extern const Color3 LEVEL_PALETTES[10][3];

// Transient events produced during a single frame, used to drive sound effects.
enum {
    EV_MOVE     = 1 << 0, // piece shifted horizontally
    EV_ROTATE   = 1 << 1, // piece rotated
    EV_LOCK     = 1 << 2, // piece locked in place
    EV_LINE     = 1 << 3, // 1-3 lines cleared
    EV_QUAD     = 1 << 4, // 4 lines cleared at once
    EV_LEVELUP  = 1 << 5, // advanced a level
    EV_GAMEOVER = 1 << 6, // topped out
};

// The simulation is either accepting input/gravity, or playing the line-clear
// wipe animation (during which the completed rows erase from the center
// outward, two columns at a time, before the rows collapse).
typedef enum {
    PHASE_PLAY = 0,
    PHASE_CLEARING,
} GamePhase;

#define CLEAR_STEPS 5        // column-pairs erased: center (4,5) out to edges (0,9)
#define CLEAR_STEP_FRAMES 4  // frames between each erase step

typedef struct {
    uint8_t cells[PLAYFIELD_HEIGHT][PLAYFIELD_WIDTH];
    Piece current_piece;
    Piece next_piece;
    int last_piece;        // previous spawned piece type, for the randomizer
    int piece_counts[NUM_PIECES]; // running count of each piece spawned (statistics)
    int level;
    int lines_cleared;
    uint32_t score;        // points scored, saturating at 999,999 (6 digits)
    int drop_points;       // soft-drop cells on the current piece, added at lock
    bool game_over;
    int fall_frames;       // frames elapsed toward the next gravity step
    int das_dir;           // current auto-shift direction: -1 left, 0 none, +1 right
    int das_counter;       // frames remaining until the next auto-shift
    bool soft_drop;        // is the player holding Down this frame
    unsigned events;       // EV_* flags set this frame (cleared each frame)

    // Line-clear animation state (valid while phase == PHASE_CLEARING).
    int phase;             // GamePhase
    int clear_rows[4];     // y indices of the completed rows being erased
    int clear_count;       // number of completed rows
    int clear_step;        // current erase step, 0..CLEAR_STEPS
    int clear_timer;       // frames elapsed within the current step
} Game;

// Shared canonical piece geometry. Lives in game.c so collision (game.c) and
// rendering (render.c) read identical data. Indexed [type][rotation][y][x].
extern const uint8_t PIECE_DATA[NUM_PIECES][4][4][4];

Game* game_create(void);
void game_destroy(Game* game);
// Advances the simulation by one frame (the render loop runs at 60 FPS).
void game_update(Game* game);
// Per-frame held-key handling for horizontal auto-shift and soft drop.
void game_handle_held(Game* game, bool left, bool right, bool down);
void game_input(Game* game, InputType input_type);
bool game_is_over(Game* game);

#endif
