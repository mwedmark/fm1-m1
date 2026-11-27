# Status (2026-11-27)

Release 0.1 plays the 100 programs, the 4 drum kits and the effects (docs/RELEASE-0.1.md). Since then: **combinations** (docs/M1_COMBINATIONS.md),
built and tested without Korg data and without an FM-1.

## Can be done in the cloud (without a device)

- Everything in the host simulator: `tools/build.py ../fm1-m1/fm1base --host` in a fm1-base checkout beside this repository, with `roms/out/m1.fm1b` present
  (a stand-in is enough: `tools/tests/make_combi_host_test.sh` makes one from fm1-sf2's default bank).
- Unit tests: `python -I tools/tests/test_m1_combi.py`.
- The converter tools need the user's dumps (`M1 files/`, outside git): upload them to the session when needed, they are not in the container.

## Needs the user's dumps (BYOR)

- Run the whole chain with the real combinations: `m1_syx.py` → `m1_combi.py`, and check the size against the bank's room.
- The open items of docs/M1_ROM.md ("Not finished"): zone bytes 3, 6 and 7, bank 1 below 0xA3CFC, the low chips' lower nibbles.
- Listening: compare the combinations and programs against the MP3 examples.

## Needs the FM-1

- Sound quality of the 64 kbit/s MP3 bank, glitches from the 64-bit multiplications at 360 MHz (README, "Open questions").
- CPU and cache with eight timbres per note (a combination is up to eight voices per key).

## Next, in order

1. Combinations: save the mode and number; the combination's own effects; per-channel detune (RPN 1 in the engine); check pan scale and INST on a real dump.
2. Bank room: the combination table (a few KB) has to come out of the samples; settle `--flash` once the real size is known.
3. **Sequencer: an open question.** The M1 has an 8-track sequencer (from memory: 8 tracks, 10 songs, about 7 700 notes; the manual has the figures)
   that the 0.1 release does not have. The FM-1 already has USB-MIDI to a computer's DAW, so the value is mainly a standalone use. Decide whether it is
   wanted, and if so what: play back a Standard MIDI File from the device's storage, record on the panel, or a copy of the M1's own screens.
   The SEQ button is kept free for it.
4. Clean-up: README's note on `src/` and `include/` (they are ignored and not in the repository), the open-questions list.
