# ETCH CRANK — ideas parked for later milestones

ETCH CRANK — by Synth Centric — for Playdate

Milestone 1 proves the crank and the sound. These ideas came up while building it, and none of them is implemented.

## Crank

* **Finer cutoff resolution.** The crank moves ETCH DS's 7-bit CUTOFF (128 steps, 0.656 semitone each). ACID interpolates the ladder coefficient per sample across every control tick, so a sweep is smooth, never stepped or zippered. But at high resonance, a very slow sweep is a glissando in 0.66-semitone steps, each ramped over 1 ms. Sub-step resolution needs one fractional cutoff term in ACID (`acid.arm.c`). That is an ETCH DS change, so make it there first and sync it here.
* **Per-pattern stored cutoff.** In ETCH, cutoff and resonance belong to the project (`synth_t`), not to each pattern. The brief says "each pattern stores cutoff and resonance". Milestone 1 uses the project's values as the stored values. Per-pattern values would be a DS-layer field, saved outside ETCH's `project_t` so ETCH codes and saves still match.
* **Crank recording (B + UP)** into a motion lane, and CORRODE re-timing recorded lanes around their own anchor (host-side, outside `corrosion/`).
* **CRANK BITE** passes the filter's openness as the `intensity` argument CORROSION already takes. With the crank docked, or BITE off, the value is the project's own MUTATE.
* **Resonance travel.** B + crank uses the same 540° as cutoff. It may want less travel (resonance is less musical to sweep slowly).

## Audio

* **A cheaper resampler.** It's 5.7 % of the CPU at one cycle per instruction, which is more than ACID. A single dot product at the nearest of 512 phases (a 16 KB table) halves it, for about −59 dB of phase noise at 10 kHz (now below −75 dB). Measure the real cost on hardware before choosing.
* **SPEAKER mode** on the limiter: mono, a low-mid lift, switched on when headphones are unplugged (`getHeadphoneState`).
* **TILT** to the delay send, off by default.
* **ETCH DS's FX parameters** are fixed at boot (the CORRODE song's). SYNTH will expose them.

## System

* **CODE and DEMOS in FILE**, because the system menu silences the audio (report §3). That frees the menu for SPEAKER and TILT.
* **The `cpu test` menu item** is a milestone-1 tool; remove it when the real menu arrives.
* **Saves** through `playdate->file`, checksum first and header last (ETCH's order). `engine/storage.c` (ETCH DS) uses stdio, which the device doesn't have, so the Playdate needs its own storage backend under the same format. **Ask before choosing the format.**
* **Reading the Playdate revision.** The SDK doesn't report Rev A or Rev B. The meter shows the measured clock; note on the device which unit you measured.

## Tooling

* **A cycle-accurate cost estimate.** `tools/arm_cost.py` counts instructions exactly under qemu, but cycles depend on the Playdate's caches and SDRAM. Once hardware figures exist, fit a CPI per stage and keep the tool as a regression check.
* **The Simulator drivers** (`tools/sim_drive.py`, `tools/sim_menu_test.py`) click fixed positions of the Simulator 3.1.2 window. `sim_menu_test.py` verifies the menu state from a screenshot and retries; `sim_drive.py`'s single menu click can still miss.
