/*
 * ETCH DS -- by Synth Centric
 * Sequencer core (ARM7, and host for the unit tests).  See seq_core.c.
 */
#ifndef ETCH_DS_SEQ_CORE_H
#define ETCH_DS_SEQ_CORE_H
#include "ipc.h"

void    seq_init(shared_t *sh);
void    seq_render_block(uint32_t blk);   /* fill sh->ev[blk % AUDIO_NBLK]   */
void    seq_command(uint32_t v);          /* CMD(...) from the ARM9           */
uint8_t seq_next_bar_pattern(void);

#endif
