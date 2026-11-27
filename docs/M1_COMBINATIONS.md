# Combinations (2026-11-27)

A combination is up to eight **timbres**, each a program with its own level, pan, transpose, key range, velocity range and MIDI channel. The
factory bank has 100 (docs/M1_ROM.md, func 4D, 100 × 124 bytes); `tools/m1_syx.py` decodes them. Playing them is `fm1base/m1_combi.c`, the data
goes into the bank with `tools/m1_combi.py`.

## How it plays

Timbre *k* plays on engine channel *k* (0..7; channel 10, index 9, stays the drum kits'). On each channel the engine already has its own program,
volume (CC 7) and pan (CC 10), so a combination is **routing and set-up, not a new voice engine**:

- A note is checked against every timbre's key and velocity range (inclusive), transposed, and sent to each channel that takes it. The notes that
  sound are remembered, so a note off ends exactly them, also if the combination was changed meanwhile.
- Single, Layer, Split and Velocity Switch differ only in the ranges the data holds. **Multi**: a timbre only takes the MIDI channel it is set to. The
  panel's keys reach all timbres in every type.
- A select is applied step by step from the main loop (`m1_combi_service`), because the engine's queue holds 32 events and a combination of eight
  timbres needs about 32: nothing is lost, the service waits for room.
- Level 0..99 → CC 7 0..127. Pan 0..15 → CC 10, linear (0 left, 15 right). **The pan scale is a guess**, as is how the M1 treats INST (bit 7).

## On the FM-1

- **PLAY** switches PROGRAM ↔ COMBINATION mode (the M1's PROG and COMBI). SEQ is kept free for a sequencer.
- **PRESET** steps through the combinations; the screen shows `C01` and the name. A MIDI program change selects a combination too.
- Outside notes while a combination is active: channel 10 is still the drum kits; other channels go through the timbres. Controllers 7 and 10 are
  ignored (they are the combination's own), the wheel, pedal, sends and all-notes-off go to the timbres' channels; pitch bend too.
- Not saved yet: the mode and the selected number are not stored; the device starts in program mode.

## The table in the bank

`tools/m1_combi.py patches.json bank.fm1b` puts a table "M1CB" in the bank, **in front of** the effect table "M1FX" so that `m1_fx2.c` still finds its
own at the very end (`m1_combi_init` walks back over both). Layout in the tool's docstring. Timbres that are OFF are left out. The table takes
room from the samples: about 100 × (12 + 10 × timbres) + 200 B, a few KB. **The bank region has 1 160 B left** (695 128 of 696 288), so lower
`pack_bank.js --flash` by the size the tool prints, and check the build fits.

## Not done

- The combination's own effects. The effect that is heard is that of the program that started last (m1_fx2.c), the engine's reverb and chorus
  send come from each program. The data is in the table's source (`effects`) and is dropped by `m1_combi.py`.
- Detune: the engine has no fine tuning per channel (RPN 1 would be the way). It is stored in the table but not applied.
- The control filter (which controllers a timbre accepts) and output assignments.
- Voices: a combination of several timbres uses several voices per note. The voice limit (Setup, default 32) applies to all.
- Programs in the table above 99 (the card bank) are not in the bank: such a timbre is silent.

## Tested without Korg data and without a device

- `python -I tools/tests/test_m1_combi.py`: the table builder, and `fm1base/m1_combi.c` against a stub engine (`tools/tests/combi_test.c`): ranges, transpose,
  layers, multi channels, held notes across a select, a short queue, leaving. Synthetic data only.
- `tools/tests/make_combi_host_test.sh`: the host simulator with fm1-sf2's default bank and a synthetic table, running `fm1base/tests/m1_combi.txt`
  (enter COMBI mode, PRESET through all five types, MIDI channels, program change, a held note across a select, leave). It overwrites
  `roms/out/m1.fm1b` (refuses a real one without `FORCE=1`).

## Not tested

Everything with the real data: that `m1_syx.py`'s offsets for the timbre block give the right fields on a real dump (it is how the 100 were decoded before),
how the 100 factory combinations sound against the MP3 examples (https://www.synthmania.com/Korg%20M1/Audio/Factory%20preset%20examples/), CPU and cache with
eight timbres on the FM-1, the pan scale.
