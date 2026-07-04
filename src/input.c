#include "input.h"
#include "render.h"
#include <raylib.h>

#ifdef PLATFORM_ANDROID

// Touch controls (the whole Input struct is populated the same as the keyboard
// build, so main.c is unchanged):
//   - Bottom on-screen buttons LEFT / RIGHT / ROTATE / DROP drive gameplay.
//     LEFT/RIGHT are held (the game's DAS auto-repeats); ROTATE is a tap.
//   - DROP: quick tap = hard drop; press-and-hold = soft drop.
//   - Tapping the playfield (anywhere not on a button) also rotates.
//   - Menus/overlays: swipe up/down moves the cursor, tap selects/dismisses.
//   - Android Back button backs out (Escape's role on desktop).
//
// Button geometry comes from render_touch_button_rects(), the same rectangles
// render.c draws, so hit-testing and drawing never diverge. Android renders at
// native resolution, so touch coordinates map 1:1 to the drawn buttons.
Input input_poll(void) {
    Input input = {0};

    Rectangle b[BTN_COUNT];
    render_touch_button_rects(b);

    static bool drop_active = false;
    static double drop_t0 = 0.0;
    static Vector2 last_pos = {0, 0};
    const double DROP_TAP_MAX = 0.14; // hold longer than this = soft drop, not a tap

    int n = GetTouchPointCount();
    if (n > 0) last_pos = GetTouchPosition(0);

    // Held buttons: scan every active touch point (so you can hold a direction
    // and tap rotate at the same time).
    bool drop_touch = false;
    for (int i = 0; i < n; i++) {
        Vector2 p = GetTouchPosition(i);
        if (CheckCollisionPointRec(p, b[BTN_LEFT]))  input.left  = true;
        if (CheckCollisionPointRec(p, b[BTN_RIGHT])) input.right = true;
        if (CheckCollisionPointRec(p, b[BTN_DROP]))  drop_touch  = true;
    }

    // DROP held past the tap window = soft drop; a quick tap becomes hard drop
    // via the gesture handling below.
    double now = GetTime();
    if (drop_touch) {
        if (!drop_active) { drop_active = true; drop_t0 = now; }
        if (now - drop_t0 > DROP_TAP_MAX) input.down = true;
    } else {
        drop_active = false;
    }

    // Discrete gestures. A quick tap on DROP = hard drop; on LEFT/RIGHT = nothing
    // extra (the held scan already nudged once); anywhere else (playfield or the
    // ROTATE button) = rotate. Taps also drive menu select / overlay dismissal.
    int g = GetGestureDetected();
    if (g == GESTURE_TAP || g == GESTURE_DOUBLETAP) {
        bool on_lr   = CheckCollisionPointRec(last_pos, b[BTN_LEFT]) ||
                       CheckCollisionPointRec(last_pos, b[BTN_RIGHT]);
        bool on_drop = CheckCollisionPointRec(last_pos, b[BTN_DROP]);
        if (on_drop)     input.hard_drop_pressed = true;
        else if (!on_lr) input.rotate_pressed = true;
        input.any_pressed = true;              // dismiss pause/game-over overlays
        input.touch_tap   = true;              // menu: select the tapped item
        input.tap_x = last_pos.x;
        input.tap_y = last_pos.y;
    }
    if (g == GESTURE_SWIPE_UP)   input.menu_up   = true;
    if (g == GESTURE_SWIPE_DOWN) input.menu_down = true;

    // Android Back button: raylib delivers it as KEY_BACK.
    if (IsKeyPressed(KEY_BACK)) {
        input.escape_pressed = true;
        input.any_pressed    = true;
    }

    input.fullscreen_toggle = false; // always fullscreen on Android
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
