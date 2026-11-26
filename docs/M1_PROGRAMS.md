# M1:s program i FM1B-banken (2026-11-25)

`tools/m1_sf2.py --patches` gör M1:s fabriksprogram (`tools/m1_syx.py`, docs/M1_ROM.md "Programdata") till SoundFont-presets, som
fm1-sf2:s `make_bank.js` och `pack_bank.js` sedan gör till banken. Det FM1B kan uttrycka finns i fm1-sf2:s FORMAT.md: volymenvelope
(attack, hold, decay, sustain, release), ett lågpass med modulationsenvelope och mod-LFO till cutoff, vibrato-LFO, pan, nivå,
reverb- och chorus-send, lager (upp till fyra zoner per not) och trumkittar i bank 128.

## Banken

- Bank 0, program 0..99: M1:s 100 program. Single = OSC 1. Double = OSC 1 och OSC 2 som två lager på varje tangent. Drum = den
  trumkitt som OSC 1:s fält pekar på (0..3).
- Bank 128, program 0..3: trumkittarna (globaldatans fyra, den tomma utelämnas). MIDI-kanal 10 spelar dem; programbyte där väljer kitt.
- M1:s kittar följer inte GM: kitt 1 har Kick 1 på tangent 24.

## Mappning

| M1 | SF2 / FM1B | Anmärkning |
| --- | --- | --- |
| VDA-EG: attack time/level, decay, break point, slope, sustain, release | attack, decay, sustain, release | decay och slope blir en decay; stiger nivån efter attacken räknas decay till attacken |
| VDA oscillator level × EG-topp | initialAttenuation | |
| VDF cutoff 0..99 | initialFilterFc 5000..13500 cent (ca 150 Hz..20 kHz) | linjärt i cent: uppskattat |
| VDF EG intensity × VDF-EG | modEnvToFilterFc och modulationsenvelopen | intensitet 99 × nivå 99 flyttar cutoff 99 steg; den lägsta nivån (negativ sustain) blir envelopens botten. Är release level högre än sustain (filtret öppnar när tangenten släpps) läggs sustain mitt emellan och releasen håller den nivån |
| Pitch-EG: start level → attack level (attack time) → 0 (decay time) | modEnvToPitch, ±99 = ±1200 cent (manualen s. 24) | glider från den största nivån till 0; release time och release level ignoreras. Modulationsenvelopen delas med filtret: har oscillatorn filter-EG vinner pitch-EG:n bara från nivå 20 (Timp&Bells, StringRise, HellsBells, Surprise!!), och filtret står då på sin sustain-cutoff. 15 oscillatorer får pitch-EG |
| attack level under break point | attack | når attacken minst halva toppen (Piano 90 → 99) räknas stigningen inte till attacken |
| VDA velocity sense (−99..99) | modulationspostens offset 40 (fm1-m1:s eget fält) | nivån = (1 − \|s\|/99) + \|s\|/99 · v² (s > 0) eller · (1 − v²) (s < 0), v = velocity/127, högst −36 dB |
| VDF EG intensity velocity sense (−99..99) | offset 42 | filter-EG:ns djup gånger 1 − s/99 · (1 − v) (s > 0) eller 1 − \|s\|/99 · v (s < 0) |
| Pitch MG (wave, freq, delay, intensity, OSC 1/2 på) | vibrato-LFO | alltid triangel; intensitet kvadratiskt till högst 1200 cent |
| VDF MG | mod-LFO till cutoff (intensitet × 48 cent) | alltid triangel |
| oktav, intervall, detune | coarseTune, fineTune (detune på OSC 2) | |
| effekt 1/2: typ (1–33) och effektandel per kanal (byte 2–5, 0–100) | reverb- eller chorus-send = 1,5 × andel × typens vikt | se nedan |
| trumkitt: ljud, tangent, pan, stämning, nivå | zon per tangent | stämning −120..120 tolkad som tiondels halvtoner (inte verifierat); decay ignoreras |

Tiderna 0..99 blir `0,002 + 0,457 × (1,021^p − 1)` sekunder (11 ≈ 0,12 s, 33 ≈ 0,45 s, 62 ≈ 1,2 s, 99 ≈ 3,1 s), och decay och
release får 4,5 gånger det som SF2-tid (100 dB). Kurvan är anpassad till fabriksexemplen (roms/ref/, utanför git): släppen i Strings
(R33), Choir (R44) och Lore (R62) faller 50, 35 och 17 dB/s i exemplen och 49, 30 och 15 dB/s i vår rendering; decay i Vibes (D89)
och A.Bass (D75) ligger på 10–13 dB/s i båda. MG-frekvensen `0,06 × 1,06^p` Hz är fortfarande en uppskattning.

## Effekter

Effektlistan är M1 Owner's Manual s. 40–53, effektbytena tabell *11 (s. 129). **I programdatan är typen manualens nummer minus 1**
(0 = Hall): parametrarna visar det (typ 0 har reverbens layout, 9 och 10 delayens, 17 tremolo i Vibes, 24 rotary i Organ 1).
Byte 2–5 är effektandelen (DRY:EFF, 0–100) per kanal för effekt 1 och 2.

| Datatyp (manualens) | blir | vikt |
| --- | --- | --- |
| 0–5 (1–6) hall, room, live stage | reverb-send | 1,0 |
| 6–8 (7–9) early reflection | reverb-send | 0,6 |
| 9 (10) stereo delay, 10 (11) cross delay, 28 (29) delay/delay | delay (fm1base/m1_fx.c) | |
| 11–12 (12–13) chorus, 13–14 (14–15) flanger, 15–16 (16–17) phaser, 23 (24) symphonic ensemble, 24 (25) rotary | chorus-send | 1,0 |
| 25–27 (26–28) delay/hall, delay/room, delay/early reflection | delay och reverb-send | 0,8 |
| 29–30 (30–31) delay/chorus, delay/flanger | delay och chorus-send | 1,0 |
| 31 (32) delay/phaser, 32 (33) delay/tremolo | delay, och chorus-send 0,5 för phasern | |
| 17–22 (18–23) tremolo, EQ, overdrive, distortion, exciter | inget | |

Sendarna är vikten × effektandelen × 1,5. Chorusens masternivå är 127 (fm1base/app.c; fm1-sf2:s standard 30), så att andelen
bestämmer mängden: E.Piano 3 (chorus 50 %) har då sida/mitt −4,9 dB och korrelation V/H 0,51 mot fabriksexemplets −4,6 dB och 0,49
(med masternivå 30: −11,9 dB och 0,88). Chorusens form följer programmet (fm1base/m1_fx.c, sf2fx_chorus_shape): hastighet enligt tabell *11-3-2
(0..99 = 0,03..3,00 Hz, 100..199 = 3,1..13 Hz, 200..216 = 14..30 Hz), delay ur parametern (chorus 0..200 ms, flanger 0..50 ms;
phaser 1 ms, delay/chorus 5 ms) och svep = djupet/99 × 90 % av delayen. E.Piano 3: 0,30 Hz, 5 ms, ±4,5 ms. Utan chorus i
programmet: fm1-sf2:s fasta 16 ms ±6,8 ms vid 0,8 Hz. Hur M1:s djup 0..99 motsvarar millisekunder är inte känt. Delayen ligger på hela utgången efter motorns mix (M1:s effekt gäller hela
programmet) med det senast spelade programmets tider (0–500 ms vänster och höger), feedback (−99..99 %, korsad för cross delay),
high damp (lågpass i återkopplingen) och effektandel (torrt × (100 − andel) + delay × andel). Den går i 22 050 Hz med 43 KB buffert.
23 program har delay. Reverbtid, chorus-/flangerparametrar, EQ, effektplaceringen (serie/parallell) och tremolo, overdrive,
distortion och exciter används inte.

## Inte med

Pitch-EG:ns release och de små pitch-EG:erna på oscillatorer med filter-EG, VDF:s tangentföljning, velocity mot EG-tider, EG-tidernas tangentföljning,
aftertouch och joystick (förutom pitch bend och modhjulets vibrato, som spelaren har), mono och hold, delay start, kombinationerna och
M1:s egna effektalgoritmer.

## Uppmätt på FM-1

Med den nya tidskurvan (691 604 B packad): ackord 56 % CPU, 39 programbyten 57 % (topp 58 %, 26 röster, 18 röster kapade och 128
gånger utan plats i cachen eftersom släppen nu ringer längre), trummor 14 %, 40 toner samtidigt 79 % med CPU-vakten som sänkte
gränsen till 37 röster och ett missat ljudblock. Inga tappade noter (`python tools/m1_measure.py`).

Röstgränsen är 32 (`FM1B_VOICES` i fm1base/m1_cfg.h, motorns standard 40): med 40 toner samtidigt 69 % CPU, 32 röster, inga
missade ljudblock och ingen CPU-vakt som behövde sänka gränsen (2026-11-26).

Med delayen (2026-11-26): ackord 59 %, 39 programbyten 65 % (topp 66 %), trummor 14 %, 40 toner 71 %, inga missade ljudblock och
inga tappade noter.

## Mixnivå och klippning (2026-11-26)

Motorns mix klippte vid full skala (sat24 i sf2fx.c och sf2out.c): sf2out håller bara den torra signalen under -3 dB, och chorus,
reverb och delay läggs på efteråt. E.Piano 3:s ackord C-E-G klippte i hela attacken. Tre åtgärder:

- **Zonens nivå sänks som M1:s DRY:EFF** (torrt × (1 - andel)): chorus- och delayandelen och halva reverbandelen; sendet följer med.
  E.Piano 3 får 6,9 dB lägre nivå (tools/m1_sf2.py, `fx_atten`).
- **Mixen får 12 dB marginal** (sat24 klampar vid 2^25 i stället för 2^23).
- **Slutlimiter, lookahead** (fm1base/m1_fx.c) före och efter delayen: ljudet fördröjs ett block (128 bildrutor, 2,9 ms) och gainet går
  linjärt över blocket till det mål som både blocket och nästa block tål (tak -1 dB), så toppen aldrig möter ett högre gain. Släpp:
  högst 16/2048 per block (cirka 0,2 s för 6 dB). En första version sänkte gainet på ett enda prov vid första toppen, och motorns
  egen limiter (sf2out, tak -3 dB) gjorde likadant; en stegvis gainändring mitt i en period är klippning och hördes som knaster i
  anslaget. sf2out:s tak är nu +12 dB (den ingriper inte), och limitern i app.c håller taket.
- **VDF:s tangentföljning** (manualen s. 27): ±99 = cutoff följer tonhöjden 1:1, noll vid center key (MIDI-ton); motorn lägger
  (ton - center) × fältet till cutoff när tonen startar (modulationsposten är 60 byte).

Anslagsmätning på hosten: ackord med velocity 110 på alla 100 program, jämfört med samma rendering utan limiter efter att en långsam
gain (10 ms) passats in. Avvikelsen är snabb gainmodulation: median SNR i de första 150 ms 18,4 dB med de gamla limitrarna och 40,7 dB
med lookahead; de sämsta 10 % -3,5 dB mot 25,4 dB (Nimbus, Sitar 1, HellsBells och Hold var värst). Latensen ökar med cirka 5,8 ms.

Kontroll på hosten: ett fyrtonsackord vid velocity 110 på alla 100 program ger global topp -1 dB, inga över taket. På FM-1 (med lookahead): ackord
60 %, programbyten 67 %, trummor 15 %, 40 toner 72 %, inga missade ljudblock och inga tappade noter.

## Chorusen mot originalet (2026-11-26)

Knaster i anslaget som kvarstod efter limiterrättningen. Jag provade fem varianter av E.Piano 3 (en sak avstängd i taget): alla knastrade,
mörkare filter minst. Det var inte klippning, limitrar, 64-bitsfelet, interpolation (4-punkts gav oförändrade mätvärden) eller aliasing
(DWGS E.P.1 är bandbegränsad för F3-F5). Chorusen ger en kamfiltersvängning på cirka 13 Hz i 2-4 kHz-bandet: standardavvikelsen över
150-550 ms efter anslaget är 0,8 dB torr, 3,75 dB med chorus och 2,8 dB (median, 1,1-4,6) i fabriksexemplet. Åtgärder:

- Chorusens svep 67,5 % av delayen i stället för 90 % (fm1base/m1_fx.c). Mätvärdet ändrades knappt: kamfiltrets djup följer
  blandningen torrt/chorus, inte sveppets längd.
- Chorusens masternivå 95 i stället för 127 (fm1base/app.c): svängningen 2,9 dB. Stereobredden ligger då något under fabriksexemplets.

Obekräftat: om knastret är borta. Kvar som möjliga orsaker: kodningen (BFP8 på 8 bitar, MP3 64 kbit/s med 20 dB SNR; jämförelsen
mot en förlustfri bank blev inte användbar).

## Orsaken till knastret vid hårda anslag (2026-11-26)

Distorsionen vid hårt tillslag och i ackord var **16-bitsomslag i effektkedjan**, inte filter, interpolation eller kodning. Jag hittade den
genom att spela samma E.Piano 3-ackord vid kanalvolym 127, 118 ... 64 på hosten: vid 64-118 var energin utanför ackordets övertoner
-31 dB, vid 127 -10,6 dB, och den ändrades inte när limitern slogs av. Övertonerna låg med 65 Hz mellanrum (differenstoner mellan C, E
och G), som en olinjäritet ger. Panelens tangenter spelar alltid på nivå 127. Tre ställen:

- **sf2out.c, resamplern** (32 -> 44,1 kHz): `l[i] = (int16_t)(stereo[2 * i] >> 8)` castade utan begränsning. Kommentaren sa "Q23
  (saturated)", men det stämde bara tills mixen fick 12 dB marginal (sat24 vid 2^25). Allt över full skala lindades. Nu går mixen genom
  en **lookahead-limiter per prov** (`look_limit`, 64 prov = 2 ms framför, tak -1 dB, release 1/32768 per prov, cirka 0,5 s för 6 dB):
  gainet följer en rak linje som når taket för varje prov i fönstret, så det faller före toppen och aldrig stegar. En binding-post
  håller reda på den brantaste linjen, så fönstret skannas bara när den lämnat det (CPU: ackord 61 mot 60 %).
- **sf2fx.c, chorusens ringbuffert**: `(int16_t)(cho[i] >> 8)` lindades när chorus-bussen gick över full skala. Nu `>> 10` (2 bitars
  marginal) och `>> 5` ut.
- Limitern i fm1base/m1_fx.c (före delayen) är borttagen; efter delayen finns kvar den blockbaserade (ekona kan summera över taket).

Före/efter vid kanalvolym 127, E.Piano 3 C-E-G vid velocity 127: -10,6 dB -> -29,7 dB icke-harmonisk energi. Alla 100 program: ackord vid
velocity 110, global topp -1 dB, inget över taket. Två fel i första versionen av limitern hittades med instrumentering: fönstrets
`look_t` startade på 0 (gainet föll till golvet vid start) och räknaren `look_pending` underflödade; båda rättade.

## Trummor och korta loopar (2026-11-26)

- **Trummorna spelas klart** efter tangent upp: programmen Drums #1-3 har decay 96 men release 1, vilket bara ger mening om ljudet lever
  efter att tangenten släppts; release är nu 3 s (`DRUM_RELEASE_S`), både för programmen och för trumkittarna på kanal 10. Det är en
  slutsats av programdatan, inte verifierat i manualen.
- **Ingen choke.** Kittarnas data har sju byte per tangent (ljud, tangent, pan, stämning, nivå, decay) och inget exklusivt-fält. Hi-hatens
  längd styrs av **decay** (kit 2:s öppna hi-hat har -22 och -99). Decay skalas nu som 60 timecent per steg på decay-tiden (skalan är
  inte verifierad). Om en riktig M1 ändå stänger en öppen hi-hat med en stängd vet jag inte.
- **Korta loopar i packningen.** Packad (MP3) mot opackad (förlustfri) bank, uthållna toner 0,8-2,0 s efter start, extra energi mot den
  förlustfria: Organ 2 från G#4, A.Bass E4-E5, Kalimba och Pan Flutes diskant skilde sig 6-25 dB. Packningen kodar en kort loop (24-160
  sampel) som MP3 och repeterar den, och en MP3-kodad kort loop slingar inte sömlöst: "dålig samplingloop senare in i ljudet". Nu
  hålls de 94 ljud med loop under 130 sampel och loopstart under 3000 sampel förlustfria (`pack_bank.js --keep s...`, 81 KB). Organ 2
  -28,9 till -59,5 dB, A.Bass -27,1 till -46,4 dB, Kalimba -23,6 till -35,5 dB; MP3-ljudens medel-SNR 19,9 till 19,6 dB. Bankens storlek
  693 080 B. Alla 110 korta loopar förlustfria ryms inte (167 KB mer).

## FM-1 Setup: röstgräns (2026-11-26)

M1:s icke-standardinställningar ligger skilda från M1:s egna menyer, som fm1-d50:s Setup-sida (issue #22). Modellen (vilka värden som
finns, gränser, lagring, tillämpning) ligger i fm1base/m1_setup.h; sidan ritas i app.c (`m1_setup_draw`), så när LCD-grafiken kommer byts
bara den funktionen ut.

- **EDIT** går in i och ur sidan. **K1** flyttar fältet, **K2** (eller PRESET) ändrar värdet, **SAVE** sparar det med övriga inställningar.
- **Röstgräns** 1-32 (standard 32). Den sparas i settings_t:s `setup`-byte som 32 minus antalet, så att en äldre post (0) är standard.
  Motorn tar den som tak för CPU-vakten (`sf2out_voice_cap`): vakten sänker gränsen vid hög belastning men höjer den aldrig över taket.
  En Double-ton tar två röster (M1 har 16 röster totalt).
- Nästa fält läggs till i `m1_setup_fields` och i bitkartan i m1_setup.h (reserverade: bit 5-7).
- Host-test: fm1base/tests/m1_setup.txt (värde, SAVE, omstart) och m1_cap.txt (16 toner med tak 8 ger 8 röster).

## Effekter som firmware gör själv (2026-11-26)

Motorn har reverb och chorus, m1_fx.c delay. Resten av de effekttyper som fabriksprogrammen använder görs i fm1base/m1_fx2.c, på hela
utgången efter delayen, för det program den senast startade tonen hör till:

| Effekt (datatyp) | Program (panelnummer) | Hur |
| --- | --- | --- |
| Distortion (21) | 15 DistGuitar | förstärkning 4x upp till 125x efter drive, hård klippning, enpolig lågpass (5 kHz), utnivå |
| Overdrive (20) | 75 PickGuitar | förstärkning upp till 40x, mjuk mättning, lågpass (7 kHz), utnivå (+14 dB: nivån matchar före-renderingen, 55,1 mot 55,2 dB) |
| Stereo tremolo 1 (17) | 16 Vibes | amplitudmodulering, vänster och höger i motfas (typ 17) eller samma fas (18), sinus eller triangel, hastighet enligt tabell *11-3-2 |
| Rotary speaker (24) | 68 Organ 1 | delningsfilter 800 Hz: tutan med amplitudmodulering, Doppler och panorering (0,9-2,7 Hz), trumman 0,7 Hz |
| Exciter (22) | 10, 24, 28, 41, 44, 60, 63, 64, 69, 73, 83, 100 | hörn 1,2-9 kHz, mättade höga frekvenser läggs till (blend -99..99) |
| Flanger, cross flanger (13, 14) | 56, 62, 71, 78, 84, 97, 14, 79 | modulerad delay (grundtid 1-40 ms, ±djup × 90 %), feedback -99..99, cross matar kanalerna korsvis |
| Symphonic ensemble (23) | 19, 21, 39, 48, 82, 91 | tre modulerade delayer (12 ms ±5 ms), blandade till vänster och höger |

Programmens effekter slås upp i en tabell sist i banken (tools/m1_fxtable.py, 20 byte per program, 2 008 B); varje zon bär programnumret i
modulationsposten (64 byte). Banken är 695 128 B av 696 288. Phaser, EQ och delay/tremolo används av inget fabriksprogram och saknas.
Effekternas egna EQ (parametrarna 6 och 7) används inte. Skalorna för djup, hastighet, drive och utnivå är uppskattade; manualen anger
dem inte. Flanger och symphonic ersätter den chorus som tidigare stod i deras ställe.

**Före och efter:** roms/fx_demo/before/ (innan) och roms/fx_demo/after/ (med effekterna): samma programmen, ett fyrtonsackord i 2,5 s och en
kort melodi, en fil per program och effekt (distortion, overdrive, tremolo, rotary, exciter, flanger, symphonic). Filerna ligger i roms/
(ignoreras av git) eftersom de innehåller ljud ur din ROM.

Uppmätt på FM-1 med effekterna: ackord 62 %, programbyten 68 %, trummor 17 %, 40 toner 74 %, och 8-tonsackord på de tio effektprogrammen
79 % (högst 32 röster, inga missade ljudblock, inga tappade noter). DistGuitar inspelad från enheten matchar datorn (crest 2,4 mot 1,4 dB).
