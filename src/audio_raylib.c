// raylib audio backend (desktop / web / android): the same InitAudioDevice /
// LoadSoundFromWave / PlaySound calls sound.c used before the audio abstraction,
// so behaviour is unchanged. iOS uses audio_ios.mm instead.
#include "audio.h"
#include <raylib.h>

#define MAX_SOUNDS 32

static Sound s_sounds[MAX_SOUNDS];
static int   s_count = 0;
static bool  s_ready = false;

void audio_init(void) {
    InitAudioDevice();
    s_ready = IsAudioDeviceReady();
    s_count = 0;
}

void audio_shutdown(void) {
    if (!s_ready) return;
    CloseAudioDevice();
    s_ready = false;
    s_count = 0;
}

bool audio_ready(void) { return s_ready; }

AudioHandle audio_load(const int16_t* samples, int frame_count, int sample_rate) {
    if (!s_ready || s_count >= MAX_SOUNDS || frame_count <= 0) return -1;
    Wave wave = {
        .frameCount = (unsigned int)frame_count,
        .sampleRate = (unsigned int)sample_rate,
        .sampleSize = 16,
        .channels   = 1,
        .data       = (void*)samples,
    };
    s_sounds[s_count] = LoadSoundFromWave(wave);
    return s_count++;
}

void audio_play(AudioHandle handle) {
    if (s_ready && handle >= 0 && handle < s_count) PlaySound(s_sounds[handle]);
}

void audio_unload(AudioHandle handle) {
    if (s_ready && handle >= 0 && handle < s_count) UnloadSound(s_sounds[handle]);
}
