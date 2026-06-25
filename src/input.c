#include "input.h"
#include <raylib.h>

Input input_poll(void) {
    Input input = {0};

    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

    // Held movement.
    input.left  = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
    input.right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    input.down  = IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S);

    // Space is the primary rotate key; a few alternates are accepted too.
    input.rotate_pressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) ||
                           IsKeyPressed(KEY_W) || IsKeyPressed(KEY_X) || IsKeyPressed(KEY_Z);

    // Alt+Enter toggles fullscreen; a plain Enter (without Alt) pauses/selects.
    input.fullscreen_toggle = alt && IsKeyPressed(KEY_ENTER);
    bool enter = IsKeyPressed(KEY_ENTER) && !alt;
    input.pause_pressed = enter;

    // Menu navigation.
    input.menu_up   = IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W);
    input.menu_down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    input.select_pressed = enter || IsKeyPressed(KEY_SPACE);
    input.escape_pressed = IsKeyPressed(KEY_ESCAPE);

    // Any key (drains one entry from the per-frame key-press queue).
    input.any_pressed = GetKeyPressed() != 0;

    return input;
}
