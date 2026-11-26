# Korg M1: PCM och OS-tabeller, uppmätt (2026-11-25)

Allt här är uppmätt på användarens egna dumpar (`M1 files/`, utanför git) med `tools/m1_rom.py` och `tools/m1_os.py`. Inget är
hämtat från Korgs kod eller dokumentation. Det som är osäkert står som det.

## PCM (IC36–IC43, 8 × 512 KB)

- **Sampel**: 12 bitar = hög-chipets byte (med tecken) << 4 | låg-chipets övre halvbyte. Paren är (IC40,IC36), (IC41,IC37),
  (IC42,IC38), (IC43,IC39), i den ordningen och i följd: sampel *k* i paret är byte *k* i båda chipen.
  `m1_rom.py --out` skriver alla 2 097 152 sampel som int16 LE (`<< 4` till: hög << 8 | låg & 0xF0).
- Låg-chipens nedre halvbyte förbättrar inget sampel. I IC36 är den 0 i 86 % av bytena. Vad den bär är okänt.
- **Samplingsfrekvens: 31 250 Hz.** Uppmätt på loopar med harmonisk ton (Bottles, Choir, Pan Flute, Voices): 31,1–31,3 kHz
  med zonernas fininställning.
- **Två banker à 1M sampel**, bank 0 = IC40/41 + IC36/37, bank 1 = IC42/43 + IC38/39. Adresserna i sampelposterna är 20 bitar
  och bit 20 i slutadressen väljer bank. PCM:en är en följd över båda: bank 0 slutar mitt i Strings (zon 2), och Strings zon 3
  till Lore, slagljuden och trummorna ligger i bank 1 (0x000002..0xFEC85). Looparna är sömlösa i båda bankerna med den
  tolkningen (median 0,16 och 0,23 av ett normalsteg). Tubular 1–4 och Bell Ring bland trumljuden ligger i bank 0.
- OS v1.9 använder hela bank 0 och bank 1 upp till 0xFEC85.

## OS-EPROM (IC23 hög, IC32 låg, 2 × 64 KB)

Bilden är 128 KB med låg-chipet på jämna adresser. `m1_os.py` hittar tabellerna på det första namnet ("Piano"), inte på fasta
adresser. I v1.9 (rev19) och M1R ligger de i ett segment med bas 0x175C0 i bilden. Pekarna nedan är offset i det segmentet.

### Multisounds: 100 poster à 14 byte (bild 0x1DF38)

| byte | fält |
| --- | --- |
| 0–1 | u16 pekare till den första zonen |
| 2 | typ: 0 PCM, 2 DWGS |
| 3 | antal zoner |
| 4–13 | namn, 10 tecken |

Index 0–76 är PCM och 77–99 DWGS (DWGS E.P.1 .. DWGS Sine). Zontabellen slutar där namntabellen börjar.

### Zoner: 10 byte (0x5E06..0x6978, 293 st)

| byte | fält |
| --- | --- |
| 0–1 | u16 pekare till sampelposten |
| 2 | översta tangent (MIDI) |
| 3 | alltid 5 (okänt) |
| 4 | fininställning i cent, med tecken: läggs till vid uppspelning |
| 5 | rotnot (MIDI) |
| 6 | med tecken, oftast 0 (DWGS −15): troligen nivå (inte verifierat) |
| 7 | med tecken, oftast 0: troligen filter eller nivå (inte verifierat) |
| 8–9 | 0 |

Den lägsta zonen kan vara en platshållare (Strings: översta tangent 0).

### Sampelposter: 14 byte (0x4C78..0x5E06, 321 st)

| byte | fält |
| --- | --- |
| 0–1 | 0 |
| 2–5 | start (bit 0–19) |
| 6–9 | loopstart (bit 0–19), bit 26 (0x04 i byte 9) = loopar; utan den är ljudet ett engångsljud |
| 10–13 | slut, exklusivt (bit 0–19), bit 20 = bank |

Loopen är [loopstart, slut): efter sampel slut−1 kommer loopstart. Loopstart kan ligga före start: 2 sampel före när hela
samplet loopas (Choir, Voices, Lore), en hel cykel före i DWGS (start mitt i cykeln) och ett längre stycke i Rhythm.

### DWGS

De 23 DWGS-multisounds delar 8 zoner och sampelposterna 0–7. Våg *k* (multisound 77 + *k*) ligger i bank 0 från *k* × 0x3FC,
och posterna är platser inom det blocket: en och samma cykel i 512, 256, 128, 64, 32, 16, 8 och 4 sampel (summa 0x3FC), en per
zon. Våg 23 slutar där Piano börjar (0x5BA6).

### Trumljud: 44 namn à 11 byte (bild 0x1E508), pekare till dem vid 0x1E4B2

Ljud *n* (Kick 1 .. Metronome2) spelar sampelpost 277 + *n*. Det är de 44 poster i följd som ingen zon använder, och de läser
bank 1. Trumkittarnas tangenter, stämning och nivå ligger i globaldatan (nedan, "Programdata").

## Programdata (SysEx)

Formatet står i M1 Owner's Manual, "MIDI Exclusive Format" och tabell 1–6 (s. 124–131). `tools/m1_syx.py` avkodar en dump.
Fabriksdatan är användarens egen dump `M1 files/MPC-00/Int_M1.syx` (utanför git): tre meddelanden, `F0 42 3n 19 func 0m data F7`
där byte 5 är minnesallokering och bank, och datan är Korgs 7-bitarspackning (en byte med de sju följandes bit 7, sedan de sju).

| func | innehåll | storlek |
| --- | --- | --- |
| 4C | alla program | 100 × 143 byte |
| 4D | alla kombinationer | 100 × 124 byte |
| 51 | globaldata: stämning, skalor, 4 trumkittar à 30 tangenter (7 byte: trumljud 1–44, tangent, pan, stämning, nivå, decay) | 861 byte |

Ett program (tabell 1): namn (0–9), läge Single/Double/Drum (10), mono/hold (11), OSC 1 och 2 multisound och oktav (12–15),
intervall, detune, delay start (16–18), pitch MG och VDF MG (19–26), aftertouch (27–31), joystick (32–37), effekter (38–62), och per
oscillator (63–102 för OSC 1, samma för OSC 2 från 103): pitch-EG, VDF (cutoff, tangentföljning, EG-intensitet), VDF-EG med åtta
parametrar, VDA (nivå, tangentföljning, velocity) och VDA-EG med sju. Värden med tecken är tvåkomplement i en byte.

Kontroll mot dumpen: 70 Single-, 27 Double- och 3 Drum-program, alla multisounds interna (0–99). Universe = Choir + Lore,
Piano 16' = Piano, Ooh/Ahh = Voices + Choir, BottleBell = Bottles + Bell. Trumkitt 1 börjar med Kick 1 på tangent 24.

## Inte klart

- Zonbytes 3, 6 och 7.
- Låg-chipens nedre halvbytar.
