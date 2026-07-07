#ifndef OPENBLOCKS_RENDER_H
#define OPENBLOCKS_RENDER_H

#include "game.h"
#include "platform.h"
#include "ob_types.h"
#include <stdbool.h>

// The landscape renderer draws to a fixed 640x480 off-screen canvas that
// present() integer-scales and letterboxes into the window. These are the
// landscape canvas dimensions only — the portrait renderer sizes itself from the
// live screen (GetScreenWidth/Height) and does not use BASE_WIDTH/BASE_HEIGHT.
#define BASE_WIDTH  640
#define BASE_HEIGHT 480

void render_init(void);
void render_cleanup(void);

// Gameplay scene.
void render_frame(const Game* game);
// Gameplay scene with a "paused" overlay on top.
void render_pause(const Game* game);
// Gameplay scene with a "game over" overlay on top.
void render_game_over(const Game* game);
// Floating menu: title plus a list of items, one highlighted. gap_before, if
// >= 0, inserts a blank line before that item index.
void render_menu(const char* title, const char* const* items, int count,
                 int selected, int gap_before);

bool render_window_should_close(void);
void render_toggle_fullscreen(void);
// True while the app window holds input focus. Used to auto-pause when the app
// is sent to the background (Android suspend/resume).
bool render_window_focused(void);

// On-screen touch controls (Android). The bottom control bar holds a row of
// four buttons; render.c draws them and input.c hit-tests them, both using the
// rectangles from render_touch_button_rects() so their geometry never diverges.
typedef enum {
    BTN_LEFT,
    BTN_RIGHT,
    BTN_ROTATE,
    BTN_DROP,
    BTN_COUNT,
} TouchButton;

// Fill rects[BTN_COUNT] with the current on-screen button rectangles (screen
// coordinates, which map 1:1 to touches since Android renders at native res).
void render_touch_button_rects(Rectangle rects[BTN_COUNT]);

// Return the menu item index at screen point `p`, or -1 if none. Uses the item
// rectangles captured by the last render_menu() call (Android touch menus).
int render_menu_hit_test(Vector2 p);

// The on-screen menu/pause button rectangle (top corner of the portrait
// renderer). render.c draws it; input.c hit-tests it to return to the menu.
void render_menu_button_rect(Rectangle* out);

// Active renderer selection. Native builds have exactly one renderer, so
// render_use_portrait() is a compile-time constant there (true on Android, false
// on desktop). The web build compiles both and picks at runtime:
// render_set_portrait(true) = portrait touch layout, false = desktop landscape.
void render_set_portrait(bool portrait);
bool render_use_portrait(void);

// On-screen button row toggle (portrait). Gestures are the primary controls;
// the button row is opt-in from the menu. Off by default.
void render_set_touch_buttons(bool shown);
bool render_touch_buttons_shown(void);

// Playfield cell size in pixels for the current portrait layout. The gesture
// input layer uses it as the drag distance that moves the piece one column.
int render_portrait_cell(void);

#endif
