# ETCH CRANK — architecture (milestone 1)

ETCH CRANK — by Synth Centric — for Playdate

Two decisions were confirmed with the project owner before any code was written:

* **Render rate:** ETCH DS's engine runs unmodified at its own rate, 16 756 991 / 512 = **32 728.498 Hz**. A fixed-point polyphase resampler converts it to the Playdate's 44 100 Hz.
* **Threading:** the clock, the voices and FX run **inside the SDK's audio callback**. The update loop does input, CORROSION and drawing.

## 1. Repository layout

```
corrosion/   CORROSION from ETCH (Etch---GB @ 62ac4c6), byte-identical, SHA256SUMS,
             UPSTREAM_COMMIT, ETCH's own tests/test_generator.c
engine/      ETCH DS's sequencer and sound engine (ETCH-DS @ 8294b6d), unmodified,
             SHA256SUMS, UPSTREAM; ETCH DS's own tests in engine/tests/
platform/    the Playdate layer: main.c (boot, callback, input), pump.c (the audio
             pump + resampler), crank.c / crank_audio.c, screen.c, app_glue.c
tests/       host tests: consistency, crank, resampler, cost workload, qemu runtime
tools/       sync_corrosion.sh, sync_engine.sh, verify_corrosion.sh, arm_parity.sh,
             arm_cost.py, gen_resampler.py, sim_drive.py
Source/      pdxinfo (the build drops pdex.elf / pdex.so here for pdc)
```

**`corrosion/` and `engine/` are never edited.** `make` checks both against their checksums and refuses to build if either changed. Fixes go to ETCH (CORROSION) or ETCH-DS (the engine) first and come back with the sync scripts. `tools/verify_corrosion.sh /path/to/Etch---GB [/path/to/ETCH-DS]` proves the CORROSION copy file by file against ETCH and ETCH DS.

**The diff against ETCH DS is zero.** The Playdate layer reaches into the engine in exactly one place, a link-time wrap of `acid_render` (`-Wl,--wrap=acid_render`), for the crank. Everything else is ETCH DS's own API: `seq_command`, `seq_render_block`, `audio_render_block`, `acid_p`, `fx_p`, `host_clock`, and the app functions in `evolve.c`, `edit.c` and `demo.c`.

## 2. One core, two contexts

```
 audio callback (SDK, preempts the update loop)          update loop (50 Hz)
 ┌──────────────────────────────────────────────┐        ┌────────────────────────────────┐
 │ pump_pull(len at 44.1 kHz)                    │        │ buttons, crank change / docked │
 │   while the resampler needs input:            │◀───────│ crank_ui_frame -> PCMD_* ──────┼─┐
 │     drain the command ring                    │  SPSC  │ evo_frame: CORROSION prepares  │ │
 │     seq_render_block   (ETCH's clock, rules)  │  ring  │   the next mutation (evolve.c) │ │
 │     audio_render_block (ACID→FOUNDRY→FX)      │        │ app_fill_frame                 │ │
 │       └ __wrap_acid_render: crank glide       │        │ screen_frame (changed regions) │ │
 │     → mirrored engine-rate ring               │        └────────────────────────────────┘ │
 │   16-tap polyphase resampler → left/right     │◀──────────────────────────────────────────┘
 └──────────────────────────────────────────────┘
```

* **The same guarantees as the DS.** On ETCH DS, the ARM9's render interrupt preempted its main loop. On the Playdate, the audio callback preempts the update loop, so a block render runs to completion before the update loop runs again. That's what ETCH DS's main-loop code assumes, for example `foundry_rebuild`'s buffer swap and `edit.c`'s `edit_ack` spin. In the Simulator the callback is a separate thread; the command ring has full barriers, and the evolution hand-off uses ETCH DS's ordered protocol.
* **Commands.** `app_send` (ETCH DS's function, implemented in `pump.c`) writes to a 256-entry single-producer, single-consumer ring. The callback drains it before sequencing each block, at the same point in the block stream where a FIFO command reached the DS's ARM7. Codes from `0xC0` up are the platform's: `PCMD_CUTOFF` and `PCMD_RES`, the crank's targets in Q8. A full ring drops and counts (`pump_cmd_drops`) rather than waiting. It has never filled.
* **The CORROSION hand-off** is ETCH DS's, unchanged:
  * The update loop mutates a copy of the next bar's pattern and publishes it with its target and edit epoch (`evo_state` IDLE → READY).
  * At step 16's gate point the sequencer commits it, unless an edit moved the epoch (ETCH II's guard).
* **The audio callback never allocates, draws, locks, waits or touches the filesystem.** It reaches the pump, the sequencer, the engine, the crank's wrap and the clock read for the meter, and nothing else.

## 3. Clock and rate

* **Sample-accurate.** `seq_core` advances ETCH's phase accumulator per 32-sample sub-block inside `seq_render_block`, in engine samples, with ETCH II's swing fix. Tempo, swing and the gate points are therefore exactly ETCH DS's, in render time.
* **Wall-clock exact.** The resampler steps 0.742142812815 engine samples per output sample (32.32 fixed point). That's within 4 × 10⁻¹¹ of 32 728.498 / 44 100, so the tempo in seconds is ETCH DS's too.
* **Resampler** (`pump.c`, table from `tools/gen_resampler.py`):
  * a 16-tap Kaiser-windowed sinc, β 7, passband to 15.2 kHz, 128 phases
  * the outputs of the two neighbouring phases are interpolated linearly
  * two taps per `SMLAD` on the Cortex-M7; the host's plain C is bit-identical
  * the input ring is mirrored, so every tap window is contiguous
  * measured: flat (0.00 dB) to 10 kHz, −0.9 dB at 13 kHz, −5.3 dB at 15 kHz; worst spurious component −75.6 dB (at 10 kHz), better than −87 dB below 5 kHz; DC exact
  * latency 8 engine samples (0.25 ms) plus the callback buffer

## 4. The crank (`crank.h`)

* **Update loop** (`crank.c`):
  * `getCrankChange()` is accumulated, so the crank is relative and picking it up never jumps the filter.
  * 540° = 127 CUTOFF steps, with the remainder carried, so 540° in any number of pieces is exactly full travel.
  * Hard stops at both ends; turning back moves at once.
  * B held: resonance. Dock: a linear glide (in octaves) to the stored values over 60 000 / BPM ms. B + dock: the live values become the stored values.
  * The live value is stored + offset. ETCH's living-song drift (`evo_cut_ofs`) stays separate, on top, inside ACID, and never touches the offset.
* **Audio callback** (`crank_audio.c`, wrapping `acid_render`):
  * Once per control tick (32 samples, 0.98 ms), a one-pole glide (τ 4 ms) toward the newest target writes `acid_p.cutoff` / `acid_p.res`.
  * Then it calls the real `acid_render`, which interpolates the ladder coefficient sample by sample across the tick. The filter therefore moves on every sample, with no steps and no zipper noise.
  * Until the crank first moves, the wrap writes nothing. Docked at rest, it writes exactly the stored values. Either way the output is ETCH DS's, bit for bit.
* **Resolution:** a CUTOFF step is 0.656 semitones (ETCH DS's 7-bit control, the same as its XY pad). The full travel is 83.3 semitones (6.9 octaves), so 4.25° per step. See IDEAS.md for finer steps.

## 5. The screen (`screen.c`)

1-bit, 400 × 240, drawn with the SDK's graphics calls. Each region is redrawn only when what it shows has changed.

* **Status band (0–63):**
  * the cutoff arc: the live value as a 5 px arc, the stored value as a tick, resonance as the inner arc
  * the scope inside the arc
  * song, seed code, MODE, NEXT EVO, GENERATION, PAT / BAR, the live CUT (with its offset), RES, tempo, play state
* **LIVE (66–153):** BASS and ETCH's four lanes. Steps CORROSION has just changed start solid and fade through 75 / 50 / 25 % dither over two bars.
* **CPU meter (156–239):**
  * per stage, as a percentage of real time
  * clocked by the Cortex-M7's DWT cycle counter on the device, and by the host's monotonic clock in the Simulator; the clock rate is measured against the millisecond clock
  * every 8th window it is also logged to the console (`logToConsole`)

## 6. Proofs (`make test`, `make arm-test`, tools)

| Claim | How |
|---|---|
| CORROSION is ETCH's, byte for byte | `corrosion/SHA256SUMS` on every `make`; `tools/verify_corrosion.sh` against Etch---GB and ETCH-DS |
| ETCH's 44 tests pass against the copy | `corrosion/tests/test_generator.c` (unmodified) |
| The engine is ETCH DS's, unmodified | `engine/SHA256SUMS` on every `make`; ETCH DS's sequencer (231) and DSP (81) tests run against it |
| The port sounds exactly like ETCH DS | `tests/consistency.c`: 16 bars of CORRODE from `0F1C-9009-5870`, dice seed 0xC0DE, crank docked, ETCH DS (from an ETCH-DS checkout) vs the Playdate path: bit-identical |
| …with the Playdate's compiler and flags | `tools/arm_parity.sh`: the Playdate path built for the Cortex-M7 (SDK device flags), run under qemu-arm: bit-identical at the engine rate, and its 44.1 kHz output equals the host's |
| The crank's gestures and glide | `tests/test_crank.c` (22 checks) |
| The resampler | `tests/test_resample.c`: gain, images, the exact rate |
| What it costs | `tools/arm_cost.py`: instructions per stage, from a qemu trace of the Cortex-M7 build |
| It runs in the Simulator | `tools/sim_drive.py`: Xvfb + PulseAudio, clicks and keys, screenshots, recording |

## 7. Memory

`pdex.elf`: 143 KB of code and read-only data (ETCH DS's DSP tables are 80 KB of it), 81 KB of `.bss` (the delay line is 64 KB). FOUNDRY's drum samples are on the heap, under 1 MB. All of this is well inside the Playdate's 16 MB.
