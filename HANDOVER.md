# Handover: combinations, from the cloud session to a local branch (2026-11-27)

Branch `claude/awesome-hopper-kiw8nd`, draft PR [mwedmark/fm1-m1#2](https://github.com/mwedmark/fm1-m1/pull/2). Nothing is merged. Nothing has run on an FM-1 or against real dumps.

## What exists

Combinations play as up to eight timbres on the engine's channels 0..7 (docs/M1_COMBINATIONS.md has the design):

| Part | File |
| --- | --- |
| Router and set-up (key/velocity ranges, transpose, level, pan, Multi by MIDI channel, held notes across a select) | `fm1base/m1_combi.c`, `m1_combi.h` |
| App integration: PLAY toggles PROGRAM/COMBI, PRESET and MIDI program change select, screen shows `C01` and the name, `app_expect` for host tests | `fm1base/app.c`, `app.h`, `fm1.toml` |
| Table builder: puts `M1CB` in the bank in front of the effect table `M1FX` | `tools/m1_combi.py` |
| Tests | `tools/tests/test_m1_combi.py` (+ `combi_test.c`), `tools/tests/make_combi_host_test.sh` (+ `fm1base/tests/m1_combi.txt`) |
| Status and next steps | `STATUS.md` |

Table layout is in `tools/m1_combi.py`'s docstring. `m1_fx2.c` still finds its own table at the very end; `m1_combi_init` walks back over both.

## Verified (synthetic data only)

- `python -I tools/tests/test_m1_combi.py`: 5 tests, all pass (table builder, and the router against a stub engine).
- `tools/tests/make_combi_host_test.sh`: all checks pass in the host simulator (fm1-sf2's default bank plus a synthetic table). It overwrites `roms/out/m1.fm1b`, and refuses a real one without `FORCE=1`.
- `fm1base/tests/play.txt` still passes (program mode unchanged).

## Not verified

- **The real dump.** `m1_syx.py`'s `combination()` offsets (timbre block at byte 36, 11 bytes each) were not checked against a real `Int_M1.syx`.
- **Room in the bank.** 1 160 B are left (695 128 of 696 288). The table is roughly 100 × (12 + 10 × timbres) + 200 B, a few KB. `pack_bank.js --flash` must be lowered by the size `m1_combi.py` prints; the real size is unknown.
- **The device build.** Only the host simulator was built in the cloud (`tools/build.py ../fm1-m1/fm1base --host` in fm1-base). The `.fwsc` was not built.
- **On the FM-1:** CPU and cache with eight timbres per key (32-voice cap; 40 notes measured 72 % before), glitches from 64-bit multiplications, sound against the MP3 examples.

## Guesses to check

- Pan 0..15 → CC 10 linearly (0 left, 15 right). The INST bit (bit 7 of the pan byte) is stored and ignored.
- Level 0..99 → CC 7 0..127.
- Key and velocity ranges are inclusive, `bottom..top`; no wrap if bottom > top.
- Program numbers above 99 (card bank) leave the timbre silent.

## Not done

1. The combination's own effects (the heard effect is the last started program's, `m1_fx2.c`). `m1_combi.py` drops them.
2. Detune per channel (stored, not applied; the engine would need RPN 1).
3. Control filter and output assignments.
4. Saving the mode and the selected number (the device starts in program mode).
5. The sequencer: an open question in `STATUS.md`; the SEQ button is kept free.

## Steps on the local branch

```bash
git fetch origin claude/awesome-hopper-kiw8nd
git switch claude/awesome-hopper-kiw8nd
python3 -I tools/tests/test_m1_combi.py
```

1. Decode the real combinations (your dumps stay outside git):

   ```bash
   python3 -I tools/m1_syx.py "M1 files/MPC-00/Int_M1.syx" --out roms/gen/m1_patches.json
   ```

   Check that the printout shows 100 combinations, and look at a few in the JSON: names, types, program numbers under 100, sensible key ranges.
2. Run the bank chain from the README up to `m1_fxtable.py`, then add the combinations. The tool prints the table size:

   ```bash
   python3 -I tools/m1_combi.py roms/gen/m1_patches.json roms/out/m1.fm1b
   ```

   If the bank no longer fits, repeat `pack_bank.js` with `--flash` lowered by that size (the effect table's 2 008 B are already counted in the current budget; confirm against the final size).
3. Build and flash as in README ("Measured on the FM-1"), then press PLAY. PRESET steps through the combinations.
4. Run `python tools/m1_measure.py` with a Layer combination held on several keys, and compare with the earlier numbers.
5. Listen against the factory examples (README, "Reference to compare against"). Fix the pan scale first if it sounds wrong.

If a combination misbehaves, `tools/tests/make_combi_host_test.sh` style scripts (`fm1base/tests/*.txt`) are the fastest way to reproduce it in the host simulator: `midi`, `key`, `turn PRESET`, `tap PLAY`, and `expect chN_voices`, `chN_program`, `chN_volume`, `chN_pan`, `combi`, `voices`.

## Notes

- Files in this repository use CRLF in `README.md`, `app.c`, `fm1.toml` and `docs/RELEASE-0.1.md`, and LF elsewhere. Keep each file's endings, or the diff becomes the whole file.
- `roms/`, `M1 files/` and `fm1base/build/` are git-ignored. `roms/out/m1.fm1b` in the cloud container is only a stand-in made from fm1-sf2's default bank; it holds no Korg data.
