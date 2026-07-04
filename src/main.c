#include "game.h"
#include "render.h"
#include "input.h"
#include "sound.h"
#include "recorder.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

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
#ifndef OB_TOUCH
    // The mp4 recorder is a desktop-only feature (stubbed out on mobile/web), so
    // the toggle would do nothing there — omit it.
    labels[n] = recorder_active() ? "Record: On" : "Record: Off"; actions[n++] = ACT_RECORD;
#endif
    labels[n] = "Exit"; actions[n++] = ACT_EXIT;
    return n;
}

// App state carried across frames. Kept in one struct so the web build can drive
// the loop from an emscripten per-frame callback (browsers can't block).
typedef struct {
    Game* game;
    AppState state;
    int selected;
    bool quit;
} AppCtx;

// One iteration of the game loop. `arg` is an AppCtx* (void* to match the
// emscripten_set_main_loop callback signature).
static void frame_step(void* arg) {
    AppCtx* c = (AppCtx*)arg;

    Input in = input_poll();
    if (in.fullscreen_toggle) render_toggle_fullscreen();

    bool resumable = (c->game != NULL && !game_is_over(c->game));
    const char* labels[MAX_MENU_ITEMS];
    MenuAction actions[MAX_MENU_ITEMS];
    int menu_count = build_menu(resumable, labels, actions);
    if (c->selected >= menu_count) c->selected = 0;

    switch (c->state) {
    case STATE_MENU:
        if (in.escape_pressed) {
            // Escape backs out: resume a game in progress, else quit (native).
            if (resumable) { c->state = STATE_PLAYING; break; }
            c->quit = true; return;
        }
        if (in.menu_up) {
            c->selected = (c->selected + menu_count - 1) % menu_count;
            sound_play(SFX_MENU_MOVE);
        }
        if (in.menu_down) {
            c->selected = (c->selected + 1) % menu_count;
            sound_play(SFX_MENU_MOVE);
        }
        // Touch: a tap directly on a menu item selects it. Keyboard select
        // activates the highlighted item.
        bool do_select = in.select_pressed;
        if (in.touch_tap) {
            int hit = render_menu_hit_test((Vector2){in.tap_x, in.tap_y});
            if (hit >= 0 && hit < menu_count) { c->selected = hit; do_select = true; }
        }
        if (do_select) {
            switch (actions[c->selected]) {
            case ACT_RESUME:
                c->state = STATE_PLAYING;
                sound_play(SFX_MENU_SELECT);
                break;
            case ACT_NEW:
                if (c->game) game_destroy(c->game);
                c->game = game_create();
                if (recorder_active()) { recorder_stop(); recorder_start(NULL); }
                c->state = STATE_PLAYING;
                sound_play(SFX_MENU_SELECT);
                break;
            case ACT_SOUND:
                sound_toggle();
                sound_play(SFX_MENU_SELECT); // audible only once enabled
                break;
            case ACT_RECORD:
                recorder_toggle();
                sound_play(SFX_MENU_SELECT);
                break;
            case ACT_EXIT:
                c->quit = true; return;
            }
        }
        break;

    case STATE_PLAYING:
#ifdef OB_TOUCH
        // Auto-pause when the app is backgrounded (Android) or the browser tab
        // loses focus (web), so the player returns paused, not mid-drop.
        if (!render_window_focused()) {
            c->state = STATE_PAUSED;
            sound_play(SFX_PAUSE);
            break;
        }
#endif
        if (in.escape_pressed) {
            c->state = STATE_MENU; // game stays alive and resumable
            c->selected = 0;
        } else if (in.pause_pressed) {
            c->state = STATE_PAUSED;
            sound_play(SFX_PAUSE);
        } else {
            game_handle_held(c->game, in.left, in.right, in.down);
            if (in.rotate_pressed) game_input(c->game, INPUT_ROTATE);
            if (in.hard_drop_pressed) game_input(c->game, INPUT_HARD_DROP);
            game_update(c->game);
            play_event_sounds(c->game->events);
            if (game_is_over(c->game)) c->state = STATE_GAMEOVER;
        }
        break;

    case STATE_PAUSED:
        if (in.escape_pressed) {
            c->state = STATE_MENU; // game stays alive and resumable
            c->selected = 0;
        } else if (in.any_pressed && !in.fullscreen_toggle) {
            c->state = STATE_PLAYING;
        }
        break;

    case STATE_GAMEOVER:
        if (in.escape_pressed || (in.any_pressed && !in.fullscreen_toggle)) {
            c->state = STATE_MENU;
            c->selected = 0;
        }
        break;
    }

    // Render for the current state.
    if (c->state == STATE_MENU) {
        render_menu("OPENBLOCKS", labels, menu_count, c->selected, menu_count - 1);
    } else if (c->state == STATE_PAUSED) {
        render_pause(c->game);
    } else if (c->state == STATE_GAMEOVER) {
        render_game_over(c->game);
    } else {
        render_frame(c->game);
    }
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

#ifdef PLATFORM_WEB
    // Show the on-screen buttons only on touch devices; desktop browsers drive
    // the game with the keyboard. A coarse primary pointer is the reliable
    // "this is a phone/tablet" signal.
    render_set_touch_controls(emscripten_run_script_int(
        "((window.matchMedia && window.matchMedia('(pointer: coarse)').matches)"
        " || ('ontouchstart' in window) || navigator.maxTouchPoints > 0) ? 1 : 0"));
#endif

    if (cli_record) recorder_start(cli_record_path);

    // Static so the pointer handed to emscripten stays valid after main()'s stack
    // is unwound on the web build (see the PLATFORM_WEB branch below).
    static AppCtx ctx;
    ctx.game = NULL;
    ctx.state = STATE_MENU;
    ctx.selected = 0;
    ctx.quit = false;

#ifdef PLATFORM_WEB
    // Browsers drive the loop via a per-frame callback; with the infinite-loop
    // flag this call does not return, so the native cleanup below never runs on
    // web (the browser tab owns the lifetime).
    emscripten_set_main_loop_arg(frame_step, &ctx, 0, 1);
#else
    while (!render_window_should_close() && !ctx.quit) {
        frame_step(&ctx);
    }
    recorder_stop(); // finalize the .mp4 if recording
    if (ctx.game) game_destroy(ctx.game);
    sound_shutdown();
    render_cleanup();
#endif
    return 0;
}
