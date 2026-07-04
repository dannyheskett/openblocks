#ifndef OPENBLOCKS_RENDER_H
#define OPENBLOCKS_RENDER_H

#include "game.h"
#include <stdbool.h>

// The game renders to a fixed off-screen canvas that present() integer-scales
// and letterboxes into the real window/screen. Desktop uses a 640x480 landscape
// canvas; Android uses a portrait canvas sized for a phone screen.
#ifdef PLATFORM_ANDROID
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

#endif
