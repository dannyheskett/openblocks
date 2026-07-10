#include "game.h"
#include "render.h"
#include "input.h"
#include "sound.h"
#include "recorder.h"
#include "app.h"
#include "tick.h"
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

// Upper bound on labels[]/actions[]: one slot per MenuAction (5). Each action
// appears at most once, so build_menu can never overflow.
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
#if defined(PLATFORM_WEB)
    // A browser tab can't be closed from code, so no Exit on web. (The renderer —
    // portrait touch vs desktop landscape — is auto-detected by pointer type.)
#elif !defined(PLATFORM_IOS) && !defined(PLATFORM_ANDROID)
    // Mobile apps don't self-terminate (the OS owns the lifecycle: home gesture /
    // back button on Android, Apple guidelines on iOS), so no Exit on either.
    labels[n] = "Exit"; actions[n++] = ACT_EXIT;
#endif
    return n;
}

#ifdef OB_SIMSTATS
// Real-device validation instrumentation (SIMSTATS=1 builds; compiles on
// desktop too for local smoke tests). Once per second of continuous play, log
// how many frames were rendered vs how many fixed 60 Hz sim steps ran, so
// scripts/devicefarm_run.py can assert from the device log that the
// fixed-timestep accumulator holds ~60 steps/s at whatever refresh rate the
// display actually delivers (the frames count is the evidence of that rate).
// Any gap between calls (menu, pause, a stall past the spiral clamp) starts a
// fresh window, so every logged line covers uninterrupted play.
#if defined(PLATFORM_ANDROID)
#include <android/log.h>
#define SIMSTATS_LOG(...) __android_log_print(ANDROID_LOG_INFO, "openblocks", __VA_ARGS__)
#else
#include <stdio.h>
#define SIMSTATS_LOG(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#endif

static void simstats_count(double now, int steps) {
    static double win_start, last_call;
    static int frames, sim_steps;
    if (last_call == 0.0 || now - last_call > 0.25) { // gap: not continuous play
        win_start = now;
        frames = 0;
        sim_steps = 0;
    }
    last_call = now;
    frames++;
    sim_steps += steps;
    double span = now - win_start;
    if (span >= 1.0) {
        SIMSTATS_LOG("SIMSTATS window=%.3f frames=%d steps=%d", span, frames, sim_steps);
        win_start = now;
        frames = 0;
        sim_steps = 0;
    }
}
#endif // OB_SIMSTATS

// App state carried across frames. Kept in one struct so the web build can drive
// the loop from an emscripten per-frame callback (browsers can't block).
typedef struct {
    Game* game;
    AppState state;
    int selected;
    bool quit;
    SimClock clock;   // fixed-timestep accumulator (only advanced while playing)
    double prev_time; // GetTime() at the previous frame; 0 before the first frame
} AppCtx;

// One iteration of the game loop. `arg` is an AppCtx* (void* to match the
// emscripten_set_main_loop callback signature).
static void frame_step(void* arg) {
    AppCtx* c = (AppCtx*)arg;

    // Real seconds since the previous frame, feeding the fixed-timestep
    // accumulator so the simulation runs at 60 Hz on any display refresh. The
    // first frame (prev_time == 0) is treated as exactly one step. The clock only
    // banks time while actually playing; any other state drains it so a pause or
    // menu can't hoard a burst of catch-up steps for the moment play resumes.
    double now = GetTime();
    double dt = (c->prev_time > 0.0) ? now - c->prev_time : SIM_DT;
    c->prev_time = now;
    if (c->state != STATE_PLAYING) sim_clock_reset(&c->clock);

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
            // Run the number of fixed 60 Hz steps that have elapsed. game_handle_held
            // clears the event flags each step, so OR them into frame_events to keep
            // every sound when a frame runs more than one step.
            unsigned frame_events = 0;
            int steps = sim_clock_advance(&c->clock, dt);
#ifdef OB_SIMSTATS
            simstats_count(now, steps);
#endif
            bool applied_edge = false;
            for (int s = 0; s < steps; s++) {
                game_handle_held(c->game, in.left, in.right, in.down);
                // Discrete actions apply exactly once, on the first step, preserving
                // the per-frame order held-move -> rotate -> hard-drop -> gravity.
                if (!applied_edge) {
                    if (in.rotate_pressed)    game_input(c->game, INPUT_ROTATE);
                    if (in.hard_drop_pressed) game_input(c->game, INPUT_HARD_DROP);
                    applied_edge = true;
                }
                game_update(c->game);
                frame_events |= c->game->events;
            }
            // A frame that ran no step (a display faster than 60 Hz) still applies
            // the press immediately, so input is never dropped; gravity catches up
            // on the next stepped frame.
            if (!applied_edge) {
                if (in.rotate_pressed)    { game_input(c->game, INPUT_ROTATE);    frame_events |= c->game->events; }
                if (in.hard_drop_pressed) { game_input(c->game, INPUT_HARD_DROP); frame_events |= c->game->events; }
            }
            play_event_sounds(frame_events);
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
#ifdef OB_AUTOPLAY
        // Validation builds restart immediately so an unattended device run
        // measures play for its whole duration (gravity plays the game solo).
        c->game = game_create();
        c->state = STATE_PLAYING;
        break;
#else
        if (in.escape_pressed || (in.any_pressed && !in.fullscreen_toggle)) {
            c->state = STATE_MENU;
            c->selected = 0;
        }
        break;
#endif
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

#if defined(PLATFORM_IOS)

// iOS: UIKit provides main() and the run loop, so the normal main() below is
// compiled out. The app shell (ios_main.mm) sets up the Metal layer, calls
// ob_app_init() once, then ob_app_frame() from a CADisplayLink each frame.
static AppCtx ios_ctx;

void ob_app_init(void) {
    srand((unsigned int)time(NULL));
    render_init();   // no-op on iOS (UIKit owns the window)
    sound_init();    // silent stub on iOS
    ios_ctx.game = NULL;
    ios_ctx.state = STATE_MENU;
    ios_ctx.selected = 0;
    ios_ctx.quit = false;
    sim_clock_reset(&ios_ctx.clock);
    ios_ctx.prev_time = 0.0;
#ifdef OB_AUTOPLAY
    ios_ctx.game = game_create();
    ios_ctx.state = STATE_PLAYING;
#endif
}

void ob_app_frame(void) { frame_step(&ios_ctx); }

#else

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
    // Pick the renderer by the primary pointer: coarse (phone/tablet) -> portrait
    // touch layout; fine (desktop / 2-in-1 laptop) -> desktop landscape layout +
    // keyboard, matching the native desktop app. Only pointer:coarse is used —
    // the maxTouchPoints/ontouchstart backstops wrongly flipped touchscreen
    // laptops to the touch layout.
    render_set_portrait(emscripten_run_script_int(
        "(window.matchMedia && window.matchMedia('(pointer: coarse)').matches) ? 1 : 0"));
#endif

    if (cli_record) recorder_start(cli_record_path);

    // Static so the pointer handed to emscripten stays valid after main()'s stack
    // is unwound on the web build (see the PLATFORM_WEB branch below).
    static AppCtx ctx;
    ctx.game = NULL;
    ctx.state = STATE_MENU;
    ctx.selected = 0;
    ctx.quit = false;
    sim_clock_reset(&ctx.clock);
    ctx.prev_time = 0.0;
#ifdef OB_AUTOPLAY
    // Validation builds skip the menu and start playing at boot (see SIMSTATS
    // in the Makefile); gravity alone keeps the simulation running.
    ctx.game = game_create();
    ctx.state = STATE_PLAYING;
#endif

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

#endif // PLATFORM_IOS (main() is compiled out on iOS)
