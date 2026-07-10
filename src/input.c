#include "input.h"
#include "render.h"
#include "platform.h"
#if !defined(PLATFORM_IOS)
#include <raylib.h>  // keyboard/mouse; iOS is touch-only (queries come from plat_ios)
#endif

// input_poll() composes up to two sources into one Input:
//   - keyboard: desktop native builds and the web build (PC browsers)
//   - touch:    Android and the web build (mobile browsers)
// The web build runs both, so a phone uses gestures while a desktop browser
// uses the keyboard — same binary. Android runs only touch; desktop native
// runs only keyboard.

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
// Gesture-recognizer state that persists across frames for the current touch
// sequence (first finger down to last finger up). Pulled out of poll_touch's
// function statics into one module-owned value so the hidden state is explicit
// and resettable, rather than scattered `static` locals inside a function that
// otherwise reads as pure.
typedef struct {
    Vector2 last_pos;  // last active pointer position (source of tap coords)
    bool    active;    // a touch sequence is in progress
    Vector2 origin;    // where the sequence started (px)
    float   anchor_x;  // advancing x anchor: one column per cell-width of travel
    double  t0;        // sequence start time (s)
    int     mode;      // 0 undecided, 1 horizontal drag, 2 downward drag
    int     max_np;    // most simultaneous fingers seen during the sequence
    float   last_dy;   // most recent vertical delta from origin (flick velocity)
} TouchState;

static TouchState s_touch;

// Touch source: playfield gestures (drag/flick/tap, two-finger tap = pause)
// plus swipe menus. Only ever sets fields true, so it composes over the
// keyboard source on web without clobbering it. Native-resolution rendering
// means touch coords map 1:1 to the on-screen geometry (menu rows).
static void poll_touch(Input* in) {
    if (!render_use_portrait()) return; // landscape (desktop-browser) mode: keyboard only

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
        s_touch.last_pos = pts[0];
    }
#if !defined(PLATFORM_IOS)
    else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        s_touch.last_pos = GetMousePosition(); // remember where a click ended, for the tap below
    }
#endif

    double now = GetTime();

    // Gestures: the playfield is the controller, matching the scheme mobile
    // block-game players know. Drag sideways and the piece follows the finger
    // one column per cell-width; drag down slowly = soft drop; flick down =
    // hard drop; a plain tap (below) = rotate; a two-finger tap = pause/menu.
    int step = render_portrait_cell();
    if (step < 8) step = 8;

    if (np > 0) {
        Vector2 p = pts[0];
        if (!s_touch.active) {
            s_touch.active = true;
            s_touch.origin = p;
            s_touch.anchor_x = p.x;
            s_touch.t0 = now;
            s_touch.mode = 0;
            s_touch.max_np = 0;
        }
        if (np > s_touch.max_np) s_touch.max_np = np;
        // A second finger turns the sequence into a pause candidate (decided on
        // release); pts[0] can jump when fingers land/lift, so stop tracking
        // movement rather than misread the jump as a drag or flick.
        if (s_touch.max_np < 2) {
            float dx = p.x - s_touch.origin.x, dy = p.y - s_touch.origin.y;
            if (s_touch.mode == 0) {
                float ax = dx < 0 ? -dx : dx;
                if (ax > (float)step * 0.55f && ax > dy) s_touch.mode = 1;
                else if (dy > (float)step * 0.80f)       s_touch.mode = 2;
            }
            if (s_touch.mode == 1) {
                // One column per cell-width of travel; anchor advances so the
                // piece tracks the finger instead of accelerating.
                if (p.x - s_touch.anchor_x >= (float)step)      { in->right = true; s_touch.anchor_x += (float)step; }
                else if (p.x - s_touch.anchor_x <= -(float)step) { in->left  = true; s_touch.anchor_x -= (float)step; }
            }
            if (s_touch.mode == 2) in->down = true; // soft drop while dragging down
            s_touch.last_dy = dy;
        }
    } else if (s_touch.active) {
        // Touch ended: decide the discrete action on RELEASE. (raylib's
        // GESTURE_TAP fires on touch-DOWN, so using it here would fire a
        // rotate at the start of every drag.)
        double dur = now - s_touch.t0;
        if (s_touch.max_np >= 2) {
            // Two-finger tap: pause (back to the menu; the game stays
            // resumable). Long multi-finger contact is ignored.
            if (dur < 0.5) {
                in->escape_pressed = true;
                in->any_pressed = true;
            }
        } else if (s_touch.mode == 2 && dur < 0.35 && s_touch.last_dy > (float)step * 1.2f &&
            s_touch.last_dy / (float)dur > (float)step * 6.0f) {
            in->hard_drop_pressed = true; // fast downward flick (velocity-based)
            in->any_pressed = true;
        } else if (s_touch.mode == 0 && dur < 0.30) {
            // A tap: rotate. Also feeds menu select / overlay dismissal via
            // the tap fields.
            in->rotate_pressed = true;
            in->any_pressed = true;
            in->touch_tap   = true;
            in->tap_x = s_touch.last_pos.x;
            in->tap_y = s_touch.last_pos.y;
        }
        s_touch.active = false;
    }

    // Swipe gestures drive menu navigation (taps are decided on release, above).
    int g = GetGestureDetected();
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
