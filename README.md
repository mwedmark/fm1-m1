# fm1-m1: a Korg M1-like synth on the M-VAVE FM-1 (draft)

**BYOR: bring your own ROM.** The repository contains no Korg data, and neither will releases or install pages.
You dump the PCM ROM and programs from your own M1 yourself and turn them into a sound bank on your own computer.
Keep the dumps outside the repository, or in `roms/`, which `.gitignore` should exclude.

## Architecture (2026-11-25)

- **Platform: fm1-base**, like fm1-d50 (`fm1base/`: `fm1.toml`, `app.h`, `app.c`). Built in the fm1-base checkout beside the repository,
  in the container `fm1b`:

  ```powershell
  docker exec fm1b sh -c "cd /git/fm1-base && JIELI_DOCKER=0 python3 tools/build.py ../fm1-m1/fm1base --no-ble"
  ```

  `--host` builds the host simulator, and `fm1base/tests/play.txt` is a first smoke test (WAV files and screenshots in `fm1base/build/test`).
- **Sample playback: fm1-sf2's engine** (`engine/`, copied from fm1-sf2 5a4f267, GPL-3.0-only with linking exception): FM1B banks,
  zones, loops, envelopes, filter, LFO, layers (Double), 16 MIDI channels (Combination), reverb and chorus. It runs at 32 kHz
  and is resampled to 44.1 kHz. Still from XIP, not RAM.
- **The bank lives in the resource region** (like fm1-d50's data): built from `roms/out/m1.fm1b`. Otherwise the install page or
  `fm1_resources.py` writes in your own file. Without a bank the screen shows `NO M1 DATA`.

### Room for samples, maxed out

`data_lo = 0x34000`: the app gets [0x4000, 0x34000), 192 KB (the image is 148 KB). The bank gets 0x34000..0xDE000, **696 288 B**
(fm1-d50 has 589 792). What is left to gain:

- Lower `data_lo` further when the app is finished and its size is known (every 4 KB sector goes to samples).
- Move the panel's calibration from 0xDE000 so that the bank reaches 0xE0000: +8 KB.
- XDATA1 (0xE5000..0xFC000, 92 KB) as a second piece: requires that the bank can be split over two windows, because the loader's staging
  0xE0000..0xE5000 lies in between.

### The M1's PCM and OS tables

Measured on the dumps, in detail in [docs/M1_ROM.md](docs/M1_ROM.md): 12-bit samples at 31 250 Hz, two banks of 1M samples each.
OS v1.9 has 100 multisounds (23 DWGS) with 469 zones in bank 0 and 44 drum sounds in bank 1. What is used is
**1 385 790 samples**: all of bank 0 (1 048 000) and 337 790 of bank 1.

What fits in 696 288 B (minus the bank's tables, a few KB):

| Codec (fm1-sf2) | bytes/sample | samples | share of 1 385 790 |
| --- | --- | --- | --- |
| 12-bit raw | 1.5 | 464 k | 33 % |
| BFP8 | 1.03 | 676 k | 49 % |
| SEA6 | 0.84 | 831 k | 60 % |
| SEA5 | 0.71 | 977 k | 70 % |
| Live cache (MP3, decoded to SRAM) | ~0.25 | all | 100 % (lossy, needs CPU and RAM) |

Everything in SEA5 needs about 990 KB. About 290 KB is missing, so something has to give: a lowered `data_lo`, XDATA1, shorter tails on long
one-shot sounds, a lower sample rate for dark samples, SEA5 for most and live cache for the rest.

## Tools

`tools/m1_rom.py`: checks IC36–IC43 (8 × 512 KB), writes CRC32/SHA-1 and merges them. The default layout `m1` is the one measured
above (int16, 2M samples). The other layouts and the guess remain for other dumps.

```powershell
python -I tools/m1_rom.py "M1 files\korgm1p1" --out roms\gen\m1_pcm.bin
```

With `--expect known_crc.json` the tool compares against CRC32s that you have filled in yourself, for example
`{"ic36": "1234abcd", "merged": "..."}`. The Plus1 files (U201–U204, `plus-1_*`) are skipped, because they are not an original M1.

`tools/m1_os.py`: reads multisounds, zones, sample records and drum sounds from the OS EPROMs (IC23 high, IC32 low) to JSON, checks
the loop seams against the PCM and can write each multisound and drum sound as a WAV to listen to:

```powershell
python -I tools/m1_os.py "M1 files\rev19-ic23-880219.bin" "M1 files\rev19-ic32-880319.bin" --pcm roms\gen\m1_pcm.bin --out roms\gen\m1_tables.json --wav roms\gen\ms
```

`tools/m1_lcd_emu.py`: runs the OS EPROMs (an NEC V50, so x86 plus the V20 bit instructions) in Unicorn and reads out what the OS writes to the 40 × 2 LCD,
to build the M1's exact screens from (docs/M1_LCD.md). Needs `pip install unicorn`:

```powershell
python -I tools/m1_lcd_emu.py "M1 files\rev19-ic23-880219.bin" "M1 files\rev19-ic32-880319.bin" --json roms\gen\lcd.json
```

## From dump to bank (live cache)

The bank is a packed FM1B (fm1-sf2's live cache): most sounds as an MP3 stream at 64 kbit/s in flash, decoded into a cache
of 192 KB in fm1-base's pool when a note needs them. Short loops and DWGS cycles are stored losslessly. The whole M1 (495 sounds: 100 multisounds
and 44 drum sounds) fits: 677 704 B of 696 288.

```powershell
python -I tools/m1_syx.py "M1 files\MPC-00\Int_M1.syx" --out roms\gen\m1_patches.json
```

```powershell
python -I tools/m1_sf2.py roms\gen\m1_tables.json roms\gen\m1_pcm.bin roms\gen\m1.sf2 --patches roms\gen\m1_patches.json
```

```powershell
node tools\fm1b_make_bank.js bfp8 32000 roms\gen\m1.sf2 roms\gen\m1_bfp8.fm1b
```

```powershell
node ..\fm1-sf2\pack_bank.js roms\gen\m1_bfp8.fm1b roms\out\m1.fm1b --kbit 64 --flash 696288 --keep "<sounds with a short loop and an early loop start, see docs/M1_PROGRAMS.md>"
```

```powershell
python -I tools/m1_fxtable.py roms\gen\m1_patches.json roms\gen\m1_packed.fm1b roms\out\m1.fm1b
```

`tools/fm1b_make_bank.js` is fm1-sf2's `make_bank.js` with the M1's velocity in the modulation record (44 bytes); it reads the
converter from `../fm1-sf2/index.html` and changes it in memory, so fm1-sf2 is untouched. The engine in `engine/` reads the fields.

Then the firmware build embeds `roms/out/m1.fm1b` as the resource region. Bank 0, programs 0–99 are the M1's factory programs, bank 128 the
four drum kits (MIDI channel 10). How the programs become presets, and what is approximated: [docs/M1_PROGRAMS.md](docs/M1_PROGRAMS.md).
Without `--patches` the bank becomes the raw multisounds instead.
The bank contains Korg's samples from your dump: it does not belong in git or online.

## Try it in the emulators

- **fm1-base's web emulator** (the app as WebAssembly): `tools/build.py ../fm1-m1/fm1base --web` in the container, with
  `source /root/emsdk/emsdk_env.sh` first. Serve the repository and open `fm1base/build/emu/index.html`, switch on the emulator and pick your
  `roms/out/m1.fm1b` with "Load M1 data". It is saved in the browser.
- **fm1-emu** (the device's own binary, `m1.fwsc`): the emulator's files (MIT) are copied from `../fm1-d50/emu` to `emu/` (ignored),
  with `m1.fwsc`, `m1.flash` and `m1.script`. The overlay is made with
  `node ../fm1-sf2/make_flash_overlay.js fm1base/build/m1-res.bin emu/m1.flash 0x34000`. Serve `emu/` and open
  `index_tap.html?fw=m1&flash=m1.flash&mhz=360&script=m1.script`.

In fm1-emu the firmware starts and binds the packed bank. The page's "load" is the computer's load for the emulation, not the firmware's load.

## Measured on the FM-1 (2026-11-25)

Installed with fm1-base's `tools/fm1_install.py fm1base/build/m1.fwsc --port "<current port>" --yes`. If the resource step does not find
the port (it changes its name to "M1 FM-1"): `tools/fm1_resources.py fm1base/build/m1-res.bin --port "M1 FM-1"`. Measured with
`python tools/m1_measure.py`, which plays over USB-MIDI and reads out the firmware (app.c `app_sysex`). 360 MHz, one core, the engine from XIP:

| Test | CPU (highest average) | Voices | Missed blocks | Dropped notes | Cache |
| --- | --- | --- | --- | --- | --- |
| Idle | 13 % | 0 | 0 | 0 | |
| Chords, 8 notes, programs 0/31/5 | 20 % | 10 | 0 | 0 | 6 misses |
| 39 program changes with chords | 18 % | 8 | 0 | 0 | 56 misses, 515 MP3 frames |
| 44 drum sounds | 12 % | 2 | 0 | 0 | 6 misses |
| 40 notes of Strings at once | 47 % | 40 | 0 | 0 | 3 misses |

Not listened to yet: the sound quality with MP3 at 64 kbit/s.

## Reference to compare against

The M1's 100 factory programs as MP3 examples: https://www.synthmania.com/Korg%20M1/Audio/Factory%20preset%20examples/Programs/
(to listen to and compare against once the program engine exists, not as data in the bank).

## Open questions

- fm1-base warns about 64-bit multiplications next to loads (felucca-bsp docs/CPU.md: can give errors at 360 MHz) in app_render, voice_play,
  filter_run and others. fm1-sf2 heard occasional glitches from it at 360 MHz. Listen for it, and move them out of line if they are heard.
- The converter `m1_tables.json` + PCM → FM1B bank (fm1-sf2 FORMAT.md).
- The factory programs exist as `.syx` (`M1 files/MPC-00/Int_M1.syx`) and are decoded by `tools/m1_syx.py` (docs/M1_ROM.md, "Program data").
  The programs are presets in the bank (docs/M1_PROGRAMS.md); times and levels are estimated and should be compared against the MP3 examples.
  `plus-1_prog_card.bin` is the Plus-1 card's own programs in the same format: the program engine can be built and tried against it.
- The combinations (100 of them) are decoded but not used.
- Zone bytes 3, 6 and 7, bank 1 below 0xA3CFC and the low chips' lower nibbles (docs/M1_ROM.md, "Not finished").

`src/` and `include/` are a first draft of an engine of our own from before fm1-sf2 was chosen. They are not used and should be removed.
