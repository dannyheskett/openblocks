#ifndef OPENBLOCKS_RENDER_H
#define OPENBLOCKS_RENDER_H

#include "game.h"
#include "platform.h"
#include "ob_types.h"
#include <stdbool.h>

// The landscape renderer lays its 3-column layout out in a 640x480 logical
// space and scales it to fit the view, centred. These are the landscape logical
// dimensions only — the portrait renderer sizes itself from the live screen
// (GetScreenWidth/Height) and does not use BASE_WIDTH/BASE_HEIGHT.
#define BASE_WIDTH  640
#define BASE_HEIGHT 480

// Window setup and teardown (window.c) plus the recorder's capture canvas.
void render_init(void);
void render_cleanup(void);

// Gameplay scene.
void render_frame(const Game* game);
// Gameplay scene with a "paused" overlay on top.
void render_pause(const Game* game);
// Gameplay scene with a "game over" overlay on top.
void render_game_over(const Game* game);
// The family menu (menu.c). gap_before, if >= 0, inserts a blank line before
// that item index. Hit-test its rows with menu_hit_test().
void render_menu(const char* title, const char* const* items, int count,
                 int selected, int gap_before);

// Active renderer selection. Native builds have exactly one renderer, so
// render_use_portrait() is a compile-time constant there (true on Android, false
// on desktop). The web build compiles both and picks at runtime:
// render_set_portrait(true) = portrait touch layout, false = desktop landscape.
void render_set_portrait(bool portrait);
bool render_use_portrait(void);

// Playfield cell size in pixels for the current portrait layout. The gesture
// input layer uses it as the drag distance that moves the piece one column.
int render_portrait_cell(void);

#endif
