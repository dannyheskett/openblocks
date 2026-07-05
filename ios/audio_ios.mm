// iOS audio backend: AVAudioEngine with one player node per effect. sound.c
// synthesizes the same short mono int16 PCM clips; here each becomes an
// AVAudioPCMBuffer (float32) played through the engine's main mixer.
#import <AVFoundation/AVFoundation.h>

#include "audio.h"

#define MAX_SOUNDS 32

static AVAudioEngine*     s_engine;
static AVAudioPCMBuffer*  s_buffers[MAX_SOUNDS];
static AVAudioPlayerNode* s_players[MAX_SOUNDS];
static int  s_count = 0;
static bool s_ready = false;

void audio_init(void) {
    @autoreleasepool {
        NSError* err = nil;
        AVAudioSession* session = [AVAudioSession sharedInstance];
        // Ambient: mixes with other audio and honours the silent switch.
        [session setCategory:AVAudioSessionCategoryAmbient error:&err];
        [session setActive:YES error:&err];

        s_engine = [[AVAudioEngine alloc] init];
        (void)s_engine.mainMixerNode; // instantiate the mixer before starting
        s_count = 0;
        s_ready = [s_engine startAndReturnError:&err] ? true : false;
    }
}

void audio_shutdown(void) {
    if (!s_ready) return;
    [s_engine stop];
    s_ready = false;
}

bool audio_ready(void) { return s_ready; }

AudioHandle audio_load(const int16_t* samples, int frame_count, int sample_rate) {
    if (!s_ready || s_count >= MAX_SOUNDS || frame_count <= 0) return -1;
    @autoreleasepool {
        AVAudioFormat* fmt =
            [[AVAudioFormat alloc] initStandardFormatWithSampleRate:sample_rate channels:1];
        AVAudioPCMBuffer* buf =
            [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:(AVAudioFrameCount)frame_count];
        buf.frameLength = (AVAudioFrameCount)frame_count;
        float* dst = buf.floatChannelData[0];
        for (int i = 0; i < frame_count; i++) dst[i] = samples[i] / 32768.0f;

        AVAudioPlayerNode* player = [[AVAudioPlayerNode alloc] init];
        [s_engine attachNode:player];
        [s_engine connect:player to:s_engine.mainMixerNode format:fmt];

        s_buffers[s_count] = buf;
        s_players[s_count] = player;
        return s_count++;
    }
}

void audio_play(AudioHandle handle) {
    if (!s_ready || handle < 0 || handle >= s_count) return;
    AVAudioPlayerNode* p = s_players[handle];
    // Interrupts a still-playing instance so rapid re-triggers restart cleanly.
    [p scheduleBuffer:s_buffers[handle]
                atTime:nil
               options:AVAudioPlayerNodeBufferInterrupts
     completionHandler:nil];
    if (!p.isPlaying) [p play];
}

void audio_unload(AudioHandle handle) {
    (void)handle; // nodes/buffers released when the engine tears down at shutdown
}
