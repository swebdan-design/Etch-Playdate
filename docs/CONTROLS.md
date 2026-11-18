# ETCH CRANK — the button map

ETCH CRANK — by Synth Centric — for Playdate

The Playdate has a D-pad, A, B and the crank. It has no SELECT and no START, and Menu and Lock belong to the system. ETCH's grammar is kept exactly. **Held B stands in for SELECT**, and the crank is for performing, never for scrolling menus.

✓ = built in milestone 1. Everything else is the plan for later milestones and still open for your review (see "Conflicts").

## 1. Everywhere

| Buttons | Action | ETCH equivalent | M1 |
|---|---|---|:-:|
| **crank** | Filter cutoff: the live offset over the stored value. Relative, 540° closed to open, hard stops at both ends | (new) | ✓ |
| **B + crank** | Resonance | (new) | ✓ |
| **dock the crank** | Cutoff and resonance glide back to the stored values over one beat | (new) | ✓ |
| **B + dock** | Keep the live cutoff and resonance as the stored values | (new) | ✓ |
| **B + DOWN** | Play / stop | START | ✓ |
| **B + UP** | Arm crank recording (one pass from the next bar into a motion lane) | (new) | message only |
| **B + LEFT / RIGHT** | Previous / next screen: SYNTH ← TRACK ← LIVE → GEN → SONG → FILE | SELECT + LEFT / RIGHT | message only (one screen in M1) |
| **B** (tap) | On LIVE: FILL on / off (latched). Elsewhere: back / cancel | ETCH LIVE's B = SELECT + START | ✓ (FILL) |
| A hold on a value + crank | Ride that value live | (new) | |
| A hold on a scene + crank | Morph towards that scene | (new) | |

**What counts as a B tap:** B is pressed and released with no other button and no crank movement in between. So B + crank, B + dock and B + D-pad never toggle the fill.

## 2. The editing grammar (every screen, unchanged from ETCH)

| Buttons | Meaning |
|---|---|
| D-pad | Move the cursor |
| A (tap) | Place / toggle / do. On an empty step A writes the last note; on a filled one it changes it |
| A held + LEFT / RIGHT | Value −1 / +1 (a semitone on a note) |
| A held + UP / DOWN | Value coarse (an octave on a note) |
| B + A | Delete, or reset to default |

On list screens, plain LEFT / RIGHT also changes the value, as in ETCH.

## 3. Where the displaced ETCH combos go

Making held B into SELECT collides with two families of ETCH controls: the SELECT combos B can't also mean, and ETCH's own B + D-pad uses. Nothing is dropped. Each one moves to a place ETCH already uses for the same kind of thing:

| ETCH | Its job | On ETCH CRANK | Why there |
|---|---|---|---|
| SELECT + A | Evolve at the next bar | LIVE's **EVOLVE** pad (A), and GEN's EVOLVE | B + A must stay "delete" (grammar) |
| SELECT + B | Undo (one level) | LIVE's **UNDO** pad (A), and GEN's UNDO | B + B is not a chord |
| SELECT + START | FILL latch | **B tap on LIVE** (ETCH already has it) | unchanged |
| SELECT + UP / DOWN | Queue the next / previous pattern | the PATTERN readout in LIVE's status band: **A + UP / DOWN** (value grammar); and SONG | B + UP / DOWN are taken by REC and play |
| TRACK: B + UP / DOWN | Jump a beat | TRACK's step column (#): **LEFT / RIGHT** jump a beat | B + UP / DOWN are taken |
| TRACK: B + LEFT / RIGHT | Previous / next pattern | TRACK's header PATTERN field: **A + LEFT / RIGHT** | B + LEFT / RIGHT change screens |
| SYNTH: B + LEFT / RIGHT | Previous / next channel | the channel tab row: **LEFT / RIGHT** (ETCH already allows this) | as above |

## 4. The system menu

Up to three custom items. The plan was **CODE**, **DEMOS** and **SPEAKER / TILT**.

* **Milestone 1 has one temporary item, `cpu test`** (a checkbox). It runs ETCH DS's CPU test (see the report) so the meter can be read on hardware. It goes away when the real items arrive.
* **Finding:** in the Simulator (SDK 3.1.2), ETCH CRANK's audio stops while the system menu is open (report §3). By the rule in the brief, **CODE entry moves into the app** (FILE). I recommend moving DEMOS into FILE as well, and using the menu for **SPEAKER** and **TILT**, which are settings, not performance.

## 5. Conflicts to confirm

1. **Evolve / undo shortcuts.** With B + A kept as delete, ETCH's global SELECT + A / SELECT + B shortcuts survive only as pads. That costs one cursor move, and LIVE is home, so I think it's acceptable. The alternative would be a new chord such as A + B, which the brief rules out ("don't invent other schemes").
2. **Screen wrap.** ETCH (Game Boy) stops at the ends of the screen row; ETCH DS's series spec v1 (an unmerged branch) says it wraps. ETCH CRANK will follow whichever is the series rule.
