# Korg M1: PCM and OS tables, measured (2026-11-25)

Everything here is measured on the user's own dumps (`M1 files/`, outside git) with `tools/m1_rom.py` and `tools/m1_os.py`. Nothing is
taken from Korg's code or documentation. What is uncertain is marked as such.

## PCM (IC36–IC43, 8 × 512 KB)

- **Samples**: 12 bits = the high chip's byte (signed) << 4 | the low chip's upper nibble. The pairs are (IC40,IC36), (IC41,IC37),
  (IC42,IC38), (IC43,IC39), in that order and in sequence: sample *k* in the pair is byte *k* in both chips.
  `m1_rom.py --out` writes all 2 097 152 samples as int16 LE (`<< 4` more: high << 8 | low & 0xF0).
- The low chip's lower nibble does not improve any sample. In IC36 it is 0 in 86 % of the bytes. What it carries is unknown.
- **Sample rate: 31 250 Hz.** Measured on loops with a harmonic tone (Bottles, Choir, Pan Flute, Voices): 31.1–31.3 kHz
  with the zones' fine tuning.
- **Two banks of 1M samples each**, bank 0 = IC40/41 + IC36/37, bank 1 = IC42/43 + IC38/39. The addresses in the sample records are 20 bits
  and bit 20 in the end address selects the bank. The PCM is one sequence across both: bank 0 ends in the middle of Strings (zone 2), and Strings zone 3
  through Lore, the percussive sounds and the drums are in bank 1 (0x000002..0xFEC85). The loops are seamless in both banks with that
  interpretation (median 0.16 and 0.23 of a normal step). Tubular 1–4 and Bell Ring among the drum sounds are in bank 0.
- OS v1.9 uses all of bank 0 and bank 1 up to 0xFEC85.

## OS EPROM (IC23 high, IC32 low, 2 × 64 KB)

The image is 128 KB with the low chip at even addresses. `m1_os.py` finds the tables by the first name ("Piano"), not at fixed
addresses. In v1.9 (rev19) and the M1R they are in a segment with base 0x175C0 in the image. The pointers below are offsets in that segment.

### Multisounds: 100 records of 14 bytes (image 0x1DF38)

| byte | field |
| --- | --- |
| 0–1 | u16 pointer to the first zone |
| 2 | type: 0 PCM, 2 DWGS |
| 3 | number of zones |
| 4–13 | name, 10 characters |

Index 0–76 are PCM and 77–99 DWGS (DWGS E.P.1 .. DWGS Sine). The zone table ends where the name table begins.

### Zones: 10 bytes (0x5E06..0x6978, 293 of them)

| byte | field |
| --- | --- |
| 0–1 | u16 pointer to the sample record |
| 2 | top key (MIDI) |
| 3 | always 5 (unknown) |
| 4 | fine tuning in cents, signed: added on playback |
| 5 | root note (MIDI) |
| 6 | signed, usually 0 (DWGS −15): probably level (not verified) |
| 7 | signed, usually 0: probably filter or level (not verified) |
| 8–9 | 0 |

The lowest zone can be a placeholder (Strings: top key 0).

### Sample records: 14 bytes (0x4C78..0x5E06, 321 of them)

| byte | field |
| --- | --- |
| 0–1 | 0 |
| 2–5 | start (bits 0–19) |
| 6–9 | loop start (bits 0–19), bit 26 (0x04 in byte 9) = loops; without it the sound is a one-shot |
| 10–13 | end, exclusive (bits 0–19), bit 20 = bank |

The loop is [loop start, end): after sample end−1 comes loop start. Loop start can lie before start: 2 samples before when the whole
sample loops (Choir, Voices, Lore), a whole cycle before in DWGS (start in the middle of the cycle) and a longer stretch in Rhythm.

### DWGS

The 23 DWGS multisounds share 8 zones and sample records 0–7. Wave *k* (multisound 77 + *k*) is in bank 0 from *k* × 0x3FC,
and the records are positions within that block: one and the same cycle in 512, 256, 128, 64, 32, 16, 8 and 4 samples (sum 0x3FC), one per
zone. Wave 23 ends where Piano begins (0x5BA6).

### Drum sounds: 44 names of 11 bytes (image 0x1E508), pointers to them at 0x1E4B2

Sound *n* (Kick 1 .. Metronome2) plays sample record 277 + *n*. They are the 44 consecutive records that no zone uses, and they read
bank 1. The drum kits' keys, tuning and level are in the global data (below, "Program data").

## Program data (SysEx)

The format is in the M1 Owner's Manual, "MIDI Exclusive Format" and tables 1–6 (p. 124–131). `tools/m1_syx.py` decodes a dump.
The factory data is the user's own dump `M1 files/MPC-00/Int_M1.syx` (outside git): three messages, `F0 42 3n 19 func 0m data F7`
where byte 5 is memory allocation and bank, and the data is Korg's 7-bit packing (one byte with the bit 7 of the following seven, then the seven).

| func | contents | size |
| --- | --- | --- |
| 4C | all programs | 100 × 143 bytes |
| 4D | all combinations | 100 × 124 bytes |
| 51 | global data: tuning, scales, 4 drum kits of 30 keys each (7 bytes: drum sound 1–44, key, pan, tuning, level, decay) | 861 bytes |

A program (table 1): name (0–9), mode Single/Double/Drum (10), mono/hold (11), OSC 1 and 2 multisound and octave (12–15),
interval, detune, delay start (16–18), pitch MG and VDF MG (19–26), aftertouch (27–31), joystick (32–37), effects (38–62), and per
oscillator (63–102 for OSC 1, the same for OSC 2 from 103): pitch EG, VDF (cutoff, key tracking, EG intensity), VDF EG with eight
parameters, VDA (level, key tracking, velocity) and VDA EG with seven. Signed values are two's complement in one byte.

Check against the dump: 70 Single, 27 Double and 3 Drum programs, all multisounds internal (0–99). Universe = Choir + Lore,
Piano 16' = Piano, Ooh/Ahh = Voices + Choir, BottleBell = Bottles + Bell. Drum kit 1 begins with Kick 1 on key 24.

## Not finished

- Zone bytes 3, 6 and 7.
- The low chips' lower nibbles.
