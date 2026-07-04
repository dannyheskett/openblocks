#ifndef OPENBLOCKS_RENDER_H
#define OPENBLOCKS_RENDER_H

#include "game.h"
#include "platform.h"
#include <raylib.h>
#include <stdbool.h>

// The game renders to a fixed off-screen canvas that present() integer-scales
// and letterboxes into the real window/screen. Desktop uses a 640x480 landscape
// canvas; Android uses a portrait canvas sized for a phone screen.
#ifdef OB_TOUCH
#define BASE_WIDTH  480
#define BASE_HEIGHT 854
#else
#define BASE_WIDTH  640
#define BASE_HEIGHT 480
#endif

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

// Whether the on-screen touch buttons are shown (and the bottom control bar
// reserved). Defaults on; the web build turns it off for desktop browsers
// (keyboard input) and on for touch devices. Android always leaves it on.
void render_set_touch_controls(bool show);
bool render_touch_controls_shown(void);

#endif
