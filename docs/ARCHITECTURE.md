# ETCH CRANK — architecture

ETCH CRANK — by Synth Centric — for Playdate

## Decisions made with the project owner

**Before milestone 1:**

* **Render rate:** ETCH DS's engine runs unmodified at its own rate, 16 756 991 / 512 = **32 728.498 Hz**. A fixed-point polyphase resampler converts it to the Playdate's 44 100 Hz.
* **Threading:** the clock, the voices and FX run inside the SDK's audio callback. The update loop does input, CORROSION and drawing.

**Before milestone 2:**

* **Saves:** the project slots stay exactly in ETCH DS's format. Playdate-only data goes in a sidecar file.
* **Seed codes:** the series' formats only. 12 digits (CORROSION's), plus an optional 4th group, the expansion seed. No new digits.
* **The ETCH DS layers** (RUST, extra lanes, the answering generator, ratchets, sound evolution) are built here, in `layers/`, written to move into ETCH-DS unchanged.
* **Button map:** B + A stays "delete", so evolve and undo live on the pads. The screen row wraps.

## 1. Repository layout

```
corrosion/   CORROSION from ETCH (Etch---GB @ 62ac4c6), byte-identical, checksummed,
             with ETCH's own tests/test_generator.c
engine/      ETCH DS's sequencer and sound engine (ETCH-DS @ 8294b6d), unmodified,
             checksummed, with ETCH DS's own host tests
layers/      the ETCH DS layers: RUST, the answering generator, ratchets, lengths,
             sound evolution (portable C, no Playdate calls)
platform/    the Playdate: boot and callback (main.c), the audio pump and resampler,
             the crank, the 1-bit renderer and ETCH DS's fonts, the six screens,
             the Playdate-only layer (cpl.c), projects and saves, ETCH's demos
tests/       host tests; tests/arm: a Linux runtime for the qemu runs
tools/       sync and verify scripts, ARM parity, CPU cost, table generators,
             the Simulator drivers
docs/        this file, controls, the manual, reports, ideas, screenshots
release/     ETCH_CRANK.pdx.zip
```

**`corrosion/` and `engine/` are never edited.** `make` checks both against their checksums and refuses to build if either has changed. Fixes go to ETCH (for CORROSION) or ETCH-DS (for the engine) first, and come back with `tools/sync_*.sh`.

## 2. How the Playdate code reaches into ETCH DS without editing it

Everything goes through ETCH DS's own API plus nine link-time wraps (`-Wl,--wrap=...`). Each wrap is an exact pass-through while its feature is off, and the consistency tests prove it.

| Wrapped | By | What for |
|---|---|---|
| `acid_render` | `platform/crank_audio.c` | The crank: per control tick, glide ACID's cutoff and resonance to the crank's target (plus the motion lane); ACID then interpolates per sample |
| `acid_step` | `layers/layers.c` | The layers step on ETCH's exact 16th: RUST, METAL, NOISE, each on its own length |
| `foundry_render` | `layers/layers.c` | Per control tick: ratchet repeats, RUST's gate, and RUST into the bus |
| `foundry_trigger` | `platform/foundry_guard.c` | Hold triggers back during a drum re-synthesis (see §6); report hits to the ratchets |
| `foundry_build` | `platform/foundry_guard.c` | The same guard for whole-kit re-synthesis (project loads) |
| `fx_limit` | `platform/cpl.c` | SPEAKER: mono plus a low-mid lift before ETCH DS's limiter |
| `corr_mutate` | `platform/cpl.c` | CRANK BITE: CORROSION's own intensity argument, taken from the filter's openness |
| `fopen` … `ftell`, `fflush` | `platform/pdfile.c` | stdio on the Playdate's file API, for ETCH DS's `storage.c` and the sidecar |

## 3. One core, two contexts

```
 audio callback (SDK; preempts the update loop)        update loop (50 Hz)
 pump_pull(len at 44.1 kHz)                           buttons, crank -> ui.c (the grammar)
   per engine block (128 samples):                    the screen (changed rows only)
     drain the command ring (SPSC)                    cpl_frame: lanes, mods, TILT,
     seq_render_block  (ETCH's clock + rules)           SPEAKER, scenes
     audio_render_block (ACID -> FOUNDRY -> FX)       layers_frame: the answering
       acid_render  (wrap: the crank, the lane)         generator, sound evolution
       acid_step    (wrap: the layers' step)          evo_frame: CORROSION's next mutation
       foundry_render (wrap: ratchets, RUST)          (ETCH DS's evolve.c)
       fx_limit     (wrap: SPEAKER)                   app_fill_frame, drum re-synthesis
   mirrored ring -> 16-tap resampler -> L / R
```

* **The same guarantees as the DS.** On ETCH DS the ARM9's render interrupt preempted its main loop. Here the audio callback preempts the update loop, so a block render always completes before the update loop runs again.
* **Commands** (`app_send`, ETCH DS's name) go through a 256-entry single-producer, single-consumer ring, drained before each block is sequenced: the same point in the block stream where a command reached the DS's ARM7. Codes from `0xC0` up belong to the platform: the crank's targets, the quarter-step length, and a reset.
* **CORROSION's hand-off** is ETCH DS's, with ETCH II's edit-epoch guard. The update loop prepares the next mutation; the sequencer commits it at step 16's gate point, so it lands on the downbeat.
* **The audio callback** never allocates, draws, locks, waits or touches the filesystem.

## 4. Clock and rate

* **The clock.** `seq_core` advances ETCH's phase accumulator per 32-sample sub-block, in engine samples, with ETCH II's swing fix. So tempo, swing and the gate points are exactly ETCH DS's.
* **The resampler:**
  * 16-tap Kaiser-windowed sinc (β 7), passband to 15.2 kHz, 128 phases
  * linear interpolation between the outputs of neighbouring phases
  * two taps per SMLAD on the M7; the host's plain C gives identical output
  * a mirrored ring, so each tap window is contiguous
  * its step is exact to 4 × 10⁻¹¹, so the tempo is ETCH DS's in seconds too
* **Headroom.** The output gain is **0.89 (−1 dB)**. ETCH DS's limiter holds the engine at 0.85, but band-limited interpolation of its flat tops and of the crusher's steps overshoots: 1.048 over the 16-bar consistency song. With the gain the peak is 0.933.

## 5. The crank

* **Update loop** (`crank.c`):
  * relative: it accumulates `getCrankChange()`
  * 540° is 127 CUTOFF steps, with the remainder carried; hard stops at both ends absorb any excess
  * B held: the crank turns resonance
  * dock: a linear glide home over one beat; B + dock keeps the live values
  * the live value is stored + offset; ETCH's living-song drift stays separate inside ACID
  * the mod matrix's cutoff and resonance offsets are added to the target
* **Audio callback** (`crank_audio.c`): once per control tick, a one-pole glide (τ 4 ms) toward the target plus the motion lane, written into ACID's cutoff and resonance. ACID interpolates the ladder coefficient per sample.

## 6. Drum re-synthesis on one core

ETCH DS's `foundry_rebuild` swaps a sample pointer and its length with interrupts off. Here the callback could interrupt between the two writes, and a trigger landing in that gap could read past a shorter sample.

`foundry_guard.c` wraps `foundry_trigger` so that hits arriving during a rebuild are held. The pump flushes them at the next block. Voices already playing keep their own pointer and length, and ETCH DS silences any voice still on the old buffer before freeing it.

## 7. The interface

* **The renderer** (`gfx1.c`): one 400 × 240 buffer in the Playdate's frame layout, using ETCH DS's SMALL 3×5 and MAIN 5×7 fonts (`font_ui.c`, copied unmodified). The series' colour roles map onto 1 bit: black, white, and 75 / 50 / 25 % dither.
* **Redrawing:** regions redraw only when what they show changes (input, step, pattern, commits, a 4 Hz refresh). On the device, only changed rows are copied into the frame.
* **The framework** (`ui.c`) resolves ETCH's grammar once:
  * A tap versus A held + D-pad, with key repeat
  * held B as SELECT, and B + A as delete
  * B tap versus B chord
  * the crank's routing: filter, resonance with B, or A + crank riding the value under the cursor
* **What it draws:** the status band (cutoff arc and scope, the readout strip, the screen row), the hint line, and a list widget shared by SYNTH, GEN and SONG.
* **The screens:** `scr_live.c`, `scr_track.c`, `scr_synth.c`, `scr_gen.c`, `scr_song.c`, `scr_file.c`. All pattern edits go through ETCH DS's `edit.c` (`app_set_bass`, `app_toggle_drum`, `app_replace`…), so the audio side stays the only writer of pattern memory.

## 8. The Playdate-only layer (`cpl.c`)

* **Motion lanes:** 64 points a bar per pattern. B + UP arms; recording starts at the next bar line and covers one pass. The audio side reads the lane per control tick. CORRODE shifts, thins and re-times a lane; DRIFT nudges it; both stay bounded by the recording (the anchor).
* **Scenes:** four snapshots of the sound. Morphing interpolates the continuous values and switches the discrete ones half way.
* **Mute groups:** two groups over ETCH DS's mute byte.
* **Mod matrix:** four rows. Sources: CRANK, TILT, LFO (ETCH's rates and shapes), ENV (ACID's filter envelope), MIC (peak level, only while a row uses it). Eleven destinations. It runs at frame rate on the *base* sound, so with no rows active the engine gets exactly the edited values.
* **SPEAKER, TILT, CRANK BITE:** see §2 and CONTROLS.md.
* **The sidecar** (`etch_crank_ext.sav`): 16 slots of 16 KB, header `ETCK`, ETCH's Fletcher-16, ETCH's write order. It holds the 4th code group, lanes and anchors, scenes, mute groups, the mod matrix, and the layers' data.

## 9. The ETCH DS layers (`layers/`)

* **RUST:** per note, two oscillators on ACID's band-limited wavetables (SAW, SQUARE, HOLLOW, BITE; "industrial" through HOLLOW, BITE, sync and ring), an interval and a detune for B, hard sync, ring mod. MONO, or PARA 3 (three notes through one filter and one envelope). A 2-pole resonant low-pass on ACID's coefficient tables; ACID's decay table. Integer arithmetic only.
* **METAL and NOISE** are FOUNDRY's lanes 4 and 5, which ETCH DS synthesises but ETCH's patterns never write. Their mutes are ETCH DS's mute bits 5 and 6; RUST's is bit 7.
* **Ratchets:** 1–4 hits per step on ETCH's four lanes. Only hits that actually fired are repeated, so ETCH's chance and condition rules still decide the first hit.
* **Lengths:** RUST, METAL and NOISE each run 1–16 steps against ETCH's bar.
* **The answering generator** writes RUST, METAL, NOISE and the ratchets as answers to CORROSION's bass and drums.
  * It reads only CORROSION's output and its pure helpers, and uses its own random stream, seeded by the 4th code group, the pattern and its generation.
  * So the same 16-digit code gives the same layers everywhere, and CORROSION's own output never changes.
  * After an evolution, it re-answers the playing and the next pattern.
  * A hand-edited pattern is locked from it.
* **The layers play only with a 4th group (or a hand edit).** So every 12-digit code, ETCH's demos and the boot song sound exactly as on ETCH DS (tested).
* **Sound evolution** is off by default. After each commit, every free knob takes a bounded step around its anchor: DRIFT ±2 within ±8, CORRODE ±5 within ±20. A hand edit re-anchors the knob, and locked knobs never move.

## 10. Saves (`proj.c`, `pdfile.c`)

* **The project** is ETCH's `project_t` plus ETCH DS's sound settings, written by ETCH DS's own `storage.c` to `etch_crank.sav`: ETCH DS's format, 16 slots, magic last.
* **The file layer** (`pdfile.c`) loads the file into RAM on open. On close it writes `NAME.tmp` and renames it over `NAME`, so each phase of ETCH's order reaches flash as one atomic file replacement. A crash between unlink and rename is recovered on the next open.
* **Imports:**
  * ETCH DS saves (`etch_ds.sav`, copied into the data folder) load as they are.
  * ETCH Game Boy saves (`etch_gb.sav`) import through ETCH DS's importer.
* **Loading** follows ETCH DS's `apply_project`: stop, let the audio go quiet, swap the project in, re-synthesise the drums, new epochs, restart. Then the crank re-takes the stored values and the layers settle.

## 11. Proofs

| Claim | How |
|---|---|
| CORROSION is ETCH's, byte for byte | `corrosion/SHA256SUMS` on every `make`; `tools/verify_corrosion.sh` |
| ETCH's 44 tests pass against it | `corrosion/tests/test_generator.c` |
| The engine is ETCH DS's, and its tests pass | `engine/SHA256SUMS`; ETCH DS's 231 sequencer and 81 DSP tests |
| The port sounds exactly like ETCH DS | `tests/consistency.c`: 16 bars, bit-identical, with every wrap linked |
| …with the device's compiler and flags | `tools/arm_parity.sh` (qemu-arm) |
| The crank | `tests/test_crank.c` (22) |
| The resampler | `tests/test_resample.c` (10) |
| The interface and its grammar | `tests/test_ui.c` (42): scripted buttons and crank over the real engine; screenshots |
| The layers and the Playdate-only layer | `tests/test_layers.c` (81): silent without a 4th group (identical to ETCH DS), deterministic answers, mutes, ratchets, lengths, sound evolution, BITE, SPEAKER, the sidecar |
| What it costs | `tools/arm_cost.py` (docs/arm_cost.txt) |
| It runs in the Simulator | `tools/sim_drive.py`, `tools/sim_menu_test.py` |

## 12. Memory

`pdex.elf`:

* 203 KB of code and read-only data, of which ETCH DS's tables are about 80 KB
* 166 KB of `.bss`: the delay line (64 KB), the sidecar image (16 KB), the frame buffer (12.5 KB) and pattern state
* FOUNDRY's drum samples on the heap: under 1 MB
* the file layer: up to 256 KB while a save or load is in progress

All of it is well inside 16 MB.
