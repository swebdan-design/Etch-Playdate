# ETCH CRANK — controls

ETCH CRANK — by Synth Centric — for Playdate

The Playdate has a D-pad, A, B and the crank. It has no SELECT or START, and Menu and Lock belong to the system. ETCH's grammar is kept exactly: **held B stands in for SELECT**, and the crank is for performing, never for scrolling menus.

## 1. Everywhere

| Buttons | Action | ETCH equivalent |
|---|---|---|
| **crank** | Filter cutoff: the live offset over the stored value. Relative; 540° from closed to open; hard stops at both ends | (new) |
| **B + crank** | Resonance | (new) |
| **A held on a value + crank** | Ride that value (any value on SYNTH, GEN, SONG and the LIVE macros) | (new) |
| **A held on a scene + crank** | Morph towards that scene (LIVE pads S1–S4); half a turn is the whole morph | (new) |
| **dock the crank** | Cutoff and resonance glide back to the stored values over one beat | (new) |
| **B + dock** | Keep the live cutoff and resonance as the stored values | (new) |
| **B + LEFT / RIGHT** | Previous / next screen: SYNTH ← TRACK ← LIVE → GEN → SONG → FILE (wraps) | SELECT + LEFT / RIGHT |
| **B + DOWN** | Play / stop | START |
| **B + UP** | Arm crank recording: from the next bar, one pass of the crank is written to the pattern's cutoff lane, which then plays back (B + UP again disarms) | (new) |
| **B** (tap) | LIVE: FILL on / off (latched). Elsewhere: back / cancel | ETCH LIVE's B (= SELECT + START) |

**A B tap** is B pressed and released with no other button and no crank movement in between, so B + crank, B + dock and B + D-pad never toggle the fill.

## 2. The editing grammar (every screen, as ETCH)

| Buttons | Meaning |
|---|---|
| D-pad | Move the cursor |
| A (tap) | Place / toggle / do. On an empty step A writes the last note; on a filled one it changes it |
| A held + LEFT / RIGHT | Value −1 / +1 (a semitone on a note) |
| A held + UP / DOWN | Value coarse (an octave on a note) |
| B + A | Delete, or reset to default |

On list screens (SYNTH, GEN, SONG) plain LEFT / RIGHT also changes the value, as in ETCH.

## 3. Screen by screen

**LIVE** (home). Rows BASS, KICK, SNARE, HAT, OPEN (CORROSION's), RUST, METAL, NOISE (the ETCH DS layers); the pads; the macros.

| Where | Buttons | Action |
|---|---|---|
| bass / RUST step | A | empty: the last note; a note: remove it |
| | A + LEFT / RIGHT, A + UP / DOWN | semitone, octave |
| | B + A | remove |
| drum / METAL / NOISE step | A, B + A | hit on / off, clear |
| lane name | A | mute |
| | A + LEFT / RIGHT | mute group 1, 2 or none |
| pads | A | FILL (one shot), REGEN, EVOLVE, UNDO; S1–S4: store (empty) or recall; M1 M2: toggle the group |
| | B + A on a scene | clear it |
| | A held + crank on a scene | morph towards it |
| macros | LEFT / RIGHT | choose |
| | A + D-pad | change: PAT (queues a pattern from the next bar), ENV, DEC, ACC, DRV, RGN (what REGEN replaces), EVO (STABLE / DRIFT / CORRODE), MUT |

A hand edit of RUST, METAL or NOISE locks that pattern's layers from the answering generator.

**TRACK**. ETCH's tracker: `# NOT A S L K S H O P CD`, and a LAYERS page: `# RST C A T M N RK RS RH RO`.

| Where | Buttons | Action |
|---|---|---|
| header | LEFT / RIGHT | PATTERN or PAGE |
| | A + LEFT / RIGHT on PATTERN | previous / next pattern, from the next bar (ETCH: B + LEFT / RIGHT) |
| | A on PAGE | ETCH / LAYERS |
| NOT | A, A + D-pad, B + A | as ETCH (A on a note: tie) |
| A S L / K S H O | A, B + A | toggle, clear |
| P | A + D-pad, B + A | chance, back to 8/8 |
| CD | A + D-pad, B + A | condition, back to every loop |
| # | A + UP / DOWN | jump a beat (ETCH: B + UP / DOWN) |
| RST, C A T, M N | as above | RUST note, chord / accent / tie, METAL / NOISE |
| RK..RO | A + D-pad, B + A | ratchet 1–4 hits, back to 1 |

**SYNTH**. Tabs BAS RST KCK SNR HAT OPN MTL NSE FX MOD EVO CPU on the top row (LEFT / RIGHT there change tab; ETCH: B + LEFT / RIGHT). Below, a list: LEFT / RIGHT ±1, A + UP / DOWN coarse, B + A default, A plays the channel, A held + crank rides.

**GEN**. CORROSION's settings (list). SEED: A gives a new seed and GEN ALL. CODE: LEFT / RIGHT pick a digit (12, plus the 4th group), A + UP / DOWN change it, A loads it, B + A clears the 4th group. Actions (D-pad, A): GEN ALL, GEN BASS, GEN DRUMS, GROOVE, EVOLVE, UNDO, LOCK BASS, LOCK PATT, ANSWER (a new 4th group: the layers answer), NO LAYERS.

**SONG**. SONG MODE (A), BPM, SWING; the 16-slot chain (A + LEFT / RIGHT pattern, A + UP / DOWN bars, A jump, B + A END); COPY (A + D-pad where, A copies); CLEAR (A, A).

**FILE**. NEW (A, A), LOAD (ETCH's demos D1–D8, the boot song, the 16 saves; A, A), SAVE (A; A, A over a used slot; B + A then A erases), CODE (to GEN's code row), IMPORT DS, IMPORT GB (A, A), ABOUT (LEFT / RIGHT turn the pages). B (tap) goes back to the menu.

## 4. Where ETCH's SELECT combos went

Held B as SELECT collides with ETCH's own B + D-pad and with B + A (delete). Nothing is dropped; each moves to a place ETCH already uses for the same kind of thing (decided with the project owner):

| ETCH | On ETCH CRANK |
|---|---|
| SELECT + A (evolve) | LIVE's EVOLVE pad, GEN's EVOLVE |
| SELECT + B (undo) | LIVE's UNDO pad, GEN's UNDO |
| SELECT + START (fill latch) | B tap on LIVE (as ETCH's LIVE) |
| SELECT + UP / DOWN (queue a pattern) | LIVE's PAT macro (A + D-pad); TRACK's PATTERN; SONG |
| TRACK: B + UP / DOWN (jump a beat) | TRACK's # column: A + UP / DOWN |
| TRACK: B + LEFT / RIGHT (pattern) | TRACK's PATTERN field: A + LEFT / RIGHT |
| SYNTH: B + LEFT / RIGHT (channel) | SYNTH's tab row: LEFT / RIGHT |

## 5. The system menu

**speaker** (auto / on / off: mono with a low-mid lift; auto turns it on when headphones are unplugged), **tilt** (on: tilting rides the delay send), **crank bite** (on: with the crank out, how open the filter is sets CORROSION's mutation amount). The Simulator silences the game while the menu is open, so CODE and the demos are in FILE.
