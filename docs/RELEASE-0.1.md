## fm1-m1 0.1: a Korg M1 on the FM-1, played from your own ROM dump

**BYOR: Bring Your Own ROM. The M1's sound is Korg's.** Its PCM ROMs, its operating system ROM and its factory program bank are not in this
repository, not on the install page, not in the emulator and not in any release, and never will be. You make the data file from your own M1 on your own
computer ([INSTALL.md](../INSTALL.md)). The install page and the emulator read your file in your browser; nothing is uploaded.

This is the first public release, 0.1: a first version to try and to report back on, not a finished M1. It plays the M1's 100 factory programs and three drum kits from the keys and MIDI, with the M1's own program data
(envelopes, filters, vibrato, velocity, effects), on an M-VAVE FM-1. Without your data file the screen says `NO M1 DATA`.

### What to listen for in 0.1

The sound was checked against recordings of a real Korg M1 ([Synthmania's M1 page](https://www.synthmania.com/Korg%20M1/): its factory program demos), by ear and by
measurement: envelope times, release slopes, chorus width and speed, and the level of every program.

- **The factory programs, as programs.** Each program is built from your own factory bank dump: its multisound (a Double program plays both oscillators), octave,
  interval and detune, the VDA and VDF envelopes (the time scale is fitted to the demos: Strings, Choir and Lore release at 50, 35 and 17 dB per second, as the M1 does),
  the pitch EG, the filter's keyboard tracking, vibrato and filter MG, and **velocity**: VDA velocity sense and the VDF EG's velocity sense follow each program's own values.
- **Effects.** The M1's reverbs and early reflections, **delay** (stereo, cross, and the delay halves of the combination types), **chorus** with each program's own speed, depth and delay,
  and now **flanger, cross flanger, symphonic ensemble, tremolo, rotary speaker, exciter, overdrive and distortion**. Dist Guitar is distorted, Organ 1 turns, Vibes tremble. The mix follows the
  M1's DRY:EFF balance, so a program with 50 % chorus is not louder than one without. Phaser, EQ and delay/tremolo are not used by any factory program and are not made.
- **Drums ring out.** A drum program keeps playing when you let go of the key (the factory drum programs have a long decay and a one-step release, which only makes sense that way).
  The kits' per-key **decay** shortens an open hi-hat as on the M1; there is no choke in the kits' data, so a closed hi-hat does not stop an open one.
- **Loops that loop.** The bank is packed to fit the FM-1's flash (most sounds as MP3 at 64 kbit/s, decoded as you play), but a short loop cannot be an MP3: the 94 sounds with loops
  under 130 samples that start early stay lossless. Organ 2, A.Bass and Kalimba hold their notes without the bad loop they had.
- **No distortion at a hard strike.** A loud chord at full velocity used to wrap around in two 16-bit stages of the effects chain and sound like crackle at the attack. The mix now goes through a
  lookahead limiter with a ceiling at -1 dB that lowers the gain before a peak and never steps it.
- **Chorus as the M1's.** A program's chorus has its own speed, delay and depth and the width of the M1's recording (E.Piano 3: 0.3 Hz, side to mid -4.5 dB against -4.6 dB on the demo).

### Playing it

- **Keys:** F3 to C5 on the FM-1's twenty keys, with velocity. **OCT-** and **OCT+** move them from -3 to +3 octaves (Kick 1 of the M1's kits is on key 24, so the lowest octave is there for the drums).
- **PRESET** knob: the program, 001 to 100 (the three drum kits are programs on MIDI channel 10, and 101 and up if your bank has more).
- **MIDI in:** all channels, program change, velocity, pitch bend, modulation, volume, pan, sustain; channel 10 plays the drum kits. Over USB the FM-1 also sends what its keys play.
- **FM-1 Setup** (**EDIT** opens and closes it; **K1** chooses the value, **K2** changes it, **SAVE** keeps it): the **voice cap**, 1 to 32 (32 by default). The CPU guard lowers the voice limit when the
  load is high and never raises it above your cap. The M1 has 16 voices; a Double program takes two per note.

### Known limits

- **Combinations are not made yet** (the 100 combinations of the M1's factory bank are in your bank dump and are not used). The M1's sequencer is not made either.
- The scales for effect depth, rotary speed and the drive of distortion and overdrive are estimates: the manual does not give them. The effects' own EQ is not used.
- Velocity does not shorten the envelope times, and the pitch EG's glide on release is not made.
- Some samples still differ from the M1's: three notes of Organ 2, three of A.Bass and five of Pan Flute are more than 30 dB from the lossless bank's at the same note. Pan Flute's lowest zone has its root at C#5, so low notes play three octaves down; that is how the M1's table is, as far as it was read.
- The flash holds 695 128 of 696 288 bytes of data: the bank is full. A bigger bank needs a harder packing.
- It was measured on one FM-1, not several: with the effects, a chord of eight notes is at 79 % of the CPU at 32 voices, ordinary chords at 62 %, and 40 notes at once at 74 %, with no missed audio block and no lost note.

### How the bank is made

`tools/` reads your dumps and writes the bank (`INSTALL.md`): the PCM chips merged and read as 12-bit samples at 31 250 Hz, the multisound, zone and sample tables read out of the M1's operating
system ROM (v1.9), and the programs and drum kits out of your factory SysEx dump. The sample engine is [fm1-sf2](https://github.com/mwedmark/fm1-sf2)'s (FM1B, with its live cache), on
[fm1-base](https://github.com/mwedmark/fm1-base). [docs/M1_ROM.md](../docs/M1_ROM.md) and [docs/M1_PROGRAMS.md](../docs/M1_PROGRAMS.md) say what was measured and what is estimated.
