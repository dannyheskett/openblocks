#include "input.h"
#include "render.h"
#include "platform.h"
#include <raylib.h>

// input_poll() composes up to two sources into one Input:
//   - keyboard: desktop native builds and the web build (PC browsers)
//   - touch:    Android and the web build (mobile browsers)
// The web build runs both, so a phone uses the on-screen buttons while a desktop
// browser uses the keyboard — same binary. Android runs only touch; desktop
// native runs only keyboard.

#if !defined(PLATFORM_ANDROID)
// Keyboard source: sets the base field values.
static void poll_keyboard(Input* in) {
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

    // Held movement.
    in->left  = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
    in->right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    in->down  = IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S);

    // Space is the primary rotate key; a few alternates are accepted too.
    in->rotate_pressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) ||
                         IsKeyPressed(KEY_W) || IsKeyPressed(KEY_X) || IsKeyPressed(KEY_Z);

    // Alt+Enter toggles fullscreen; a plain Enter (without Alt) pauses/selects.
    in->fullscreen_toggle = alt && IsKeyPressed(KEY_ENTER);
    bool enter = IsKeyPressed(KEY_ENTER) && !alt;
    in->pause_pressed = enter;

    // Menu navigation.
    in->menu_up   = IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W);
    in->menu_down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    in->select_pressed = enter || IsKeyPressed(KEY_SPACE);
    in->escape_pressed = IsKeyPressed(KEY_ESCAPE);

    // Any key (drains one entry from the per-frame key-press queue).
    in->any_pressed = GetKeyPressed() != 0;
}
#endif // !PLATFORM_ANDROID

#ifdef OB_TOUCH
// Touch source: on-screen buttons LEFT/RIGHT/ROTATE/DROP plus tap-to-rotate and
// swipe menus. Only ever sets fields true, so it composes over the keyboard
// source on web without clobbering it. Button geometry comes from
// render_touch_button_rects() (matches what render.c draws); native-resolution
// rendering means touch coords map 1:1 to the buttons.
static void poll_touch(Input* in) {
    if (!render_touch_controls_shown()) return; // desktop browsers: keyboard only
    Rectangle b[BTN_COUNT];
    render_touch_button_rects(b);

    static bool drop_active = false;
    static double drop_t0 = 0.0;
    static Vector2 last_pos = {0, 0};
    const double DROP_TAP_MAX = 0.14; // hold longer than this = soft drop, not a tap

    int n = GetTouchPointCount();
    if (n > 0) last_pos = GetTouchPosition(0);

    // Held buttons (scan every touch point so you can hold a direction + tap).
    bool drop_touch = false;
    for (int i = 0; i < n; i++) {
        Vector2 p = GetTouchPosition(i);
        if (CheckCollisionPointRec(p, b[BTN_LEFT]))  in->left  = true;
        if (CheckCollisionPointRec(p, b[BTN_RIGHT])) in->right = true;
        if (CheckCollisionPointRec(p, b[BTN_DROP]))  drop_touch  = true;
    }

    // DROP held past the tap window = soft drop; a quick tap = hard drop (below).
    double now = GetTime();
    if (drop_touch) {
        if (!drop_active) { drop_active = true; drop_t0 = now; }
        if (now - drop_t0 > DROP_TAP_MAX) in->down = true;
    } else {
        drop_active = false;
    }

    // Discrete gestures. Quick tap on DROP = hard drop; on LEFT/RIGHT = nothing
    // extra; anywhere else (playfield or ROTATE) = rotate. Taps also drive menu
    // select / overlay dismissal.
    int g = GetGestureDetected();
    if (g == GESTURE_TAP || g == GESTURE_DOUBLETAP) {
        bool on_lr   = CheckCollisionPointRec(last_pos, b[BTN_LEFT]) ||
                       CheckCollisionPointRec(last_pos, b[BTN_RIGHT]);
        bool on_drop = CheckCollisionPointRec(last_pos, b[BTN_DROP]);
        if (on_drop)     in->hard_drop_pressed = true;
        else if (!on_lr) in->rotate_pressed = true;
        in->any_pressed = true;
        in->touch_tap   = true;
        in->tap_x = last_pos.x;
        in->tap_y = last_pos.y;
    }
    if (g == GESTURE_SWIPE_UP)   in->menu_up   = true;
    if (g == GESTURE_SWIPE_DOWN) in->menu_down = true;

    // Android hardware/gesture Back button (KEY_BACK); harmless no-op on web.
    if (IsKeyPressed(KEY_BACK)) {
        in->escape_pressed = true;
        in->any_pressed    = true;
    }
}
#endif // OB_TOUCH

Input input_poll(void) {
    Input in = {0};
#if !defined(PLATFORM_ANDROID)
    poll_keyboard(&in);   // desktop native + web (PC browsers)
#endif
#ifdef OB_TOUCH
    poll_touch(&in);      // Android + web (mobile browsers)
#endif
    return in;
}
