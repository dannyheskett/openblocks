#include "sound.h"
#include "audio.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

// All effects are short, generated PCM clips built from three classic 8-bit
// building blocks: square tones (with selectable duty cycle), pitch sweeps, and
// white noise. A simple linear decay envelope on each clip gives the percussive,
// plucky character of vintage sound chips. Playback goes through the audio.h
// backend (raylib on desktop/web/android, AVAudioEngine on iOS); this file is
// backend-agnostic and does the synthesis.

#define SAMPLE_RATE 44100

static bool        enabled = false;        // off by default
static AudioHandle effects[SFX_COUNT];

static AudioHandle load_samples(int16_t* samples, int count) {
    AudioHandle h = audio_load(samples, count, SAMPLE_RATE);
    free(samples);
    return h;
}

// A square tone of fixed frequency with a linear amplitude decay.
static AudioHandle make_tone(float freq, float dur, float duty, float vol) {
    int n = (int)(dur * SAMPLE_RATE);
    int16_t* buf = malloc(sizeof(int16_t) * n);
    if (!buf) return -1;
    for (int i = 0; i < n; i++) {
        float phase = freq * ((float)i / SAMPLE_RATE);
        phase -= (int)phase;
        float env = 1.0f - (float)i / n;
        float v = (phase < duty ? 1.0f : -1.0f) * vol * env;
        buf[i] = (int16_t)(v * 32767.0f);
    }
    return load_samples(buf, n);
}

// A square tone whose pitch glides from f0 to f1 over its duration.
static AudioHandle make_sweep(float f0, float f1, float dur, float duty, float vol) {
    int n = (int)(dur * SAMPLE_RATE);
    int16_t* buf = malloc(sizeof(int16_t) * n);
    if (!buf) return -1;
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float freq = f0 + (f1 - f0) * ((float)i / n);
        phase += freq / SAMPLE_RATE;
        float p = phase - (int)phase;
        float env = 1.0f - (float)i / n;
        float v = (p < duty ? 1.0f : -1.0f) * vol * env;
        buf[i] = (int16_t)(v * 32767.0f);
    }
    return load_samples(buf, n);
}

// A burst of white noise with a linear decay — used for the "thunk" of a lock.
static AudioHandle make_noise(float dur, float vol) {
    int n = (int)(dur * SAMPLE_RATE);
    int16_t* buf = malloc(sizeof(int16_t) * n);
    if (!buf) return -1;
    float hold = 0.0f;
    for (int i = 0; i < n; i++) {
        if (i % 8 == 0) { // sample-and-hold gives a grittier, lower-rate hiss
            hold = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        }
        float env = 1.0f - (float)i / n;
        buf[i] = (int16_t)(hold * vol * env * 32767.0f);
    }
    return load_samples(buf, n);
}

// A quick run of square tones — a tiny arpeggio for jingles.
static AudioHandle make_arp(const float* freqs, int count, float per_note, float duty, float vol) {
    int note_n = (int)(per_note * SAMPLE_RATE);
    int n = note_n * count;
    int16_t* buf = malloc(sizeof(int16_t) * n);
    if (!buf) return -1;
    for (int j = 0; j < count; j++) {
        for (int i = 0; i < note_n; i++) {
            float phase = freqs[j] * ((float)i / SAMPLE_RATE);
            phase -= (int)phase;
            float env = 1.0f - (float)i / note_n;
            float v = (phase < duty ? 1.0f : -1.0f) * vol * env;
            buf[j * note_n + i] = (int16_t)(v * 32767.0f);
        }
    }
    return load_samples(buf, n);
}

void sound_init(void) {
    audio_init();
    if (!audio_ready()) return;

    static const float quad_arp[]   = {523.25f, 659.25f, 783.99f, 1046.50f}; // C E G C
    static const float select_arp[] = {659.25f, 987.77f};                    // E B
    static const float levelup_arp[]= {659.25f, 880.00f, 1318.51f};          // E A E
    static const float over_arp[]   = {440.00f, 349.23f, 261.63f, 196.00f};  // A F C G (descending)

    effects[SFX_MOVE]        = make_tone(220.0f, 0.025f, 0.5f, 0.18f);
    effects[SFX_ROTATE]      = make_tone(660.0f, 0.045f, 0.25f, 0.22f);
    effects[SFX_LOCK]        = make_noise(0.06f, 0.25f);
    effects[SFX_LINE]        = make_sweep(440.0f, 1200.0f, 0.22f, 0.5f, 0.28f);
    effects[SFX_QUAD]        = make_arp(quad_arp, 4, 0.06f, 0.5f, 0.30f);
    effects[SFX_LEVELUP]     = make_arp(levelup_arp, 3, 0.06f, 0.5f, 0.28f);
    effects[SFX_GAMEOVER]    = make_arp(over_arp, 4, 0.16f, 0.5f, 0.30f);
    effects[SFX_MENU_MOVE]   = make_tone(440.0f, 0.03f, 0.5f, 0.18f);
    effects[SFX_MENU_SELECT] = make_arp(select_arp, 2, 0.05f, 0.5f, 0.26f);
    effects[SFX_PAUSE]       = make_tone(330.0f, 0.09f, 0.5f, 0.22f);
}

void sound_shutdown(void) {
    if (!audio_ready()) return;
    for (int i = 0; i < SFX_COUNT; i++) {
        audio_unload(effects[i]);
    }
    audio_shutdown();
}

bool sound_is_enabled(void) { return enabled; }
void sound_toggle(void) { enabled = !enabled; }

void sound_play(SfxId id) {
    if (!enabled || !audio_ready() || id < 0 || id >= SFX_COUNT) return;
    audio_play(effects[id]);
}
