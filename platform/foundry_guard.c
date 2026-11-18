/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * Re-synthesising a drum while the song plays, safely on one core.
 *
 * ETCH DS's foundry_rebuild() renders the new sound, then swaps the sample
 * pointer and its length in with interrupts off (on the DS) and frees the old
 * buffer.  On the Playdate the audio callback can interrupt the update loop
 * anywhere, and a trigger landing between the pointer and the length being
 * written could read past the end of a shorter sample.  So the triggers are
 * guarded instead: a link-time wrap of foundry_trigger (-Wl,--wrap=...)
 * holds back any hit that arrives while a rebuild is in progress and plays it
 * the moment the rebuild is done (the next trigger or the next block).  Voices
 * already playing keep their own copy of the pointer and length, and ETCH DS
 * silences any voice on the old buffer before freeing it.  The delay is the
 * length of the rebuild, and only while a drum is being edited.
 */
#include "audio.h"
#include "plat.h"

static volatile uint8_t rebuilding;
static volatile uint8_t held_lanes, held_kits;

/* the kit bits that belong to these lanes (2 bits each for ETCH's 4 lanes) */
static inline uint8_t kit_mask_for(uint8_t lanes)
{
    uint8_t m = 0, l;
    for (l = 0; l < 4; l++) if (lanes & (1u << l)) m |= (uint8_t)(3u << (l * 2));
    return m;
}

void __real_foundry_trigger(uint8_t lanes, uint8_t kits);
void __wrap_foundry_trigger(uint8_t lanes, uint8_t kits)
{
    if (rebuilding) {                       /* audio callback, mid-rebuild: hold it */
        held_kits = (uint8_t)((held_kits & ~kit_mask_for(lanes)) | (kits & kit_mask_for(lanes)));
        held_lanes |= lanes;
        return;
    }
    if (held_lanes) {
        uint8_t l = held_lanes, k = held_kits;
        held_lanes = 0;
        __real_foundry_trigger(l, k);
    }
    if (lanes) __real_foundry_trigger(lanes, kits);
}

/* once per block, from the pump: a held hit plays as soon as it may */
void foundry_guard_flush(void)
{
    if (!rebuilding && held_lanes) __wrap_foundry_trigger(0, 0);
}

/* ETCH DS's foundry_build (boot, project loads in edit.c) re-renders every
 * sound: guarded the same way */
void __real_foundry_build(void);
void __wrap_foundry_build(void)
{
    rebuilding = 1;
    __sync_synchronize();
    __real_foundry_build();
    __sync_synchronize();
    rebuilding = 0;
}

void foundry_guard_rebuild(uint32_t lane, uint32_t kit)
{
    rebuilding = 1;
    __sync_synchronize();
    foundry_rebuild(lane, kit);
    __sync_synchronize();
    rebuilding = 0;
}
