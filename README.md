# fm1-m1: en Korg M1-liknande synt på M-VAVE FM-1 (utkast)

**BYOR: ta med din egen ROM.** Repot innehåller ingen Korg-data, och det kommer inte releaser eller installationssidor heller att göra.
Du dumpar själv PCM-ROM och program från din egen M1 och gör om dem till en ljudbank på din egen dator.
Lägg dumparna utanför repot, eller i `roms/`, som `.gitignore` ska utesluta.

## Arkitektur (2026-11-25)

- **Plattform: fm1-base**, som fm1-d50 (`fm1base/`: `fm1.toml`, `app.h`, `app.c`). Byggs i fm1-base-checkouten bredvid repot,
  i containern `fm1b`:

  ```powershell
  docker exec fm1b sh -c "cd /git/fm1-base && JIELI_DOCKER=0 python3 tools/build.py ../fm1-m1/fm1base --no-ble"
  ```

  `--host` bygger host-simulatorn, och `fm1base/tests/play.txt` är ett första röktest (WAV och skärmbilder i `fm1base/build/test`).
- **Sampeluppspelning: fm1-sf2:s motor** (`engine/`, kopierad från fm1-sf2 5a4f267, GPL-3.0-only med linking exception): FM1B-banker,
  zoner, loopar, envelopes, filter, LFO, lager (Double), 16 MIDI-kanaler (Combination), reverb och chorus. Den körs i 32 kHz och
  omsamplas till 44,1 kHz. Ännu från XIP, inte RAM.
- **Banken ligger i resursregionen** (som fm1-d50:s data): byggs från `roms/out/m1.fm1b`. Annars skriver installationssidan eller
  `fm1_resources.py` in din egen fil. Utan bank visar skärmen `NO M1 DATA`.

### Utrymmet för sampel, maxat

`data_lo = 0x34000`: appen får [0x4000, 0x34000), 192 KB (bilden är 148 KB). Banken får 0x34000..0xDE000, **696 288 B**
(fm1-d50 har 589 792). Det som återstår att vinna:

- Sänka `data_lo` ytterligare när appen är klar och storleken känd (varje 4 KB-sektor går till sampel).
- Flytta panelens kalibrering från 0xDE000 så att banken når 0xE0000: +8 KB.
- XDATA1 (0xE5000..0xFC000, 92 KB) som en andra bit: kräver att banken kan delas på två fönster, eftersom loaderns staging
  0xE0000..0xE5000 ligger emellan.

### M1:s PCM och OS-tabeller

Uppmätt på dumparna, i detalj i [docs/M1_ROM.md](docs/M1_ROM.md): 12-bitars sampel i 31 250 Hz, två banker à 1M sampel.
OS v1.9 har 100 multisounds (23 DWGS) med 469 zoner i bank 0 och 44 trumljud i bank 1. Det som används är
**1 385 790 sampel**: hela bank 0 (1 048 000) och 337 790 av bank 1.

Vad som får plats i 696 288 B (minus bankens tabeller, några KB):

| Kodek (fm1-sf2) | byte/sampel | sampel | andel av 1 385 790 |
| --- | --- | --- | --- |
| 12-bit rå | 1,5 | 464 k | 33 % |
| BFP8 | 1,03 | 676 k | 49 % |
| SEA6 | 0,84 | 831 k | 60 % |
| SEA5 | 0,71 | 977 k | 70 % |
| Live cache (MP3, avkodas till SRAM) | ~0,25 | alla | 100 % (förstörande, kräver CPU och RAM) |

Allt i SEA5 kräver cirka 990 KB. Det saknas cirka 290 KB, så något måste ge: sänkt `data_lo`, XDATA1, kortare svansar på långa
engångsljud, lägre frekvens för mörka sampel, SEA5 för det mesta och live cache för resten.

## Verktyg

`tools/m1_rom.py`: kontrollerar IC36–IC43 (8 × 512 KB), skriver CRC32/SHA-1 och slår ihop dem. Standardlayouten `m1` är den uppmätta
ovan (int16, 2M sampel). De andra layouterna och gissningen finns kvar för andra dumpar.

```powershell
python -I tools/m1_rom.py "M1 files\korgm1p1" --out roms\gen\m1_pcm.bin
```

Med `--expect kanda_crc.json` jämför verktyget mot CRC32:or som du själv fyllt i, till exempel
`{"ic36": "1234abcd", "merged": "..."}`. Plus1-filerna (U201–U204, `plus-1_*`) hoppas över, eftersom de inte är original-M1.

`tools/m1_os.py`: läser multisounds, zoner, sampelposter och trumljud ur OS-EPROM:en (IC23 hög, IC32 låg) till JSON, kontrollerar
loopskarvarna mot PCM:en och kan skriva varje multisound och trumljud som WAV att lyssna på:

```powershell
python -I tools/m1_os.py "M1 files\rev19-ic23-880219.bin" "M1 files\rev19-ic32-880319.bin" --pcm roms\gen\m1_pcm.bin --out roms\gen\m1_tables.json --wav roms\gen\ms
```

## Från dump till bank (live cache)

Banken är en packad FM1B (fm1-sf2:s live cache): de flesta ljuden som en MP3-ström på 64 kbit/s i flashen, som avkodas till en cache
på 192 KB i fm1-base:s pool när en not behöver dem. Korta loopar och DWGS-cykler ligger förlustfritt. Hela M1 (495 ljud: 100 multisounds
och 44 trumljud) får plats: 677 704 B av 696 288.

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
node ..\fm1-sf2\pack_bank.js roms\gen\m1_bfp8.fm1b roms\out\m1.fm1b --kbit 64 --flash 696288 --keep "<ljud med kort loop och tidig loopstart, se docs/M1_PROGRAMS.md>"
```

```powershell
python -I tools/m1_fxtable.py roms\gen\m1_patches.json roms\gen\m1_packed.fm1b roms\out\m1.fm1b
```

`tools/fm1b_make_bank.js` är fm1-sf2:s `make_bank.js` med M1:s velocity i modulationsposten (44 byte); den läser
omvandlaren ur `../fm1-sf2/index.html` och ändrar den i minnet, så fm1-sf2 är orört. Motorn i `engine/` läser fälten.

Sedan bygger firmware-bygget in `roms/out/m1.fm1b` som resursregion. Bank 0, program 0–99 är M1:s fabriksprogram, bank 128 de
fyra trumkittarna (MIDI-kanal 10). Hur programmen blir presets, och vad som är approximerat: [docs/M1_PROGRAMS.md](docs/M1_PROGRAMS.md).
Utan `--patches` blir banken de råa multisounds i stället.
Banken innehåller Korgs sampel från din dump: den hör inte hemma i git eller på nätet.

## Prova i emulatorerna

- **fm1-base:s webbemulator** (appen som WebAssembly): `tools/build.py ../fm1-m1/fm1base --web` i containern, med
  `source /root/emsdk/emsdk_env.sh` först. Servera repot och öppna `fm1base/build/emu/index.html`, slå på emulatorn och välj din
  `roms/out/m1.fm1b` med "Load M1 data". Den sparas i webbläsaren.
- **fm1-emu** (enhetens egen binär, `m1.fwsc`): emulatorns filer (MIT) kopieras från `../fm1-d50/emu` till `emu/` (ignorerad),
  med `m1.fwsc`, `m1.flash` och `m1.script`. Överlagringen görs med
  `node ../fm1-sf2/make_flash_overlay.js fm1base/build/m1-res.bin emu/m1.flash 0x34000`. Servera `emu/` och öppna
  `index_tap.html?fw=m1&flash=m1.flash&mhz=360&script=m1.script`.

I fm1-emu startar firmware och binder den packade banken. Sidans "load" är datorns last för emuleringen, inte firmware-lasten.

## Uppmätt på FM-1 (2026-11-25)

Installerat med fm1-base:s `tools/fm1_install.py fm1base/build/m1.fwsc --port "<nuvarande port>" --yes`. Om resurssteget inte hittar
porten (den byter namn till "M1 FM-1"): `tools/fm1_resources.py fm1base/build/m1-res.bin --port "M1 FM-1"`. Mätt med
`python tools/m1_measure.py`, som spelar över USB-MIDI och läser av firmware (app.c `app_sysex`). 360 MHz, en kärna, motorn från XIP:

| Test | CPU (högsta medel) | Röster | Missade block | Tappade noter | Cache |
| --- | --- | --- | --- | --- | --- |
| Tomgång | 13 % | 0 | 0 | 0 | |
| Ackord, 8 toner, program 0/31/5 | 20 % | 10 | 0 | 0 | 6 missar |
| 39 programbyten med ackord | 18 % | 8 | 0 | 0 | 56 missar, 515 MP3-ramar |
| 44 trumljud | 12 % | 2 | 0 | 0 | 6 missar |
| 40 toner Strings samtidigt | 47 % | 40 | 0 | 0 | 3 missar |

Inte lyssnat på än: ljudkvaliteten med MP3 på 64 kbit/s.

## Referens att jämföra mot

M1:s 100 fabriksprogram som MP3-exempel: https://www.synthmania.com/Korg%20M1/Audio/Factory%20preset%20examples/Programs/
(för att lyssna och jämföra när programmotorn finns, inte som data i banken).

## Öppna frågor

- fm1-base varnar för 64-bitarsmultiplikationer bredvid laddningar (felucca-bsp docs/CPU.md: kan ge fel vid 360 MHz) i app_render, voice_play,
  filter_run med flera. fm1-sf2 hörde enstaka glitchar av det vid 360 MHz. Lyssna efter det, och flytta dem ur linje om de hörs.
- Omvandlaren `m1_tables.json` + PCM → FM1B-bank (fm1-sf2 FORMAT.md).
- Fabriksprogrammen finns som `.syx` (`M1 files/MPC-00/Int_M1.syx`) och avkodas av `tools/m1_syx.py` (docs/M1_ROM.md, "Programdata").
  Programmen är presets i banken (docs/M1_PROGRAMS.md); tider och nivåer är uppskattade och ska jämföras mot MP3-exemplen.
  `plus-1_prog_card.bin` är Plus-1-kortets egna program i samma format: programmotorn kan byggas och provas mot det.
- Kombinationerna (100 st) är avkodade men inte använda.
- Zonbytes 3, 6 och 7, bank 1 under 0xA3CFC och låg-chipens nedre halvbytar (docs/M1_ROM.md, "Inte klart").

`src/` och `include/` är ett första utkast till en egen motor från innan fm1-sf2 valdes. Det används inte och ska tas bort.
