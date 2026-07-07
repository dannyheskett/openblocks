#include "input.h"
#include "render.h"
#include "platform.h"
#if !defined(PLATFORM_IOS)
#include <raylib.h>  // keyboard/mouse; iOS is touch-only (queries come from plat_ios)
#endif

// input_poll() composes up to two sources into one Input:
//   - keyboard: desktop native builds and the web build (PC browsers)
//   - touch:    Android and the web build (mobile browsers)
// The web build runs both, so a phone uses the on-screen buttons while a desktop
// browser uses the keyboard — same binary. Android runs only touch; desktop
// native runs only keyboard.

#if !defined(PLATFORM_ANDROID) && !defined(PLATFORM_IOS)
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
    if (!render_use_portrait()) return; // landscape (desktop-browser) mode: keyboard only

    static Vector2 last_pos = {0, 0};

    // Active pointers: touch points, or the mouse while its button is held.
    // Desktop browsers report no touch points for a mouse, so without this the
    // touch controls never register and every click falls through to rotate.
    int n = GetTouchPointCount();
    Vector2 pts[8];
    int np = 0;
    for (int i = 0; i < n && np < 8; i++) pts[np++] = GetTouchPosition(i);
#if !defined(PLATFORM_IOS)
    // Desktop browsers report a mouse, not a touch point; fold it in so the
    // touch controls work with a click. iOS has no mouse.
    if (n == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) pts[np++] = GetMousePosition();
#endif

    if (np > 0) {
        last_pos = pts[0];
    }
#if !defined(PLATFORM_IOS)
    else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        last_pos = GetMousePosition(); // remember where a click ended, for the tap below
    }
#endif

    double now = GetTime();
    bool buttons = render_touch_buttons_shown();
    bool on_lr = false, on_drop = false;

    if (buttons) {
        // Button mode: the bottom row is the controller.
        Rectangle b[BTN_COUNT];
        render_touch_button_rects(b);

        static bool drop_active = false;
        static double drop_t0 = 0.0;
        const double DROP_TAP_MAX = 0.14; // hold longer than this = soft drop, not a tap

        // Held buttons (scan every active pointer so you can hold a direction + tap).
        bool drop_touch = false;
        for (int i = 0; i < np; i++) {
            if (CheckCollisionPointRec(pts[i], b[BTN_LEFT]))  in->left  = true;
            if (CheckCollisionPointRec(pts[i], b[BTN_RIGHT])) in->right = true;
            if (CheckCollisionPointRec(pts[i], b[BTN_DROP]))  drop_touch  = true;
        }

        // DROP held past the tap window = soft drop; a quick tap = hard drop (below).
        if (drop_touch) {
            if (!drop_active) { drop_active = true; drop_t0 = now; }
            if (now - drop_t0 > DROP_TAP_MAX) in->down = true;
        } else {
            drop_active = false;
        }
        on_lr   = CheckCollisionPointRec(last_pos, b[BTN_LEFT]) ||
                  CheckCollisionPointRec(last_pos, b[BTN_RIGHT]);
        on_drop = CheckCollisionPointRec(last_pos, b[BTN_DROP]);
    } else {
        // Gesture mode (default): the playfield is the controller, matching the
        // scheme mobile block-game players know. Drag sideways and the piece
        // follows the finger one column per cell-width; drag down slowly = soft
        // drop; flick down = hard drop; a plain tap (below) = rotate.
        static bool   active = false;
        static Vector2 origin = {0, 0};
        static float  anchor_x = 0;
        static double t0 = 0;
        static int    mode = 0;  // 0 undecided, 1 horizontal, 2 down
        static bool   was_active = false;
        static float  last_dy = 0;

        int step = render_portrait_cell();
        if (step < 8) step = 8;

        if (np > 0) {
            Vector2 p = pts[0];
            if (!active) {
                active = true;
                origin = p;
                anchor_x = p.x;
                t0 = now;
                mode = 0;
            }
            float dx = p.x - origin.x, dy = p.y - origin.y;
            if (mode == 0) {
                float ax = dx < 0 ? -dx : dx;
                if (ax > (float)step * 0.55f && ax > dy) mode = 1;
                else if (dy > (float)step * 0.80f)       mode = 2;
            }
            if (mode == 1) {
                // One column per cell-width of travel; anchor advances so the
                // piece tracks the finger instead of accelerating.
                if (p.x - anchor_x >= (float)step)      { in->right = true; anchor_x += (float)step; }
                else if (p.x - anchor_x <= -(float)step) { in->left  = true; anchor_x -= (float)step; }
            }
            if (mode == 2) in->down = true; // soft drop while dragging down
            last_dy = dy;
        } else if (active) {
            // Touch ended: decide the discrete action on RELEASE. (raylib's
            // GESTURE_TAP fires on touch-DOWN, so using it here would fire a
            // rotate at the start of every drag.)
            double dur = now - t0;
            if (mode == 2 && dur < 0.35 && last_dy > (float)step * 1.2f &&
                last_dy / (float)dur > (float)step * 6.0f) {
                in->hard_drop_pressed = true; // fast downward flick (velocity-based)
                in->any_pressed = true;
            } else if (mode == 0 && dur < 0.30) {
                // A tap: pause key -> menu, anywhere else -> rotate. Also feeds
                // menu select / overlay dismissal via the tap fields.
                Rectangle mb; render_menu_button_rect(&mb);
                if (CheckCollisionPointRec(last_pos, mb)) in->escape_pressed = true;
                else                                      in->rotate_pressed = true;
                in->any_pressed = true;
                in->touch_tap   = true;
                in->tap_x = last_pos.x;
                in->tap_y = last_pos.y;
            }
            active = false;
        }
        was_active = active; (void)was_active;
    }

    // Discrete gestures (button mode only — gesture mode decides taps on
    // release, above). A tap on DROP = hard drop; on LEFT/RIGHT = nothing
    // extra; anywhere else = rotate. Taps also drive menu select / dismissal.
    int g = GetGestureDetected();
    if (buttons && (g == GESTURE_TAP || g == GESTURE_DOUBLETAP)) {
        Rectangle mb; render_menu_button_rect(&mb);
        bool on_menu = CheckCollisionPointRec(last_pos, mb);
        if (on_menu)      in->escape_pressed = true;   // pause key -> back to menu
        else if (on_drop) in->hard_drop_pressed = true;
        else if (!on_lr)  in->rotate_pressed = true;
        in->any_pressed = true;
        in->touch_tap   = true;
        in->tap_x = last_pos.x;
        in->tap_y = last_pos.y;
    }
    if (g == GESTURE_SWIPE_UP)   in->menu_up   = true;
    if (g == GESTURE_SWIPE_DOWN) in->menu_down = true;

#if !defined(PLATFORM_IOS)
    // Android hardware/gesture Back button (KEY_BACK); harmless no-op on web.
    // iOS has no key events (and no hardware back button).
    if (IsKeyPressed(KEY_BACK)) {
        in->escape_pressed = true;
        in->any_pressed    = true;
    }
#endif
}
#endif // OB_TOUCH

Input input_poll(void) {
    Input in = {0};
#if !defined(PLATFORM_ANDROID) && !defined(PLATFORM_IOS)
    poll_keyboard(&in);   // desktop native + web (PC browsers)
#endif
#ifdef OB_TOUCH
    poll_touch(&in);      // Android + web (mobile browsers)
#endif
    return in;
}
