// Unit tests for the touch-gesture recognizer in input.c. Like test_game.c,
// the code under test runs in isolation — no raylib, no window — by compiling
// with -DPLATFORM_IOS, the raylib-free configuration input.c already supports:
// ob_types.h supplies the types and declares the touch/clock queries, and this
// file provides scripted fakes of them. The recognizer under test is the same
// C compiled into every touch platform (Android / web / iOS); only the poll
// surface behind it differs.
//
// Frames are driven at exactly 60 Hz: the recognizer's thresholds are in
// seconds (tap/flick/pause durations) and playfield-cell widths (drag
// distances), so the fake clock advances 1/60 s per polled frame.
//
// Built and run by `make test`. A non-zero exit means a failure.

#include "input.c"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// --- Scripted fake of the poll surface --------------------------------------
// input.c reads fingers, swipe gestures, layout, and the clock through these.
// Tests set the state, then call one of the frame helpers to advance 1/60 s
// and poll.

static double  fake_now;       // GetTime()
static int     fake_np;        // GetTouchPointCount()
static Vector2 fake_pts[8];    // GetTouchPosition(i)
static int     fake_gesture;   // GetGestureDetected() (swipes; 0 = none)
static bool    fake_portrait;  // render_use_portrait()
static int     fake_cell;      // render_portrait_cell() (px per playfield cell)

double  GetTime(void)              { return fake_now; }
int     GetTouchPointCount(void)   { return fake_np; }
Vector2 GetTouchPosition(int i)    { return fake_pts[i]; }
int     GetGestureDetected(void)   { return fake_gesture; }
bool    render_use_portrait(void)  { return fake_portrait; }
int     render_portrait_cell(void) { return fake_cell; }

#define DT (1.0 / 60.0)
// 40 px cells => mode thresholds: horizontal 22 px, downward 32 px; one column
// per 40 px of drag; flick needs 48 px and 240 px/s.
#define CELL 40

// Fresh recognizer + fake surface for each test.
static void reset(void) {
    memset(&s_touch, 0, sizeof s_touch);
    fake_now      = 100.0;
    fake_np       = 0;
    fake_gesture  = 0;
    fake_portrait = true;
    fake_cell     = CELL;
}

// Advance one 60 Hz frame and poll with the fingers currently set.
static Input frame(void) {
    fake_now += DT;
    return input_poll();
}

// One frame with a single finger at (x, y).
static Input frame_touch(float x, float y) {
    fake_np = 1;
    fake_pts[0] = (Vector2){x, y};
    return frame();
}

// One frame with two fingers down.
static Input frame_touch2(float x0, float y0, float x1, float y1) {
    fake_np = 2;
    fake_pts[0] = (Vector2){x0, y0};
    fake_pts[1] = (Vector2){x1, y1};
    return frame();
}

// One frame with every finger lifted (the release the recognizer decides on).
static Input frame_release(void) {
    fake_np = 0;
    return frame();
}

// --- Tap = rotate, decided on release ----------------------------------------
static void test_tap_rotates_on_release(void) {
    reset();

    // While the finger is down nothing fires — a tap must not trigger on
    // touch-DOWN or every drag would begin with a spurious rotation.
    Input in = frame_touch(200, 400);
    CHECK(!in.rotate_pressed && !in.touch_tap && !in.any_pressed);
    in = frame_touch(200, 400);
    CHECK(!in.rotate_pressed);

    // Release after ~50 ms: a tap. Rotate + the tap fields for menu hit-tests.
    in = frame_release();
    CHECK(in.rotate_pressed);
    CHECK(in.any_pressed);
    CHECK(in.touch_tap);
    CHECK(in.tap_x == 200 && in.tap_y == 400);
    CHECK(!in.hard_drop_pressed && !in.escape_pressed);

    // The release is an edge: the next empty frame is silent.
    in = frame_release();
    CHECK(!in.rotate_pressed && !in.touch_tap && !in.any_pressed);
}

// --- Long press is not a tap -------------------------------------------------
static void test_long_press_is_not_a_tap(void) {
    reset();

    frame_touch(200, 400);
    for (int i = 0; i < 20; i++) frame_touch(200, 400); // 20/60 s > 0.30 s cap
    Input in = frame_release();
    CHECK(!in.rotate_pressed && !in.touch_tap && !in.any_pressed);
    CHECK(!in.hard_drop_pressed && !in.escape_pressed);
}

// --- Jitter inside the dead zone still taps ----------------------------------
static void test_tap_survives_jitter(void) {
    reset();

    // 10 px sideways and 5 px down: under both mode thresholds (22 / 32), so
    // the sequence stays "undecided" and releases as a tap — at the finger's
    // final position, which is what the menu hit-test needs.
    frame_touch(200, 400);
    frame_touch(210, 405);
    Input in = frame_release();
    CHECK(in.rotate_pressed && in.touch_tap);
    CHECK(in.tap_x == 210 && in.tap_y == 405);
}

// --- Horizontal drag: one column per cell-width, tracking the finger ---------
static void test_drag_right_one_column_per_cell(void) {
    reset();

    Input in = frame_touch(100, 400);
    CHECK(!in.right);
    in = frame_touch(115, 400);            // 15 px: under the 22 px mode gate
    CHECK(!in.right && !in.left);
    in = frame_touch(125, 400);            // mode locks horizontal; 25 px from
    CHECK(!in.right);                       // the 100 px anchor: no column yet
    in = frame_touch(139, 400);            // 39 px: still one short
    CHECK(!in.right);
    in = frame_touch(140, 400);            // one full cell from the anchor
    CHECK(in.right);
    in = frame_touch(140, 400);            // holding still: no repeat
    CHECK(!in.right);
    in = frame_touch(181, 400);            // anchor advanced to 140; +41 px
    CHECK(in.right);

    // Releasing a decided drag quickly is NOT a tap: no rotation.
    in = frame_release();
    CHECK(!in.rotate_pressed && !in.touch_tap && !in.hard_drop_pressed);
}

static void test_drag_left_mirrors(void) {
    reset();

    frame_touch(300, 400);
    Input in = frame_touch(270, 400);      // 30 px left: mode locks, no column
    CHECK(!in.left);
    in = frame_touch(259, 400);            // 41 px from the anchor
    CHECK(in.left && !in.right);
    in = frame_touch(219, 400);            // another cell (anchor now 260)
    CHECK(in.left);
}

// A fast fling can cross several columns in one frame; the anchor advances one
// cell per polled frame until the piece catches up with the finger.
static void test_fling_catches_up_one_column_per_frame(void) {
    reset();

    frame_touch(100, 400);
    int rights = 0;
    if (frame_touch(260, 400).right) rights++; // jumped 4 cells in one frame
    for (int i = 0; i < 6; i++) {
        if (frame_touch(260, 400).right) rights++;
    }
    CHECK(rights == 4); // exactly 160 px / 40 px, no over- or under-shoot
}

// --- A horizontal drag never soft-drops --------------------------------------
static void test_horizontal_drag_never_soft_drops(void) {
    reset();

    frame_touch(100, 400);
    frame_touch(160, 400);                  // mode locked horizontal
    Input in = frame_touch(160, 470);       // then 70 px downward drift
    CHECK(!in.down);                        // stays horizontal: no soft drop

    // And its release is neither a flick nor a tap.
    in = frame_release();
    CHECK(!in.hard_drop_pressed && !in.rotate_pressed);
}

// --- Downward drag = soft drop ------------------------------------------------
static void test_downward_drag_soft_drops(void) {
    reset();

    frame_touch(200, 200);
    Input in = {0};
    // +8 px per frame: crosses the 32 px gate between the 4th and 5th step.
    for (int i = 1; i <= 4; i++) {
        in = frame_touch(200, 200 + 8.0f * (float)i);
        CHECK(!in.down);
    }
    for (int i = 5; i <= 23; i++) {
        in = frame_touch(200, 200 + 8.0f * (float)i);
        CHECK(in.down);                    // soft drop for as long as the drag holds
        CHECK(!in.hard_drop_pressed);
    }
    // 24 frames = 0.4 s: too long to be a flick, however far it travelled.
    in = frame_release();
    CHECK(!in.hard_drop_pressed && !in.down && !in.rotate_pressed);
}

// --- Flick down = hard drop (velocity- and duration-gated) --------------------
static void test_flick_hard_drops(void) {
    reset();

    frame_touch(200, 200);
    Input in = frame_touch(200, 260);       // 60 px in one frame: mode down
    CHECK(in.down);                          // (soft drop while still in contact)
    in = frame_release();                    // ~33 ms, 60 px, ~1800 px/s: flick
    CHECK(in.hard_drop_pressed);
    CHECK(in.any_pressed);
    CHECK(!in.rotate_pressed && !in.touch_tap && !in.escape_pressed);
}

static void test_slow_flick_rejected_by_velocity(void) {
    reset();

    // 50 px of travel (over the 48 px floor) but then held: by release the
    // average velocity is ~176 px/s, under the 240 px/s gate — soft drop only.
    frame_touch(200, 200);
    frame_touch(200, 240);
    frame_touch(200, 250);
    for (int i = 0; i < 14; i++) frame_touch(200, 250);
    Input in = frame_release();
    CHECK(!in.hard_drop_pressed);
}

static void test_short_flick_rejected_by_distance(void) {
    reset();

    // Fast but only 40 px: crosses the 32 px mode gate, not the 48 px flick
    // floor.
    frame_touch(200, 200);
    frame_touch(200, 240);
    Input in = frame_release();
    CHECK(!in.hard_drop_pressed && !in.rotate_pressed);
}

// --- Two-finger tap = pause/menu ----------------------------------------------
static void test_two_finger_tap_pauses(void) {
    reset();

    frame_touch(200, 300);
    Input in = frame_touch2(200, 300, 260, 300);
    CHECK(!in.escape_pressed);              // decided on release, like the tap
    in = frame_release();
    CHECK(in.escape_pressed);
    CHECK(in.any_pressed);
    CHECK(!in.rotate_pressed && !in.touch_tap && !in.hard_drop_pressed);
}

static void test_two_finger_hold_ignored(void) {
    reset();

    frame_touch(200, 300);
    for (int i = 0; i < 32; i++) frame_touch2(200, 300, 260, 300); // > 0.5 s
    Input in = frame_release();
    CHECK(!in.escape_pressed && !in.rotate_pressed && !in.any_pressed);
}

// Once a second finger has landed, pointer-0 can jump between fingers; the
// recognizer must stop tracking movement rather than misread the jump as a
// drag or flick.
static void test_second_finger_freezes_tracking(void) {
    reset();

    frame_touch(200, 400);
    Input in = frame_touch2(400, 400, 210, 410); // pts[0] jumps 200 px
    CHECK(!in.right && !in.left && !in.down);
    in = frame_touch2(400, 480, 210, 410);       // and 80 px downward
    CHECK(!in.down);
    in = frame_release();
    CHECK(in.escape_pressed);                     // still a two-finger tap
    CHECK(!in.hard_drop_pressed && !in.rotate_pressed);
}

// --- Swipes drive the menus; raylib's touch-down TAP gesture is ignored -------
static void test_swipes_drive_menus(void) {
    reset();

    fake_gesture = GESTURE_SWIPE_UP;
    Input in = frame();
    CHECK(in.menu_up && !in.menu_down);

    fake_gesture = GESTURE_SWIPE_DOWN;
    in = frame();
    CHECK(in.menu_down && !in.menu_up);

    // GESTURE_TAP fires on touch-DOWN, so the recognizer deliberately ignores
    // it (taps are decided on release, above).
    fake_gesture = GESTURE_TAP;
    in = frame();
    CHECK(!in.rotate_pressed && !in.touch_tap && !in.menu_up && !in.menu_down);

    fake_gesture = 0;
    in = frame();
    CHECK(!in.menu_up && !in.menu_down);
}

// --- Landscape (desktop-browser) mode: the touch layer is inert ---------------
static void test_landscape_ignores_touch(void) {
    reset();
    fake_portrait = false;

    fake_gesture = GESTURE_SWIPE_UP;
    frame_touch(200, 400);
    frame_touch(200, 460);
    Input in = frame_release();
    CHECK(!in.rotate_pressed && !in.hard_drop_pressed && !in.down);
    CHECK(!in.menu_up && !in.touch_tap && !in.any_pressed);
}

// --- Thresholds scale with the layout's cell size ------------------------------
static void test_thresholds_scale_with_cell_size(void) {
    reset();
    fake_cell = 80; // bigger cells: 30 px is jitter here, not a drag

    frame_touch(100, 400);
    frame_touch(130, 400);
    Input in = frame_release();
    CHECK(in.rotate_pressed && in.touch_tap); // still a tap at 80 px cells

    reset();
    fake_cell = 80;
    frame_touch(100, 400);
    in = frame_touch(190, 400); // 90 px: one full 80 px column
    CHECK(in.right);

    // A degenerate cell size (portrait layout not measured yet) floors the
    // step at 8 px instead of treating every wobble as a full column.
    reset();
    fake_cell = 0;
    frame_touch(100, 400);
    in = frame_touch(109, 400);
    CHECK(in.right);
}

int main(void) {
    printf("test_input: touch gestures — tap/rotate, drag move, soft drop, flick\n");
    printf("            hard drop, two-finger pause, swipe menus, mode guards\n");
    test_tap_rotates_on_release();
    test_long_press_is_not_a_tap();
    test_tap_survives_jitter();
    test_drag_right_one_column_per_cell();
    test_drag_left_mirrors();
    test_fling_catches_up_one_column_per_frame();
    test_horizontal_drag_never_soft_drops();
    test_downward_drag_soft_drops();
    test_flick_hard_drops();
    test_slow_flick_rejected_by_velocity();
    test_short_flick_rejected_by_distance();
    test_two_finger_tap_pauses();
    test_two_finger_hold_ignored();
    test_second_finger_freezes_tracking();
    test_swipes_drive_menus();
    test_landscape_ignores_touch();
    test_thresholds_scale_with_cell_size();
    if (failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
