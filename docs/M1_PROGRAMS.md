# The M1's programs in the FM1B bank (2026-11-25)

`tools/m1_sf2.py --patches` turns the M1's factory programs (`tools/m1_syx.py`, docs/M1_ROM.md "Program data") into SoundFont presets, which
fm1-sf2's `make_bank.js` and `pack_bank.js` then turn into the bank. What FM1B can express is in fm1-sf2's FORMAT.md: volume envelope
(attack, hold, decay, sustain, release), a low-pass with modulation envelope and mod LFO to cutoff, vibrato LFO, pan, level,
reverb and chorus sends, layers (up to four zones per note) and drum kits in bank 128.

## The bank

- Bank 0, programs 0..99: the M1's 100 programs. Single = OSC 1. Double = OSC 1 and OSC 2 as two layers on every key. Drum = the
  drum kit that OSC 1's field points to (0..3).
- Bank 128, programs 0..3: the drum kits (the global data's four, the empty one is left out). MIDI channel 10 plays them; a program change there selects the kit.
- The M1's kits do not follow GM: kit 1 has Kick 1 on key 24.

## Mapping

| M1 | SF2 / FM1B | Note |
| --- | --- | --- |
| VDA EG: attack time/level, decay, break point, slope, sustain, release | attack, decay, sustain, release | decay and slope become one decay; if the level rises after the attack, decay is counted into the attack |
| VDA oscillator level × EG peak | initialAttenuation | |
| VDF cutoff 0..99 | initialFilterFc 5000..13500 cents (about 150 Hz..20 kHz) | linear in cents: estimated |
| VDF EG intensity × VDF EG | modEnvToFilterFc and the modulation envelope | intensity 99 × level 99 moves the cutoff 99 steps; the lowest level (negative sustain) becomes the envelope's bottom. If the release level is higher than sustain (the filter opens when the key is released) sustain is placed in between and the release holds that level |
| Pitch EG: start level → attack level (attack time) → 0 (decay time) | modEnvToPitch, ±99 = ±1200 cents (the manual p. 24) | glides from the largest level to 0; release time and release level are ignored. The modulation envelope is shared with the filter: if the oscillator has a filter EG the pitch EG only wins from level 20 (Timp&Bells, StringRise, HellsBells, Surprise!!), and the filter then stays at its sustain cutoff. 15 oscillators get a pitch EG |
| attack level below break point | attack | if the attack reaches at least half the peak (Piano 90 → 99) the rise is not counted into the attack |
| VDA velocity sense (−99..99) | the modulation record's offset 40 (fm1-m1's own field) | level = (1 − \|s\|/99) + \|s\|/99 · v² (s > 0) or · (1 − v²) (s < 0), v = velocity/127, at most −36 dB |
| VDF EG intensity velocity sense (−99..99) | offset 42 | the filter EG's depth times 1 − s/99 · (1 − v) (s > 0) or 1 − \|s\|/99 · v (s < 0) |
| Pitch MG (wave, freq, delay, intensity, OSC 1/2 on) | vibrato LFO | always triangle; intensity quadratic up to at most 1200 cents |
| VDF MG | mod LFO to cutoff (intensity × 48 cents) | always triangle |
| octave, interval, detune | coarseTune, fineTune (detune on OSC 2) | |
| effect 1/2: type (1–33) and effect share per channel (bytes 2–5, 0–100) | reverb or chorus send = 1.5 × share × the type's weight | see below |
| drum kit: sound, key, pan, tuning, level | a zone per key | tuning −120..120 interpreted as tenths of semitones (not verified); decay ignored |

The times 0..99 become `0.002 + 0.457 × (1.021^p − 1)` seconds (11 ≈ 0.12 s, 33 ≈ 0.45 s, 62 ≈ 1.2 s, 99 ≈ 3.1 s), and decay and
release get 4.5 times that as SF2 time (100 dB). The curve is fitted to the factory examples (roms/ref/, outside git): the releases in Strings
(R33), Choir (R44) and Lore (R62) fall 50, 35 and 17 dB/s in the examples and 49, 30 and 15 dB/s in our rendering; decay in Vibes (D89)
and A.Bass (D75) is at 10–13 dB/s in both. The MG frequency `0.06 × 1.06^p` Hz is still an estimate.

## Effects

The effect list is M1 Owner's Manual p. 40–53, the effect bytes table *11 (p. 129). **In the program data the type is the manual's number minus 1**
(0 = Hall): the parameters show it (type 0 has the reverb's layout, 9 and 10 the delay's, 17 tremolo in Vibes, 24 rotary in Organ 1).
Bytes 2–5 are the effect share (DRY:EFF, 0–100) per channel for effect 1 and 2.

| Data type (the manual's) | becomes | weight |
| --- | --- | --- |
| 0–5 (1–6) hall, room, live stage | reverb send | 1.0 |
| 6–8 (7–9) early reflection | reverb send | 0.6 |
| 9 (10) stereo delay, 10 (11) cross delay, 28 (29) delay/delay | delay (fm1base/m1_fx.c) | |
| 11–12 (12–13) chorus, 13–14 (14–15) flanger, 15–16 (16–17) phaser, 23 (24) symphonic ensemble, 24 (25) rotary | chorus send | 1.0 |
| 25–27 (26–28) delay/hall, delay/room, delay/early reflection | delay and reverb send | 0.8 |
| 29–30 (30–31) delay/chorus, delay/flanger | delay and chorus send | 1.0 |
| 31 (32) delay/phaser, 32 (33) delay/tremolo | delay, and chorus send 0.5 for the phaser | |
| 17–22 (18–23) tremolo, EQ, overdrive, distortion, exciter | nothing | |

The sends are the weight × the effect share × 1.5. The chorus's master level is 127 (fm1base/app.c; fm1-sf2's default 30), so that the share
determines the amount: E.Piano 3 (chorus 50 %) then has side/mid −4.9 dB and L/R correlation 0.51 against the factory example's −4.6 dB and 0.49
(with master level 30: −11.9 dB and 0.88). The chorus's shape follows the program (fm1base/m1_fx.c, sf2fx_chorus_shape): rate per table *11-3-2
(0..99 = 0.03..3.00 Hz, 100..199 = 3.1..13 Hz, 200..216 = 14..30 Hz), delay from the parameter (chorus 0..200 ms, flanger 0..50 ms;
phaser 1 ms, delay/chorus 5 ms) and sweep = depth/99 × 90 % of the delay. E.Piano 3: 0.30 Hz, 5 ms, ±4.5 ms. Without chorus in the
program: fm1-sf2's fixed 16 ms ±6.8 ms at 0.8 Hz. How the M1's depth 0..99 corresponds to milliseconds is not known. The delay is on the whole output after the engine's mix (the M1's effect applies to the whole
program) with the most recently played program's times (0–500 ms left and right), feedback (−99..99 %, crossed for cross delay),
high damp (low-pass in the feedback) and effect share (dry × (100 − share) + delay × share). It runs at 22 050 Hz with a 43 KB buffer.
23 programs have a delay. Reverb time, chorus/flanger parameters, EQ, the effect placement (serial/parallel) and tremolo, overdrive,
distortion and exciter are not used.

## Not included

The pitch EG's release and the small pitch EGs on oscillators with a filter EG, VDF's key tracking, velocity to EG times, the EG times' key tracking,
aftertouch and joystick (except pitch bend and the mod wheel's vibrato, which the player has), mono and hold, delay start, the combinations and
the M1's own effect algorithms.

## Measured on the FM-1

With the new time curve (691 604 B packed): chords 56 % CPU, 39 program changes 57 % (peak 58 %, 26 voices, 18 voices cut and 128
times without room in the cache because the releases now ring longer), drums 14 %, 40 notes at once 79 % with the CPU guard lowering
the limit to 37 voices and one missed audio block. No dropped notes (`python tools/m1_measure.py`).

The voice limit is 32 (`FM1B_VOICES` in fm1base/m1_cfg.h, the engine's default 40): with 40 notes at once 69 % CPU, 32 voices, no
missed audio blocks and no CPU guard that needed to lower the limit (2026-11-26).

With the delay (2026-11-26): chords 59 %, 39 program changes 65 % (peak 66 %), drums 14 %, 40 notes 71 %, no missed audio blocks and
no dropped notes.

## Mix level and clipping (2026-11-26)

The engine's mix clipped at full scale (sat24 in sf2fx.c and sf2out.c): sf2out only keeps the dry signal under -3 dB, and chorus,
reverb and delay are added afterwards. E.Piano 3's chord C-E-G clipped throughout the attack. Three measures:

- **The zone's level is lowered like the M1's DRY:EFF** (dry × (1 - share)): the chorus and delay share and half the reverb share; the send follows.
  E.Piano 3 gets a level 6.9 dB lower (tools/m1_sf2.py, `fx_atten`).
- **The mix gets 12 dB of headroom** (sat24 clamps at 2^25 instead of 2^23).
- **Final limiter, lookahead** (fm1base/m1_fx.c) before and after the delay: the sound is delayed one block (128 frames, 2.9 ms) and the gain moves
  linearly across the block to the target that both the block and the next block can bear (ceiling -1 dB), so the peak never meets a higher gain. Release:
  at most 16/2048 per block (about 0.2 s for 6 dB). A first version cut the gain on a single sample at the first peak, and the engine's
  own limiter (sf2out, ceiling -3 dB) did the same; a stepwise gain change in the middle of a period is clipping and was heard as crackle in the
  attack. sf2out's ceiling is now +12 dB (it does not intervene), and the limiter in app.c holds the ceiling.
- **VDF's key tracking** (the manual p. 27): ±99 = the cutoff follows the pitch 1:1, zero at the center key (MIDI note); the engine adds
  (note - center) × the field to the cutoff when the note starts (the modulation record is 60 bytes).

Attack measurement on the host: chords at velocity 110 on all 100 programs, compared with the same rendering without limiter after a slow
gain (10 ms) has been fitted. The deviation is fast gain modulation: median SNR in the first 150 ms 18.4 dB with the old limiters and 40.7 dB
with lookahead; the worst 10 % -3.5 dB against 25.4 dB (Nimbus, Sitar 1, HellsBells and Hold were worst). The latency increases by about 5.8 ms.

Check on the host: a four-note chord at velocity 110 on all 100 programs gives a global peak of -1 dB, none over the ceiling. On the FM-1 (with lookahead): chords
60 %, program changes 67 %, drums 15 %, 40 notes 72 %, no missed audio blocks and no dropped notes.

## The chorus against the original (2026-11-26)

Crackle in the attack that remained after the limiter fix. I tried five variants of E.Piano 3 (one thing switched off at a time): all crackled,
a darker filter least. It was not clipping, limiters, the 64-bit error, interpolation (4-point gave unchanged measurements) or aliasing
(DWGS E.P.1 is band-limited for F3-F5). The chorus gives a comb-filter swing of about 13 Hz in the 2-4 kHz band: the standard deviation over
150-550 ms after the attack is 0.8 dB dry, 3.75 dB with chorus and 2.8 dB (median, 1.1-4.6) in the factory example. Measures:

- The chorus's sweep 67.5 % of the delay instead of 90 % (fm1base/m1_fx.c). The measurement barely changed: the comb filter's depth follows
  the dry/chorus mix, not the length of the sweep.
- The chorus's master level 95 instead of 127 (fm1base/app.c): the swing 2.9 dB. The stereo width is then slightly below the factory example's.

Unconfirmed: whether the crackle is gone. Remaining as possible causes: the encoding (BFP8 at 8 bits, MP3 at 64 kbit/s with 20 dB SNR; the comparison
against a lossless bank was not usable).

## The cause of the crackle on hard attacks (2026-11-26)

The distortion on hard strikes and in chords was **16-bit wraparound in the effect chain**, not filter, interpolation or encoding. I found it
by playing the same E.Piano 3 chord at channel volume 127, 118 ... 64 on the host: at 64-118 the energy outside the chord's harmonics was
-31 dB, at 127 -10.6 dB, and it did not change when the limiter was switched off. The harmonics were spaced 65 Hz apart (difference tones between C, E
and G), as a nonlinearity gives. The panel's keys always play at level 127. Three places:

- **sf2out.c, the resampler** (32 -> 44.1 kHz): `l[i] = (int16_t)(stereo[2 * i] >> 8)` cast without limiting. The comment said "Q23
  (saturated)", but that was only true until the mix got 12 dB of headroom (sat24 at 2^25). Everything above full scale wrapped. Now the mix goes through
  a **per-sample lookahead limiter** (`look_limit`, 64 samples = 2 ms ahead, ceiling -1 dB, release 1/32768 per sample, about 0.5 s for 6 dB):
  the gain follows a straight line that reaches the ceiling for every sample in the window, so it falls before the peak and never steps. A binding
  point keeps track of the steepest line, so the window is only scanned when it has left it (CPU: chords 61 against 60 %).
- **sf2fx.c, the chorus's ring buffer**: `(int16_t)(cho[i] >> 8)` wrapped when the chorus bus went over full scale. Now `>> 10` (2 bits of
  headroom) and `>> 5` out.
- The limiter in fm1base/m1_fx.c (before the delay) is removed; after the delay the block-based one remains (the echoes can sum over the ceiling).

Before/after at channel volume 127, E.Piano 3 C-E-G at velocity 127: -10.6 dB -> -29.7 dB of non-harmonic energy. All 100 programs: chords at
velocity 110, global peak -1 dB, nothing over the ceiling. Two errors in the first version of the limiter were found with instrumentation: the window's
`look_t` started at 0 (the gain fell to the floor at start) and the counter `look_pending` underflowed; both fixed.

## Drums and short loops (2026-11-26)

- **The drums play to the end** after key up: the programs Drums #1-3 have decay 96 but release 1, which only makes sense if the sound lives
  on after the key is released; release is now 3 s (`DRUM_RELEASE_S`), both for the programs and for the drum kits on channel 10. It is a
  conclusion from the program data, not verified in the manual.
- **No choke.** The kits' data has seven bytes per key (sound, key, pan, tuning, level, decay) and no exclusive field. The hi-hat's
  length is controlled by **decay** (kit 2's open hi-hat has -22 and -99). Decay is now scaled as 60 timecents per step on the decay time (the scale is
  not verified). Whether a real M1 closes an open hi-hat with a closed one anyway I do not know.
- **Short loops in the packing.** Packed (MP3) against unpacked (lossless) bank, sustained notes 0.8-2.0 s after the start, extra energy against the
  lossless one: Organ 2 from G#4, A.Bass E4-E5, Kalimba and Pan Flute's treble differed by 6-25 dB. The packing encodes a short loop (24-160
  samples) as MP3 and repeats it, and an MP3-encoded short loop does not loop seamlessly: "bad sample loop later in the sound". Now
  the 94 sounds with a loop under 130 samples and a loop start under 3000 samples are kept lossless (`pack_bank.js --keep s...`, 81 KB). Organ 2
  -28.9 to -59.5 dB, A.Bass -27.1 to -46.4 dB, Kalimba -23.6 to -35.5 dB; the MP3 sounds' mean SNR 19.9 to 19.6 dB. The bank's size
  693 080 B. All 110 short loops lossless do not fit (167 KB more).

## FM-1 Setup: voice limit (2026-11-26)

The M1's non-standard settings are kept apart from the M1's own menus, like fm1-d50's Setup page (issue #22). The model (which values
exist, limits, storage, application) is in fm1base/m1_setup.h; the page is drawn in app.c (`m1_setup_draw`), so when the LCD graphics arrive only
that function is replaced.

- **EDIT** enters and leaves the page. **K1** moves the field, **K2** (or PRESET) changes the value, **SAVE** saves it with the other settings.
- **Voice limit** 1-32 (default 32). It is stored in settings_t's `setup` byte as 32 minus the count, so that an older record (0) is the default.
  The engine takes it as the ceiling for the CPU guard (`sf2out_voice_cap`): the guard lowers the limit under high load but never raises it above the ceiling.
  A Double note takes two voices (the M1 has 16 voices in total).
- The next field is added to `m1_setup_fields` and to the bit map in m1_setup.h (reserved: bits 5-7).
- Host tests: fm1base/tests/m1_setup.txt (value, SAVE, restart) and m1_cap.txt (16 notes with a ceiling of 8 give 8 voices).

## Effects that the firmware makes itself (2026-11-26)

The engine has reverb and chorus, m1_fx.c a delay. The rest of the effect types that the factory programs use are made in fm1base/m1_fx2.c, on the whole
output after the delay, for the program that the most recently started note belongs to:

| Effect (data type) | Program (panel number) | How |
| --- | --- | --- |
| Distortion (21) | 15 DistGuitar | gain 4x up to 125x by drive, hard clipping, one-pole low-pass (5 kHz), output level |
| Overdrive (20) | 75 PickGuitar | gain up to 40x, soft saturation, low-pass (7 kHz), output level (+14 dB: the level matches the pre-rendering, 55.1 against 55.2 dB) |
| Stereo tremolo 1 (17) | 16 Vibes | amplitude modulation, left and right in antiphase (type 17) or in phase (18), sine or triangle, rate per table *11-3-2 |
| Rotary speaker (24) | 68 Organ 1 | crossover filter 800 Hz: the horn with amplitude modulation, Doppler and panning (0.9-2.7 Hz), the drum 0.7 Hz |
| Exciter (22) | 10, 24, 28, 41, 44, 60, 63, 64, 69, 73, 83, 100 | corner 1.2-9 kHz, saturated high frequencies are added (blend -99..99) |
| Flanger, cross flanger (13, 14) | 56, 62, 71, 78, 84, 97, 14, 79 | modulated delay (base time 1-40 ms, ±depth × 90 %), feedback -99..99, cross feeds the channels crosswise |
| Symphonic ensemble (23) | 19, 21, 39, 48, 82, 91 | three modulated delays (12 ms ±5 ms), mixed to left and right |

The programs' effects are looked up in a table at the end of the bank (tools/m1_fxtable.py, 20 bytes per program, 2 008 B); each zone carries the program number in the
modulation record (64 bytes). The bank is 695 128 B of 696 288. Phaser, EQ and delay/tremolo are used by no factory program and are missing.
The effects' own EQ (parameters 6 and 7) is not used. The scales for depth, rate, drive and output level are estimated; the manual does not give
them. Flanger and symphonic replace the chorus that previously stood in their place.

**Before and after:** roms/fx_demo/before/ (before) and roms/fx_demo/after/ (with the effects): the same programs, a four-note chord for 2.5 s and a
short melody, one file per program and effect (distortion, overdrive, tremolo, rotary, exciter, flanger, symphonic). The files are in roms/
(ignored by git) because they contain sound from your ROM.

Measured on the FM-1 with the effects: chords 62 %, program changes 68 %, drums 17 %, 40 notes 74 %, and 8-note chords on the ten effect programs
79 % (at most 32 voices, no missed audio blocks, no dropped notes). DistGuitar recorded from the device matches the computer (crest 2.4 against 1.4 dB).
