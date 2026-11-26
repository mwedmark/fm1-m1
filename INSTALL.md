# Installing the M1 on an FM-1

There are two ways to use the M1 firmware, and both start by making the M1's data from your own M1:

1. **In your browser**, with no FM-1 at all.
2. **On your FM-1**, from the install page: the firmware and your data in one go.

> **BYOR: Bring Your Own ROM. The M1's sound is Korg's.** Its PCM ROMs, its operating system ROM and its factory program bank are not in this repository, not on the install
> page, not in the emulator and not in any release, and never will be. You make the data from your own M1 (the ROM dumps and the SysEx dump of its factory
> programs), on your own computer. The install page and the emulator read your file in your browser; nothing is uploaded.

## Step 1: make your data file

You need:

- Your M1's **PCM ROMs** (IC36 to IC43, 512 KB each) and **operating system ROM** (IC23 and IC32, rev 1.9), dumped. `tools/m1_rom.py` merges the PCM chips and checks them
  against CRC-32s you give it (`--expect`); [docs/M1_ROM.md](docs/M1_ROM.md) says how the data is laid out.
- A **SysEx dump of the factory programs** (`.syx`, the all-programs, all-combinations and global dumps of the M1, which hold the programs and the drum kits).
- Python 3, Node.js and ffmpeg, and checkouts of this repository, of [fm1-sf2](https://github.com/mwedmark/fm1-sf2) (its bank tools) and of [fm1-base](https://github.com/mwedmark/fm1-base) beside it.

From the root of the checkout, with your dumps in `M1 files/`, in this order (each writes to `roms/`, which git ignores; keep it so, and never share the files):

```bash
python3 -I tools/m1_rom.py "M1 files/korgm1p1" --out roms/gen/m1_pcm.bin
python3 -I tools/m1_os.py "M1 files/rev19-ic23-880219.bin" "M1 files/rev19-ic32-880319.bin" --pcm roms/gen/m1_pcm.bin --out roms/gen/m1_tables.json
python3 -I tools/m1_syx.py "M1 files/MPC-00/Int_M1.syx" --out roms/gen/m1_patches.json
python3 -I tools/m1_sf2.py roms/gen/m1_tables.json roms/gen/m1_pcm.bin roms/gen/m1.sf2 --patches roms/gen/m1_patches.json
node tools/fm1b_make_bank.js bfp8 32000 roms/gen/m1.sf2 roms/gen/m1_bfp8.fm1b
node ../fm1-sf2/pack_bank.js roms/gen/m1_bfp8.fm1b roms/gen/m1_packed.fm1b --kbit 64 --flash 696288 --keep "$(python3 -I tools/m1_keep.py roms/gen/m1_bfp8.fm1b)"
python3 -I tools/m1_fxtable.py roms/gen/m1_patches.json roms/gen/m1_packed.fm1b roms/out/m1.fm1b
```

The last command writes `roms/out/m1.fm1b`: **that is your data file** (about 695 KB). The file names in `M1 files/` are those of the dumps this was made with; use yours.
[README.md](README.md) (in Swedish) has the same steps with the details of each tool.

## Step 2a: try it in the browser

Open the emulator page. It is the firmware running in an emulator. Switch it on, press **Load M1 data** and pick your `m1.fm1b`. The data stays in your browser.
Without it the screen says `NO M1 DATA`.

## Step 2b: put it on your FM-1

You need an M-VAVE FM-1, a USB cable straight to the computer (no hub), and Chrome or Edge.

1. Open the install page.
2. Under **Your M1 data**, press **Choose your file** and pick your `m1.fm1b`. The page checks that it is an FM1B bank.
3. Press install. The page writes the firmware, then your data. The FM-1 restarts and shows the program number and name.

Installing the firmware again keeps your data in the FM-1; a new data file replaces it (the FM-1 restarts on the new bank by itself).

## Building the firmware yourself

```bash
python3 tools/build.py ../fm1-m1/fm1base --no-ble            # in fm1-base, beside this repository
```

With `roms/out/m1.fm1b` in place the package has your data in it (for your own FM-1 only: never publish it); without it the package is the firmware only, as the release's.
`tools/build.py ... --release 0.1` makes a release package, and `--host` and `--web` the host simulator and the browser emulator. The build was made with fm1-base at
the commit named in the release notes.
