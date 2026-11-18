# ETCH CRANK — milestone 1 report: "It squelches under the crank"

ETCH CRANK — by Synth Centric — for Playdate · Playdate SDK 3.1.2 · arm-none-eabi-gcc 13.2.1

> **Since milestone 1** (see [PROJECT_REPORT.md](PROJECT_REPORT.md)): the temporary `cpu test` system-menu item moved to SYNTH → CPU; the system menu now holds speaker, tilt and crank bite; the resampler gained −1 dB of headroom (its CPU figures are unchanged); and the CPU table there adds RUST and the layers.

**No Playdate hardware was available.** Everything below was measured on the host, under qemu-arm with the device's compiler and flags, and in the Playdate Simulator (headless). §4 lists what still needs a device.

## 1. What works

| # | Deliverable | Status | Evidence |
|---|---|---|---|
| 1 | SDK project builds a `.pdx` for Simulator and device | **Done** (device untested) | `make` → `ETCH_CRANK.pdx` (device `pdex.bin` 114 KB + Simulator `pdex.so`), no warnings. Runs in the Simulator (§2.4). |
| 2 | CORROSION copied in, checksummed, ETCH's 44 tests pass against the copy | **Done** | `corrosion/` = Etch---GB @ `62ac4c6`, byte for byte (`tools/verify_corrosion.sh` also matches ETCH-DS's copy). `make` refuses to build if it changes. **44 checks, 0 failures.** |
| 3 | Audio source, sample-accurate clock, step rules, swing fix, edit epoch | **Done** | One stereo `addSource` callback runs ETCH DS's `seq_core` (ETCH's rules, ETCH II's swing fix and epoch guard) in render time. ETCH DS's own sequencer tests: **231, 0 failures**. In the Simulator: 0 late gates, 0 ring drops. |
| 4 | ACID ported from ETCH DS; FOUNDRY drums | **Done (zero diff)** | `engine/` = ETCH-DS @ `8294b6d`, unmodified and checksummed: all four oscillators, the 2× ladder, envelopes, accent capacitor, constant-time slide, drive, and FOUNDRY's six lanes with ETCH's kits. ETCH DS's DSP tests: **81, 0 failures**. |
| 5 | ETCH demo song from its seed code, DRIFT on launch | **Done** | CORRODE from `0F1C-9009-5870` (ETCH DS's `demo_load`), DRIFT, playing at launch with no button press. Simulator: DRIFT evolved on schedule (generation counter, NEXT EVO, the dithered fade of changed steps). |
| 6 | Crank on cutoff (540°, octaves, per-sample glide), B + crank on resonance, docking home; status band with cutoff arc | **Done** | `test_crank`: **22 checks, 0 failures**. In the Simulator: +270° → CUT 93 (+63); −540° → 0 at the hard stop; +180° then B + crank −180° → CUT 42, RES 50; dock → home at CUT 30, RES 92. Screenshots: `docs/images/`. |
| 7 | Sound consistency test for ACID and drums | **Passes: bit-identical** | §2.1 |
| 8 | CPU on Rev A and Rev B, render-rate recommendation | **Estimated; not measured on hardware** | §2.2. The on-device meter and the `cpu test` menu item are ready for the hardware measurement. |

**Your decisions, as implemented:**
* **Render rate:** ETCH DS's engine runs at its own 32 728.5 Hz, unmodified, and a fixed-point resampler converts it to 44.1 kHz.
* **Threading:** everything audio runs in the SDK audio callback; the update loop does input, CORROSION and drawing. They talk through an SPSC command ring and ETCH DS's epoch-guarded hand-off.

## 2. Measurements

### 2.1 Sound consistency

`make test DS=/path/to/ETCH-DS` builds the ETCH DS side from the ETCH-DS checkout's own sources. Both sides play:

* ETCH's demo 1, CORRODE, from its seed code `0F1C-9009-5870`, in DRIFT
* dice seed 0xC0DE, CORROSION's entropy fixed
* crank docked
* 16 bars at 124 BPM

The port side runs the real Playdate path: the command ring, the callback pulling uneven lengths, the crank's wrap and update loop, and the resampler. Its engine-rate output is captured before the resampler.

| Comparison | Result |
|---|---|
| ETCH DS vs ETCH CRANK, host | **bit-identical**, 1 013 632 frames (30.97 s), both channels; hash `8be7eda9b483e56d`, 4 evolutions on both |
| ETCH DS (host) vs ETCH CRANK built for the **Cortex-M7 with the SDK's device flags**, run under qemu-arm | **bit-identical** (`make arm-test`) |
| 44.1 kHz output: host build vs Cortex-M7 build | **bit-identical**, 1 365 632 frames |

**One timing difference, explained.** The test runs the update loop at the ETCH DS side's block boundaries. If instead it free-runs at 50 Hz between uneven callbacks, as on the device, the two renders differ in one window: from bar 8, step 16, 2 542 samples into the step (15.44 s), to 18.73 s. After that they are identical again, with the same patterns and the same drift values at the end.

* **Why:** after each evolution, ETCH's sound drift nudges the cutoff and drum tuning. The update loop applies the nudge when it notices the commit (ETCH DS's `evolve.c`), so it lands up to one frame apart.
* ETCH DS on its own hardware varies the same way, because its main loop free-runs against the audio too.
* `make test` prints both results; the bit-identical one is the test.

### 2.2 CPU

**Not measured on hardware.** What I could measure is exactly how many instructions each stage executes on the Cortex-M7 build (`tools/arm_cost.py`, a qemu instruction trace with boot subtracted, `docs/arm_cost.txt`). Turning instructions into cycles needs a CPI (cycles per instruction) that only hardware can give: the M7 is dual-issue, but the Playdate runs from SDRAM through small caches, and Rev B's RAM is slower. So the figures are given at three CPIs.

* **Song** is the boot song in DRIFT with the crank moving.
* **CPU test** is ETCH DS's CPU test: ACID retriggered with an accent on every 16th, all **six** drum lanes on every 16th, every FX stage working, crank moving.

| Stage | Song, M instr/s | CPU test, M instr/s | CPU test, % of 168 MHz at CPI 1.0 / 1.5 / 2.5 |
|---|---|---|---|
| ACID (+ crank glide) | 2.97 | 4.66 | 2.8 / 4.2 / 6.9 |
| FOUNDRY, 6 lanes | 1.65 | 5.39 | 3.2 / 4.8 / 8.0 |
| FX drive | 1.31 | 1.31 | 0.8 / 1.2 / 2.0 |
| FX crush | 0.00 (off) | 0.58 | 0.3 / 0.5 / 0.9 |
| FX delay | 1.36 | 1.36 | 0.8 / 1.2 / 2.0 |
| FX comb | 0.86 | 0.86 | 0.5 / 0.8 / 1.3 |
| FX limiter | 1.42 | 1.43 | 0.8 / 1.3 / 2.1 |
| events | 0.16 | 0.16 | 0.1 / 0.1 / 0.2 |
| sequencer | 0.06 | 0.06 | 0.0 / 0.1 / 0.1 |
| resampler | 9.49 | 9.49 | 5.7 / 8.5 / 14.1 |
| **audio callback total** | **19.3** | **25.3** | **15.1 / 22.6 / 37.7** |
| update loop (CORROSION, crank) | 0.01 | 0.01 | ~0 |

* **A planning range:** CPI 1.0–1.5 for Rev A, and up to about 2.5 for Rev B's slower RAM. That's a guess; the hardware meter replaces it.
* The FX chain isn't stubbed: it is ETCH DS's full chain.
* The resampler started at 16 M instructions per second. Mirroring its ring and using SMLAD brought it to 9.5 M; IDEAS.md has a way to halve it again.

**Measuring on hardware** (Rev A and Rev B):

1. Sideload `release/ETCH_CRANK.pdx.zip`.
2. Read the CPU meter on the LIVE screen. The figures are percentages of real time from the M7's cycle counter, with the clock rate measured.
3. Then tick **Menu → `cpu test`**.

The meter also logs a line to the console every few seconds, readable over USB serial.

**Render-rate recommendation: keep ETCH DS's 32.7 kHz engine with the resampler, and don't go to 22.05 kHz.**

* Even at the pessimistic CPI 2.5, the full CPU test uses 38 % of the core.
* ACID costs 2.8–6.9 % in the CPU test. RUST (two oscillators, sync, ring mod, three paraphonic notes) should cost about 3× ACID, which is 8–20 %. That still leaves 40 % or more free in the worst case.
* Native 44.1 kHz rendering would cost about 35 % more for ACID and RUST, and would break bit-identity with ETCH DS. 22.05 kHz isn't needed by these numbers.
* If hardware disagrees, cut the resampler first (IDEAS.md), then the ladder's 2× oversampling, before any rate change.

### 2.3 Audio dropouts

* **Host and qemu:** the pump renders on demand, so a dropout can only come from the callback overrunning on hardware. That isn't measurable here.
* **In the Simulator:**
  * The recording shows continuous audio from launch to quit (about −10 to −12 dBFS RMS per 0.5 s window).
  * There were no gaps except where intended: B + DOWN stop, and the open system menu.
  * Gates late 0, command-ring drops 0.

### 2.4 The Simulator run (`tools/sim_drive.py`, log in `docs/sim_run.txt`)

* Launch: audio about 5 s after starting the Simulator (its own start-up included), with no input; CORRODE playing in DRIFT.
* Crank, B + crank, dock and B + DOWN all behave as listed in §1.
* **System menu:** while it is open, ETCH CRANK's audio **stops**, and it resumes when the menu closes. `tools/sim_menu_test.py` checks this reproducibly. It confirms from a screenshot that the menu really is open, then records each phase. Three runs out of three (`docs/sim_menu_test.txt`):
  * playing: −11 dBFS
  * menu open: digital silence, peak 0
  * menu closed again: −11 dBFS

  In an earlier manual probe the callback ran 4 times in about 5.5 s with the menu up, against about 115 times a second while playing, and the game's millisecond clock stopped too.

## 3. What I could not verify

* **Anything on a Playdate:**
  * Real CPU per stage on Rev A and Rev B.
  * Callback timing, and dropouts under load.
  * Whether the DWT cycle counter is readable from a game. If it isn't, the CPU meter faults at boot; rebuild with `UDEFS = -DETCH_CPU_METER=0` to turn it off.
  * The crank's feel at 540°.
  * Whether the system menu silences audio on hardware as it does in the Simulator.
* **How it sounds.** I can't listen. The sound is ETCH DS's sample for sample (§2.1); the crank and the resampler are checked by measurement. Please listen to:
  * `build/host/crank_sweep.wav` from `make test`: the boot song, crank open over 4 bars, closed over 4, then B + crank on resonance
  * `build/host/consist_port_44k.raw`: the 16-bar consistency render at 44.1 kHz, raw stereo int16
* **The Simulator's own audio path.** It isn't the device's.
* **Speaker vs headphones.** There's no SPEAKER mode yet (milestone 2).

## 4. Deviations and notes

* **The DS-only layers in the brief** (RUST, sound evolution with knob locks, the answering generator, extra code digits, EBM / DUB TECHNO / NOISE, motion lanes, ratchets) **are not in ETCH-DS yet.** Its newest code, on `main` and on the unpublished 0.4 series branch, is milestone 1 (ACID, FOUNDRY, FX). Milestone 1 here needs only that. The rest has to be built in ETCH DS first, then synced, to keep "same composer, same codes".
* **Stored cutoff is per project, not per pattern.** ETCH stores cutoff and resonance in `synth_t`, for the whole project, so the stored value is the project's. See IDEAS.md.
* **A crank step is 0.656 semitones,** ETCH DS's 7-bit CUTOFF. Sweeps are smooth per sample, but sub-step resolution needs a one-line ETCH DS change. I didn't make it (rule: don't edit the engine). IDEAS.md.
* **The `cpu test` system-menu item is temporary.** It's there for the hardware measurement.
* **B + UP and B + LEFT / RIGHT only show a message in milestone 1.** There is no recording and only one screen yet.
* **A gap I fixed:** the crank's audio half kept its engaged state across a re-init. It now resets in `pump_init`, which a project load will need.

## 5. The button map

The full map is in [`CONTROLS.md`](CONTROLS.md). In short:

* Held B is SELECT: **B + ←/→** screens, **B + ↓** play / stop, **B + ↑** arm crank recording.
* A, A + D-pad and B + A are exactly ETCH's.
* **B tap** on LIVE is FILL, as on ETCH.
* Crank: cutoff. B + crank: resonance. Dock: home over a beat. B + dock: keep.
* ETCH's SELECT + A / B / ↑ / ↓ and its B + D-pad uses move to places ETCH already uses for the same kind of thing: the LIVE pads, the PATTERN readout, TRACK's columns, SYNTH's tab row. Two points are open in CONTROLS.md §5.

## 6. Open questions, with recommendations

1. **Name.** ETCH CRANK, as you chose. No action.
2. **Is 540° right, or a setting?**
   * Keep **540° as the default**: 4.25° per 0.66-semitone step, so one turn and a half sweeps the whole filter, and a quarter turn is about 14 semitones, a musical gesture.
   * Add a **travel setting (360 / 540 / 720°)** in SYNTH once it exists, and decide by feel on hardware.
3. **Docking: return home, or hold?**
   * **Return home by default** (built). The pillar says the song should sound finished with the crank docked, and returning makes docking a musical gesture: a one-beat sweep back.
   * B + dock already covers "hold".
4. **Does audio keep running while the system menu is open?**
   * **Tested in the Simulator: no.** The callback stops and the output is silent.
   * So, per the brief, **CODE entry moves into the app** (FILE), and I'd move DEMOS there too, leaving the menu for SPEAKER and TILT.
   * Re-check on hardware, but design for silence.
5. **A sync pulse on one headphone channel, pocket-operator style?**
   * **Yes, as an opt-in output mode** (SYNC: OFF / PO), not in milestone 2's critical path.
   * It costs the stereo image when on: the left channel becomes the pulse, the right carries a mono mix.
   * It's sample-accurate for free: `seq_core` already knows each step's exact sample offset (its EV_STEP events), so the pulse can be written into the output at that sample.
6. **TILT in version 1?**
   * **Yes, but off by default and only on the delay send,** as the brief scopes it.
   * It's cheap (an accelerometer read per frame, a parameter update), but on a handheld that's also cranked it moves by accident, so it should be something you choose.
7. **ETCH DS project saves: import, not just codes?**
   * **Yes, read-only import of ETCH DS's `etch_ds.sav` slots,** as ETCH DS imports ETCH Game Boy saves.
   * The data is ETCH's `project_t` plus ETCH DS's sound settings, the same structures this port already uses. `engine/storage.c` already parses and checksums it.
   * The Playdate needs only a read path through `playdate->file`.
   * **The ETCH CRANK save format itself is still to be decided with you.**
