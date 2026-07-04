#include "game.h"
#include "render.h"
#include "input.h"
#include "sound.h"
#include "recorder.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    STATE_MENU,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_GAMEOVER,
} AppState;

// Menu actions. The set of items shown depends on whether a game is in
// progress (a resumable game adds "Resume Game").
typedef enum {
    ACT_RESUME,
    ACT_NEW,
    ACT_SOUND,
    ACT_RECORD,
    ACT_EXIT,
} MenuAction;

#define MAX_MENU_ITEMS 5

// Map the events produced during a frame to sound effects.
static void play_event_sounds(unsigned events) {
    if (events & EV_GAMEOVER) { sound_play(SFX_GAMEOVER); return; }
    if (events & EV_QUAD)      sound_play(SFX_QUAD);
    else if (events & EV_LINE) sound_play(SFX_LINE);
    else if (events & EV_LOCK) sound_play(SFX_LOCK);
    if (events & EV_LEVELUP)   sound_play(SFX_LEVELUP);
    if (events & EV_MOVE)      sound_play(SFX_MOVE);
    if (events & EV_ROTATE)    sound_play(SFX_ROTATE);
}

// Build the current menu. Returns the item count; fills labels[] and actions[].
static int build_menu(bool resumable, const char* labels[], MenuAction actions[]) {
    int n = 0;
    if (resumable) { labels[n] = "Resume Game"; actions[n++] = ACT_RESUME; }
    labels[n] = "New Game"; actions[n++] = ACT_NEW;
    labels[n] = sound_is_enabled() ? "Sound: On" : "Sound: Off"; actions[n++] = ACT_SOUND;
#ifndef PLATFORM_ANDROID
    // The mp4 recorder is a desktop-only feature (stubbed out on mobile), so the
    // toggle would do nothing on Android — omit it there.
    labels[n] = recorder_active() ? "Record: On" : "Record: Off"; actions[n++] = ACT_RECORD;
#endif
    labels[n] = "Exit"; actions[n++] = ACT_EXIT;
    return n;
}

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL)); // seed the piece randomizer once at startup

    // CLI: --record [path] starts recording immediately (auto-named if no path).
    bool cli_record = false;
    const char* cli_record_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--record") == 0) {
            cli_record = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') cli_record_path = argv[++i];
        }
    }

    render_init();
    sound_init();

    if (cli_record) recorder_start(cli_record_path);

    Game* game = NULL;
    AppState state = STATE_MENU;
    int selected = 0;

    while (!render_window_should_close()) {
        Input in = input_poll();

        if (in.fullscreen_toggle) render_toggle_fullscreen();

        bool resumable = (game != NULL && !game_is_over(game));
        const char* labels[MAX_MENU_ITEMS];
        MenuAction actions[MAX_MENU_ITEMS];
        int menu_count = build_menu(resumable, labels, actions);
        if (selected >= menu_count) selected = 0;

        switch (state) {
        case STATE_MENU:
            if (in.escape_pressed) {
                // Escape backs out: resume the game if one is in progress,
                // otherwise (top-level menu) exit.
                if (resumable) { state = STATE_PLAYING; break; }
                goto quit;
            }
            if (in.menu_up) {
                selected = (selected + menu_count - 1) % menu_count;
                sound_play(SFX_MENU_MOVE);
            }
            if (in.menu_down) {
                selected = (selected + 1) % menu_count;
                sound_play(SFX_MENU_MOVE);
            }
            // Touch: a tap directly on a menu item selects it (no highlight-then-
            // confirm step). Keyboard select still activates the highlighted item.
            bool do_select = in.select_pressed;
            if (in.touch_tap) {
                int hit = render_menu_hit_test((Vector2){in.tap_x, in.tap_y});
                if (hit >= 0 && hit < menu_count) { selected = hit; do_select = true; }
            }
            if (do_select) {
                switch (actions[selected]) {
                case ACT_RESUME:
                    state = STATE_PLAYING;
                    sound_play(SFX_MENU_SELECT);
                    break;
                case ACT_NEW:
                    if (game) game_destroy(game);
                    game = game_create();
                    // Each game records to its own file: finalize the current
                    // recording (if any) and begin a fresh one.
                    if (recorder_active()) {
                        recorder_stop();
                        recorder_start(NULL);
                    }
                    state = STATE_PLAYING;
                    sound_play(SFX_MENU_SELECT);
                    break;
                case ACT_SOUND:
                    sound_toggle();
                    sound_play(SFX_MENU_SELECT); // audible only once enabled
                    break;
                case ACT_RECORD:
                    recorder_toggle(); // start (auto-named file) or finalize
                    sound_play(SFX_MENU_SELECT);
                    break;
                case ACT_EXIT:
                    goto quit;
                }
            }
            break;

        case STATE_PLAYING:
#ifdef PLATFORM_ANDROID
            // Auto-pause when the app is sent to the background so the player
            // returns to a paused game instead of mid-drop.
            if (!render_window_focused()) {
                state = STATE_PAUSED;
                sound_play(SFX_PAUSE);
                break;
            }
#endif
            if (in.escape_pressed) {
                state = STATE_MENU; // game stays alive and resumable
                selected = 0;
            } else if (in.pause_pressed) {
                state = STATE_PAUSED;
                sound_play(SFX_PAUSE);
            } else {
                game_handle_held(game, in.left, in.right, in.down);
                if (in.rotate_pressed) game_input(game, INPUT_ROTATE);
                if (in.hard_drop_pressed) game_input(game, INPUT_HARD_DROP);
                game_update(game);
                play_event_sounds(game->events);
                if (game_is_over(game)) state = STATE_GAMEOVER;
            }
            break;

        case STATE_PAUSED:
            if (in.escape_pressed) {
                state = STATE_MENU; // game stays alive and resumable
                selected = 0;
            } else if (in.any_pressed && !in.fullscreen_toggle) {
                state = STATE_PLAYING;
            }
            break;

        case STATE_GAMEOVER:
            if (in.escape_pressed || (in.any_pressed && !in.fullscreen_toggle)) {
                state = STATE_MENU;
                selected = 0;
            }
            break;
        }

        // Render for the current state.
        if (state == STATE_MENU) {
            render_menu("OPENBLOCKS", labels, menu_count, selected, menu_count - 1);
        } else if (state == STATE_PAUSED) {
            render_pause(game);
        } else if (state == STATE_GAMEOVER) {
            render_game_over(game);
        } else {
            render_frame(game);
        }
    }

quit:
    recorder_stop(); // finalize the .mp4 if recording
    if (game) game_destroy(game);
    sound_shutdown();
    render_cleanup();
    return 0;
}
