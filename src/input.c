#include "input.h"
#include <raylib.h>

#ifdef PLATFORM_ANDROID

// Touch controls (the whole Input struct is populated the same as the keyboard
// build, so main.c is unchanged):
//   - Touch and slide left/right past a deadzone: steer the piece. Holding the
//     slide keeps the direction held, so the game's delayed-auto-shift repeats.
//   - Slide downward past a deadzone: soft drop (held).
//   - Quick tap: rotate (in play) / select (in menus) / dismiss (overlays).
//   - Swipe up / down: move the menu cursor.
//   - Two-finger tap: pause.
//   - Android Back button: back out (same role as Escape on desktop).
//
// Thresholds are in raw screen pixels; they are deliberately generous and will
// be fine-tuned on real devices. present() letterboxes the canvas, but these
// gestures are all displacement/direction based, so no canvas mapping is needed.
Input input_poll(void) {
    Input input = {0};

    const float H_DEADZONE = 40.0f; // px of horizontal slide before steering
    const float V_DEADZONE = 60.0f; // px of downward slide before soft drop

    static bool touch_active = false;
    static Vector2 start = {0};
    static int prev_count = 0;

    int count = GetTouchPointCount();

    // Two-finger tap (touch count rising to >= 2) pauses.
    if (count >= 2 && prev_count < 2) input.pause_pressed = true;

    if (count > 0) {
        Vector2 pos = GetTouchPosition(0);
        if (!touch_active) { touch_active = true; start = pos; }

        float dx = pos.x - start.x;
        float dy = pos.y - start.y;

        // Held horizontal steer (game_handle_held's DAS auto-repeats it).
        if (dx < -H_DEADZONE)      input.left  = true;
        else if (dx > H_DEADZONE)  input.right = true;

        // Held soft drop when the finger is dragged downward.
        if (dy > V_DEADZONE) input.down = true;
    } else {
        touch_active = false;
    }
    prev_count = count;

    // Discrete gestures. A quick tap serves as rotate / select / dismiss; the
    // app state in main.c decides which of those it reads.
    int g = GetGestureDetected();
    if (g == GESTURE_TAP || g == GESTURE_DOUBLETAP) {
        input.rotate_pressed = true;
        input.select_pressed = true;
        input.any_pressed    = true;
    }
    if (g == GESTURE_SWIPE_UP)   input.menu_up   = true;
    if (g == GESTURE_SWIPE_DOWN) input.menu_down = true;

    // Android hardware/gesture Back button: raylib delivers it as KEY_BACK.
    if (IsKeyPressed(KEY_BACK)) {
        input.escape_pressed = true;
        input.any_pressed    = true;
    }

    // No fullscreen concept on Android (always fullscreen).
    input.fullscreen_toggle = false;

    return input;
}

#else

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

#endif // PLATFORM_ANDROID
