/*
 * ETCH CRANK -- by Synth Centric
 * The application glue ETCH DS's main.c provided around its engine: the
 * shared block, the entropy pool, the boot song.  Portable (host tests too).
 */
#include <string.h>
#include "app_glue.h"
#include "audio.h"
#include "seq_core.h"

static shared_t shared_store;
shared_t *SH;
volatile uint16_t ui_entropy;
uint8_t app_card;

/* the pattern after the next bar line (advisory, as in ETCH's main loop);
 * the same as ETCH DS / 3DS main.c */
uint8_t app_next_bar_pattern(void)
{
    uint8_t p = SH->cur;
    if (SH->proj.chain.song) {
        if ((uint8_t)(SH->chain_bar + 1) >= SH->proj.chain.rep[SH->chain_pos]) {
            uint8_t pos = SH->chain_pos, i;
            for (i = 0; i < CHAIN_LEN; i++) {
                pos = (uint8_t)((pos + 1) & (CHAIN_LEN - 1));
                if (SH->proj.chain.pat[pos] < NUM_PATTERNS) { p = SH->proj.chain.pat[pos]; break; }
                if (SH->proj.chain.pat[pos] == 0xFF) pos = CHAIN_LEN - 1;
            }
        }
    } else if (SH->queued != 0xFF) {
        p = SH->queued;
    }
    return p;
}

/* the boot song: ETCH's demo song 1, CORRODE, composed by CORROSION from its
 * seed code 0F1C-9009-5870 (engine/demo.c), with CORRODE's sound and DRIFT */
void shared_init(void)
{
    SH = &shared_store;
    memset(SH, 0, sizeof *SH);
    SH->queued = 0xFF;
    SH->evo_undo_pat = 0xFF;
    demo_load(&SH->proj);
    demo_import_sound(&SH->proj);
    SH->magic = SHARED_MAGIC;
}
