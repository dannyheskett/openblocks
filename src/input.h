#ifndef OPENBLOCKS_INPUT_H
#define OPENBLOCKS_INPUT_H

#include <stdbool.h>

// Discrete, edge-triggered gameplay actions routed through game_input().
// (Held movement and soft drop are handled separately via game_handle_held.)
typedef enum {
    INPUT_ROTATE,
    INPUT_HARD_DROP,   // slam the piece to the bottom and lock it immediately
} InputType;

typedef struct {
    // Gameplay
    bool left;            // held state, drives auto-shift
    bool right;           // held state, drives auto-shift
    bool down;            // held state, drives soft drop
    bool rotate_pressed;  // edge-triggered: Space (one rotation per press)
    bool hard_drop_pressed; // edge-triggered: slam to bottom (Drop button tap)
    bool pause_pressed;   // edge-triggered: Enter

    // Menu / overlays
    bool menu_up;         // edge-triggered: move cursor up
    bool menu_down;       // edge-triggered: move cursor down
    bool select_pressed;  // edge-triggered: Enter or Space
    bool escape_pressed;  // edge-triggered: Escape
    bool any_pressed;     // any key pressed this frame (used to dismiss overlays)

    // Window
    bool fullscreen_toggle; // Alt+Enter
} Input;

Input input_poll(void);

#endif
