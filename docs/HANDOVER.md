# Överlämning: M1-LCD, knappar och Global-sidor (2026-11-27)

Läget när arbetet flyttas från molnsessionen till en lokal gren. Allt som är gjort är committat och pushat på
`claude/confident-feynman-vnuvnt`, utkast-PR [#3](https://github.com/mwedmark/fm1-m1/pull/3). Inget är byggt för enheten (ingen ARM-toolchain i molnet):
**första steget lokalt är att bygga `m1.fwsc` och mäta imagestorleken** (se "Att kontrollera först").

## Ta över lokalt

```bash
git fetch origin claude/confident-feynman-vnuvnt
git switch -c lokal origin/claude/confident-feynman-vnuvnt
```

Radslut: `README.md`, `engine/fm1b.c`, `engine/fm1b.h` och `fm1base/app.c` är **CRLF** i repot. Verktyg som skriver om filerna med LF ger en
diff på tusentals rader (det hände en gång i sessionen och är rättat i sista committen). Kontrollera med `git diff --stat origin/main` att
nettot är litet (README +48, fm1b.c 12 rader, fm1b.h 2, app.c ~250) och behåll CRLF i de fyra filerna.

## Vad som är gjort (6 commits)

| Del | Filer | Läge |
| --- | --- | --- |
| OS:et körs i emulator (NEC V50, Unicorn) och LCD:n läses ut | `tools/m1_lcd_emu.py`, `docs/M1_LCD.md` | klart, verifierat mot foton |
| HD44780 A00-fonten ur databladet | `tools/hd44780_a00.py` | klart (exakt, ur databladets sida 17) |
| LCD-vyer: `M1` (1:1) och `2X20`, färgerna GREEN (default), BLUE, AMBER | `fm1base/m1_lcd.h`, `m1_setup.h`, `app.c` | klart i host-simulatorn |
| FM-1-knapparna som M1-knappar, hjälp vid 3 s | `fm1base/m1_keys.h`, `app.c` | klart i host-simulatorn |
| Global-sidorna (20 st), redigering, SAVE | `tools/m1_pages.py`, `fm1base/m1_pages.h`, `app.c`, `engine/fm1b.c` | klart i host-simulatorn |
| Tester (host-skript) | `fm1base/tests/m1_lcd.txt`, `m1_keys.txt`, `m1_global.txt` | körs, tar skärmbilder |

### Det viktigaste som mättes (detaljer i `docs/M1_LCD.md`)

- **CPU:** NEC V50 (V30-kärna). OS-EPROM (IC23 hög, IC32 låg) ligger interfolierade på 0xE0000–0xFFFFF, dataseg `F75C`, stacken wrappar vid 1 MB.
  V20-bitinstruktionerna `0F 10..1F` finns inte i Unicorn och emuleras i verktyget.
- **LCD:** HD44780-kompatibel, 40×2, I/O 0x0A06 (data) och 0x0A08 (styr: bit0 RS, bit1 R/W, bit2 E). OS:ets textbuffert är 80 byte på `DS:9454`.
  Textutskrift: `ESC nn c` = kolumn, TAB = nästa 5-kolumnsfält, FF = rensa. Teckengeneratorn (CGROM) finns i modulen, inte i OS:et.
- **Knappmatris:** 7 rader, rad väljs via 0x0A00 (bit 0–2), kolumner läses från 0x0A02 (bit 0–5). Händelsekod `rad×8+kol`, bit 7 = tryck. De flesta
  funktioner utförs vid **släpp**. Kartan finns i `docs/M1_LCD.md` (COMBI 0,4; EDIT-COMBI 0,5; PROG 1,2; EDIT-PROG 1,3; GLOBAL 1,5; +1/−1 2,0/2,1;
  PAGE 6,5/6,4; siffror 4,0–5,4; åtta fältknappar 2,2–2,5 och 6,0–6,3 under displayens 5-kolumnsgrupper).
- **Global:** 20 sidor, markören ligger i `DS:94C5`, rad 0 visar vald parameters namn, värden klampar (wrap aldrig).
- **Bekräftat mot foton:** `PROG  I00 Init Prog` / `Battery Low (Internal)` och, med batteriet "OK", `O+00 F+00 L+00 K+00 V+00 A+00 R+00 E+00`.

### Beslut jag tog (ändra gärna, allt står i tabeller)

- **Setup flyttades från EDIT till ARP**, eftersom EDIT nu är M1:s EDIT. Ändras i `m1_keys.h` (`m1_keymap`) och `key_action` i `app.c`.
- **Knappar agerar vid släpp** (som på M1:n). Håll 3 s (`M1_HELP_MS`) visar M1-funktionen på LCD:n i 2,5 s och gör inget vid släpp.
- **Standard:** vy `M1` (den tunna 1:1-remsan), färg GREEN. Båda väljs på Setup-sidan (ARP, K1 fält, K2 värde).
- **Settings:** `settings_t` utökad med `g[32]` (Global-värdena, index+1, 0 = OS:ets startvärde). Äldre poster på 8 byte läses fortfarande.
- **Vad Global gör:** Master Tune (ny `fm1b_master_tune` i motorn, gäller toner som startar efteråt), Key Transpose (tangenter och inkommande toner, inte trummor),
  Local ON/OFF, MIDI-filter för program change och controllers. Resten sparas och visas men gör inget (pedaler, skalatyp, Common CH, klocka, eftertryck, SysEx, minnesskydd).
- **Bank och BYOR:** Korgs texter och sidor går **inte** in i git. `tools/m1_pages.py` bygger en tabell (~2,8 KB) som läggs sist i resursregionen med trailern `M1PG`
  (före den läser `m1_fx2_init` sin `M1FX`; `m1_pg_init` tar bort `M1PG` först). `roms/` är gitignorerad.

## Bygga och testa

README beskriver docker-bygget (`fm1b`). I molnet gick host-simulatorn med `JIELI_DOCKER=0` och `--host` från fm1-base-checkouten bredvid:

```bash
python3 tools/build.py ../fm1-m1/fm1base --host
./fm1base/build/host/m1_host fm1base/tests/m1_global.txt <utkatalog>
```

- Bygget kräver att `roms/out/m1.fm1b` finns. För bara-UI-tester räcker en platshållare: `b'FM1B' + 60 nollor`, sedan `tools/m1_pages.py --append` för sidtabellen.
  Utan riktig bank visar spelsidan `NO M1 DATA`; Global-sidorna fungerar ändå.
- Emulatorn: `pip install unicorn`, sedan
  `python -I tools/m1_lcd_emu.py IC23.bin IC32.bin [--tap RAD,KOL ...] [--battery-low]` (~6 s). Sidtabellen: `python -I tools/m1_pages.py IC23.bin IC32.bin roms/gen/m1_pages.bin` (~30 s).
- Fallgropar: ett skript som heter `glob.py` skuggar stdlib och kraschar Unicorn-importen. `Machine.tap` måste hålla knappen ≥ en skanning (~560 000 instr) och vänta
  tills LCD:n uppdaterats (den skrivs ett tecken per varv i huvudslingan), annars läses en gammal skärm. Host-simulatorns `reboot` nollar inte `ui`-structen.

## Att kontrollera först (inte gjort)

1. **Bygg för enheten** och mät imagen mot 192 KB-luckan (`[0x4000, 0x34000)`; imagen var ~148 KB före arbetet). Nytt: `m1_lcd.h` (~1 KB font + kod), `m1_pages.h`, `m1_keys.h`,
   motorfunktionen. `data_lo` i `fm1.toml` kan behöva justeras om det inte ryms.
2. **Lyssna på Master Tune och Transpose** med riktig bank, och kontrollera att Local OFF och filtren beter sig.
3. **Att modulens CGROM verkligen är A00** vilar bara på fotona. Jämför ett par tecken på en riktig M1 (gemena, `\`, `~`).
4. **LCD-modellen som 80 linjära tecken** är en gissning (OS:et skriver rad 1 och rad 2 i en följd utan adressbyte).
5. Programnamn och värden är inte kontrollerade mot riktiga program (emulatorn har inget batteriminne; behöver `Int_M1.syx` laddad i RAM).

## Nästa steg (förslag, i ordning)

1. Mät och rätta enhetsbygget (ovan).
2. **Combination-spel** på samma sätt som Global (knapp 0,4): kartlägg sidorna med `--tap`, utöka `m1_pages.py` till flera lägen (generalisera `read_global`),
   koppla PLAY. Sidorna har samma fältmodell.
3. **Trumkitten (Global 6–9) och användarskalan (5)**: parametrarna hänger ihop, så de behöver en egen modell (idag bara bilder).
4. **Program-edit** (EDIT, knapp 1,3): många sidor; kräver programdata i RAM för att visa riktiga värden.
5. Koppla SELECT-vredet och K1–K4 vidare (PAGE och de åtta fältknapparna) i Program/Combi-lägena, och siffer-/snabbparametertangenter.
6. Okända M1-knappar: positioner i rad 0, 1, 2, 3 och 6 som inte gav något (SEQ, media, ENTER/EXIT?). Prova från fler lägen.
7. Analoga reglage (0x0900) och klaviaturens egen skanning är inte utlästa.

## Öppna frågor till dig

- Ska `2X20` vara standardvy i stället för `M1`? (Remsan i 1:1 blir bara ~22 px hög på 240×240.)
- Vill du att Common CH filtrerar inkommande MIDI som på M1:n? Idag spelar motorn alla 16 kanaler.
- Ska Setup ligga kvar på ARP, eller hellre en annan knapp?
- Fler färgscheman (t.ex. M1R) utöver de tre från fotona?

## Filer att känna till

`fm1base/`: `app.c` (UI, knappar, Global-logik), `m1_lcd.h` (font, färger, vyer), `m1_keys.h` (knappkopplingen), `m1_pages.h` (sidtabell), `m1_setup.h` (Setup-fält),
`tests/m1_*.txt` (host-skript). `tools/`: `m1_lcd_emu.py`, `m1_pages.py`, `hd44780_a00.py`. `docs/M1_LCD.md`: all mätning. `engine/fm1b.c`: `fm1b_master_tune`.
Dina ROM-filer (som aldrig ska i git): `roms/os/ic23.bin`, `roms/os/ic32.bin` användes i molnet; lokalt är det `M1 files\rev19-ic23-880219.bin` och `rev19-ic32-880319.bin`.
