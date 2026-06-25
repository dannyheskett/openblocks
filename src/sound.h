#ifndef OPENBLOCKS_SOUND_H
#define OPENBLOCKS_SOUND_H

#include <stdbool.h>

// Procedural retro sound effects. Every effect is synthesized at startup from
// simple square / swept / noise waveforms (no audio files), giving the crunchy
// chiptune feel of classic 8-bit hardware. Sound is disabled by default.

typedef enum {
    SFX_MOVE = 0,    // piece shifted left/right
    SFX_ROTATE,      // piece rotated
    SFX_LOCK,        // piece landed and locked
    SFX_LINE,        // 1-3 lines cleared
    SFX_QUAD,        // 4 lines cleared at once
    SFX_LEVELUP,     // advanced to the next level
    SFX_GAMEOVER,    // board topped out
    SFX_MENU_MOVE,   // menu cursor moved
    SFX_MENU_SELECT, // menu item chosen
    SFX_PAUSE,       // game paused
    SFX_COUNT
} SfxId;

void sound_init(void);       // open the audio device and synthesize all effects
void sound_shutdown(void);   // free effects and close the audio device
bool sound_is_enabled(void);
void sound_toggle(void);
void sound_play(SfxId id);   // no-op when sound is disabled

#endif
