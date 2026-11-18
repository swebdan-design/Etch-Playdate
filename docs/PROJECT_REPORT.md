# ETCH CRANK — project report (milestones 2–4)

ETCH CRANK — by Synth Centric — for Playdate · Playdate SDK 3.1.2 · arm-none-eabi-gcc 13.2.1

This continues [`MILESTONE1_REPORT.md`](MILESTONE1_REPORT.md). **No Playdate hardware was available**: everything was measured on the host, under qemu-arm with the device's compiler and flags, and in the Playdate Simulator.

## 1. What was built

| Milestone | What | Status |
|---|---|---|
| 2 | The six screens in ETCH's order with ETCH's grammar; the status band; the 1-bit renderer with ETCH DS's fonts | **Done**: `test_ui` (42 checks), screenshots, Simulator |
| 2 | Saves in ETCH DS's slot format through `playdate->file`, with ETCH's write order; the sidecar; ETCH DS and ETCH Game Boy import; ETCH's eight demo songs, byte for byte | **Done**: host round trips; in the Simulator, a save written to the data folder with valid magics and checksums (§3) |
| 3 | Crank recording (motion lanes, evolved by CORRODE), A + crank riding values, scenes with crank morph, mute groups, the mod matrix (CRANK, TILT, LFO, ENV, MIC), SPEAKER, TILT, CRANK BITE, the system menu | **Done**: `test_ui`, `test_layers` |
| 4 | The ETCH DS layers: RUST, METAL and NOISE, ratchets, per-track lengths, the answering generator (seeded by the 4th code group), sound evolution with knob locks | **Done**: `test_layers` (81 checks), Simulator |

**Still holding after all of it:**

* `corrosion/` and `engine/` are unmodified (checksums checked on every build).
* The 16-bar sound consistency test against ETCH DS is **bit-identical** on the host and on the Cortex-M7 build, with every wrap linked in.
* With the layers bound but off, the output is ETCH DS's exactly (`test_layers` §1).

## 2. Measurements

### CPU (instructions exact, cycles estimated)

From `tools/arm_cost.py` (full table: `docs/arm_cost.txt`): the Cortex-M7 build, a qemu instruction trace, 2 s of steady state. Figures are millions of instructions per second, then % of a 168 MHz core at 1.0 / 1.5 / 2.5 cycles per instruction.

| Stage | Song | ETCH DS's CPU test | Every layer at its worst |
|---|---|---|---|
| ACID (+ crank) | 2.97 · 1.8/2.7/4.4 | 4.66 · 2.8/4.2/6.9 | 2.97 · 1.8/2.7/4.4 |
| RUST | — | — | 5.34 · 3.2/4.8/8.0 |
| FOUNDRY | 1.65 · 1.0/1.5/2.5 | 5.39 · 3.2/4.8/8.0 | 3.91 · 2.3/3.5/5.8 |
| FX (drive, crush, delay, comb, limiter) | 4.95 | 5.54 | 5.54 |
| resampler | 9.49 · 5.7/8.5/14.1 | 9.49 | 9.49 |
| **audio callback** | **19.3 · 11.5/17.3/28.8** | **25.3 · 15.1/22.6/37.7** | **27.6 · 16.4/24.6/41.0** |

"Every layer at its worst" means:

* a RUST 3-note stab on every 16th
* METAL and NOISE on every 16th
* ratchets ×4 on every drum hit
* every FX stage working

Even at the pessimistic 2.5 cycles per instruction, the worst case leaves more than half the core for the update loop.

### Audio level

ETCH DS's limiter holds the engine at 0.85. Resampling to 44.1 kHz overshoots on its flat tops and on the crusher's steps: an unclipped 1.048 over the consistency song, so 4 samples clipped. The resampler now has **−1 dB of headroom** (0.89, built into its coefficients), and the peak is **0.933**. Passband flatness and image rejection are unchanged: flat to 10 kHz, spurious products below −75 dB.

## 3. The Simulator (SDK 3.1.2, headless)

* **Every screen runs**, B + LEFT / RIGHT moves along the row, and audio plays (about −11 dBFS).
* **GEN → ANSWER** gives the code a 4th group (`0F1C-9009-5870-A1DD`), and RUST's answer appears under the bass on LIVE. Screenshots: `docs/images/sim_*.png`.
* **FILE → SAVE** wrote `etch_crank.sav` (262 160 bytes, ETCH DS's 16 slots plus metadata) and `etch_crank_ext.sav` to the game's data folder. Re-read independently, slot 1 has magic `ETDS` version 2, project "CORRODE", and a valid Fletcher-16; the sidecar has `ETCK` and a valid checksum.
* **The system menu silences the game** (`tools/sim_menu_test.py`, 3 runs out of 3), so CODE and the demos are in FILE.

## 4. What I could not verify

* **Anything on hardware:**
  * real CPU on Rev A and Rev B (the meter is on SYNTH → CPU, with ETCH DS's CPU TEST)
  * callback timing and dropouts
  * whether the DWT cycle counter is readable from a game (if it isn't, rebuild with `-DETCH_CPU_METER=0`)
  * the crank's feel, TILT, the mic and headphone detection
  * whether the system menu silences audio on the device too
* **How it sounds.** I can't listen. ETCH DS's sound is matched sample for sample. RUST, the layers' musical taste, SPEAKER's lift and the answering generator's density are first drafts tuned by measurement, not by ear. Please listen:
  * `build/host/crank_sweep.wav` (from `make test`)
  * a song with **GEN → ANSWER**
* **ETCH DS / Game Boy imports on the device.** They're tested through ETCH DS's own code on the host, and the file layer in the Simulator, but not with a real `etch_ds.sav` on a Playdate.

## 5. Deviations, and what is not built

* **64 patterns and a 64-slot chain with sections are not built.** ETCH's `project_t` (CORROSION's `etch.h`, which may not change) holds 32 patterns and a 16-slot chain, and ETCH DS's sequencer reads exactly those. Growing them needs a change in ETCH and ETCH DS first.
* **Per-track length applies to the layers only** (RUST, METAL, NOISE, 1–16 steps). ETCH's bass and drums keep ETCH DS's 16 steps for the same reason.
* **The EBM, DUB TECHNO and NOISE styles wait for the series** to decide how codes carry them (your choice: no new digits).
* **RUST's "industrial wavetables"** are ACID's four band-limited tables, with HOLLOW and BITE, hard sync and ring mod giving the industrial edge. No new tables.
* **Stored cutoff is per project, as ETCH.** Each scene stores its own.
* **The layers sit inside the sidecar,** so a project opened on ETCH DS plays without them (by design: the same as a 12-digit code).
* **Sound evolution is off by default,** so the default sound stays ETCH DS's.
* **The crank's resolution** is still ETCH DS's 7-bit cutoff (IDEAS.md).

## 6. Open questions, with recommendations

1. **Move the layers into ETCH-DS?** Yes. `layers/` has no Playdate calls and reaches the engine only through the wraps listed in ARCHITECTURE.md §2. In ETCH-DS the wraps could become direct calls, and the answering generator would give ETCH DS and ETCH 3DS the same answers for the same 16-digit codes.
2. **Answering density and taste.** Listen first. The constants are at the top of `layers/answer.c`, grouped by part.
3. **The 64-pattern song structure.** Decide it series-wide (ETCH, ETCH DS, ETCH 3DS). It is the one feature in the brief that needs `project_t` to grow.
4. **Hardware run.** Sideload `release/ETCH_CRANK.pdx.zip` on a Rev A and a Rev B and read SYNTH → CPU, idle and with CPU TEST. That replaces the estimates in §2.
