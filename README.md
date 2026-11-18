# ETCH CRANK

**A generative acid machine for Playdate — by Synth Centric**

ETCH CRANK is the fifth ETCH, after ETCH (Game Boy), ETCH II (GBA), ETCH DS and ETCH 3DS. **The crank is the 303's filter cutoff:** turn it clockwise to open the filter and back to close it. Everything else is ETCH: the same composer (CORROSION, byte for byte), the same seed codes, the same living songs, and ETCH DS's sound, sample for sample.

![LIVE](docs/images/screen_live.png)

* **Launch it, hear music.** It boots into ETCH's demo song CORRODE, composed by CORROSION from its seed code `0F1C-9009-5870`, living in DRIFT. The crank is never needed for sound.
* **The crank:** cutoff (relative, 540° with hard stops, equal turns are equal intervals); B + crank for resonance; dock to glide home over a beat, B + dock to keep. Hold A on any value and turn to ride it, or on a scene to morph towards it. **B + UP records** a bar of cranking into a motion lane that CORRODE then bends.
* **ETCH's six screens in ETCH's order** with ETCH's grammar: SYNTH ← TRACK ← **LIVE** → GEN → SONG → FILE. Held B is SELECT.
* **ETCH DS's engine, unmodified and sample for sample:** ACID, FOUNDRY's drum synth, the FX chain, ETCH's step rules and clock.
* **The ETCH DS layers:** RUST (a second voice: two oscillators, sync, ring, 3-note paraphony), METAL and NOISE lanes, ratchets and polymeter lengths, all written by an **answering generator** seeded by the code's 4th group; sound evolution with knob locks.
* **Saves in ETCH DS's format.** ETCH DS saves load as they are, ETCH Game Boy saves import, and ETCH's eight demo songs are built in.
* **For the Playdate:** SPEAKER mode, TILT, CRANK BITE, scenes, mute groups, a mod matrix, and a 1-bit face drawn in ETCH DS's own pixel fonts.

The manual is [`docs/ETCH_CRANK_Manual.md`](docs/ETCH_CRANK_Manual.md). The controls are in [`docs/CONTROLS.md`](docs/CONTROLS.md), the design in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and the reports in [`docs/MILESTONE1_REPORT.md`](docs/MILESTONE1_REPORT.md) and [`docs/PROJECT_REPORT.md`](docs/PROJECT_REPORT.md).

## Controls

| | |
|---|---|
| crank | filter cutoff |
| B + crank | resonance |
| dock / B + dock | glide home over a beat / keep |
| A held + crank | ride the value under the cursor, or morph to a scene |
| B + LEFT / RIGHT | previous / next screen |
| B + DOWN | play / stop |
| B + UP | record a crank lane (from the next bar) |
| A, A + D-pad, B + A | place, change, delete (ETCH's grammar) |
| B (tap) | LIVE: FILL on / off |
| Menu | speaker, tilt, crank bite |

## Building

You need the Playdate SDK (tested with 3.1.2, with `PLAYDATE_SDK_PATH` set) and `arm-none-eabi-gcc` (tested with 13.2.1).

```bash
make
```

That checks that `corrosion/` and `engine/` still match their checksums, then builds `ETCH_CRANK.pdx` for the device and the Simulator. A prebuilt `release/ETCH_CRANK.pdx.zip` is included for sideloading.

## Testing

```bash
make test DS=/path/to/ETCH-DS
```

* ETCH's 44 CORROSION tests and ETCH DS's sequencer and sound tests, all unmodified, against the copies here.
* The **sound consistency test**: 16 bars of CORRODE through ETCH DS (built from that checkout) and through this port. They are bit-identical.
* The crank, the resampler, the interface (scripted buttons and crank, screenshots), and the layers.
* It writes `build/host/crank_sweep.wav` to listen to.

```bash
make arm-test
```

The consistency test again, built for the Cortex-M7 with the SDK's device flags and run under `qemu-arm`.

```bash
python3 tools/arm_cost.py
```

Instructions per stage on the Cortex-M7 build (a qemu trace).

```bash
python3 tools/sim_drive.py
```

Drives the Playdate Simulator headless. It needs Xvfb, PulseAudio and xdotool.

```bash
python3 tools/sim_menu_test.py
```

Checks whether the audio keeps running while the system menu is open. (In the Simulator it doesn't.)

```bash
tools/verify_corrosion.sh /path/to/Etch---GB /path/to/ETCH-DS
```

Proves CORROSION byte for byte against ETCH and ETCH DS.

## Layout

```
corrosion/   CORROSION from ETCH, byte-identical (checksummed)
engine/      ETCH DS's sequencer and sound engine, unmodified (checksummed)
layers/      the ETCH DS layers: RUST, the answering generator, ratchets, sound evolution
platform/    the Playdate: audio pump + resampler, crank, screens, saves, boot
tests/       host tests
tools/       sync, verify, parity, cost, resampler table, Simulator driver
docs/        architecture, controls, milestone report, ideas
```

## Credits

**ETCH CRANK — by Synth Centric, for Playdate.** Synth Centric is Dan Schwebel. CORROSION comes from ETCH (MIT; see `corrosion/LICENSE.ETCH`), the sound engine from ETCH DS, and the swing-change fix and edit epoch from ETCH II. Built with the Playdate SDK. Playdate is a trademark of Panic Inc.; ETCH CRANK is not affiliated with or endorsed by Panic. TB-303 is a trademark of Roland Corporation; ETCH CRANK is not affiliated with Roland.
