# Korg M1: the LCD and how the OS drives it, measured (2026-11-27)

Measured on the user's own OS dumps (rev 1.9, IC23 high / IC32 low) by disassembling them (`ndisasm -b16`) and running them with
`tools/m1_lcd_emu.py`. Nothing here is Korg's code or text: it is addresses, protocol and geometry. What is a guess is marked.

## CPU and memory map

- **NEC V50** (a V30 core): 8086 code, 186 instructions (`pusha`, `enter`, `push imm`) and the V20 bit instructions `0F 10..1F`
  (`TEST1/CLR1/SET1/NOT1`, 1 289 executed on the way to the play page).
- The two EPROMs are interleaved (low chip at even addresses) and sit at **0xE0000–0xFFFFF**. Reset vector `F000:FFF0` = `JMP FAR E000:0000`.
- The OS's data segment is `F75C` (= image offset 0x175C0, where the multisound tables are). The stack is `F75C:9420`, which wraps past 1 MB
  (A20 not connected) onto low RAM, so DS-relative addresses ≥ 0x8A40 are RAM and the rest of the segment is ROM data.
- RAM test at start: writes and reads back `4F 6B 61` ("Oka") at 0000:0000.

## LCD

- **HD44780-compatible, 40 × 2 characters**, 5 × 8 dots, at I/O **0x0A06** (data) and **0x0A08** (control: bit 0 RS, bit 1 R/W, bit 2 E).
  A command is `out A06,v; out A08,4; out A08,0`, a data byte `out A06,v; out A08,5; out A08,1`. Busy is read with `out A08,2; out A08,6; in A06`
  (bit 7), via `out A0C,0xCC / 0x0C` around it (probably a bus-direction latch). The emulator answers "not busy".
- Init (OS 0x0EDB): `38` three times with delays, then `08`, `01`, `06`, `0C` (display on, no cursor, no blink).
- Address set (OS 0x0FA4): column 0..39 of row 0 = DDRAM 0x00.., of row 1 = 0x40.. (`col ≥ 0x28 → +0x18`).
- **The OS's text buffer** is 80 bytes at `DS:9454` (index = row × 40 + column). Writers put characters there; a loop in the main
  loop (OS 0x101D) sends **one non-zero byte per pass** to the LCD and clears it (0 = nothing to send, a space is 0x20). The address is set
  only after a skipped byte, so a full redraw is one burst of 80 characters: the module has to continue from column 40 of row 0 into row 1,
  or the OS's own screens would be broken. The emulator therefore models the LCD as 80 linear characters (a guess that matches the OS's intent).
- **Text output** (OS 0x115D, one character at a time) understands `FF` (0x0C: clear), `TAB` (0x09: to the next multiple of 5 columns) and
  `ESC nn c`: two decimal digits and a character ≥ 0x40 (OS 0x10DB) set the cursor column; the UI is built from format strings such as
  `ESC 4 0 …` / `ESC 4 5 %-35s`, printed with a printf (OS 0xFB28). The fields of a page lie on a 5-column grid (OS 0x1C8B, `× 5`).
- **Custom characters (CGRAM)**: eight, loaded one row at a time by OS 0xF51 from a table of eight pointers at `DS:955A`
  (set up at OS 0x1ECA). Set at start:

  | code | glyph (8 rows, 5 bits) | looks like |
  | --- | --- | --- |
  | 1 | `02 02 02 02 0E 1E 1C 00` | a note |
  | 4 | `01 03 07 0F 07 03 01 00` | ◀ |
  | 5 | `10 18 1C 1E 1C 18 10 00` | ▶ |
  | 6 | `0F 0C 0C 0C 0C 0C 0F 00` | `[` |
  | 7 | `1E 06 06 06 06 06 1E 00` | `]` |

  Codes 0, 2 and 3 are never given a glyph in the start-up code (their pointers are not set there); the emulator leaves them at whatever
  RAM held, so ignore them until a page that uses them is found.
- **The character generator ROM (CGROM) is in the LCD module, not in the OS**, and cannot be read out through the controller's interface. The OS prints
  ASCII 0x20–0x7D, so the glyph shapes come from the module's font. The photos show the HD44780 **ROM code A00** (Japanese standard font; ASCII except
  `\` = yen and `~` = arrow). Its patterns are published in Table 4 of Hitachi's HD44780U datasheet (page 17), and `tools/hd44780_a00.py` reads them
  out of the PDF (the dots are vector shapes) into `fm1base/m1_lcd.h`. Whether the M1's module has exactly ROM A00 is not proved by the ROM,
  only by how the photos look; a clone controller's font could differ in single characters.

## Checked against photos of real displays

Photos of an M1 with a green module and of replacement displays (blue with white dots, black with amber dots) show the same text as the
emulator, character for character and column for column: `PROG  I00 Init Prog` / `Battery Low (Internal)` (row 1 from column 5) and, with a
program loaded, `PROG  I00 Universe` / `O+00 F+00 L+00 K+00 V+00 A+00 R+00 E+00`. With the battery reading faked as good the emulator draws
that second row too (`tools/m1_lcd_emu.py`, the default; `--battery-low` leaves the warning). The photos also show the HD44780 look of the
glyphs (a zero with a diagonal, descenders on g, j, p, q and y). The default colours in `fm1base/m1_lcd.h`
(green, blue, amber) are read off the photos.

## What the OS draws with nothing in battery RAM

`tools/m1_lcd_emu.py` boots the OS to the play page: row 0 the program number and name (`PROG  I00 Init Prog`), row 1 the
battery warning from column 5. The start splash is on row 1 from column 5, with the version at column 35. Programs, combinations and
global data live in battery RAM, which the emulator does not have; to see a real program name, the RAM has to be loaded (next step).

## The panel scan (measured 2026-11-27)

- **Where:** the tick interrupt (vector 10, OS 0x11E8D) calls the scanner at OS **0x11BAC** every time, and every fourth call (a counter in
  `DS:C4D4` overflowing) the panel scan at **0x11C22**.
- **Key matrix:** 7 rows. The scanner writes the row number to I/O **0x0A00** (bits 0..2; the upper bits are other outputs, written in the same byte) and
  reads the columns from **0x0A02**, **bits 0..5**: 1 = key down. A read is done twice and must agree (debounce). The previous row values are kept at
  `DS:C4CC` + row; a changed bit makes an event in a ring of 64 bytes at `DS:C486` (write index `DS:C4CA`, count `DS:C4C6`): code **row × 8 + column**,
  with bit 7 set for a key going down (the repeat table at `DS:9543` decides which keys repeat). The main loop takes the events (OS 0x8D6).
- Bits 6 and 7 of 0x0A02 are other inputs, not keys: bit 6 is debounced for 20 scans and queues event 0xFD when low / 0xFB when high (not identified);
  0x0A08 bit 3 queues 0xFC (not identified). The emulator leaves them 0, which the OS accepts.
- **Analog inputs:** 8 channels are read from **0x0900** after selecting one with 0x0A00 (bits 3..5 and the strobe bit 6/7), compared with the last
  value (change of 2 or more counts) and flagged in `DS:C4EB` (sliders, joystick, pedals: not used in the emulator, all read 0).
- **Other outputs of the scan loop:** 0x0A04 (three values in turn, with bits from `DS:9556`: the LED columns).

### The key map (what each position does in the emulator)

Found by pressing every position from the play page, from Combination, from Program edit and from Global and watching the LCD
(`tools/m1_lcd_emu.py --tap ROW,COL`). Most functions act on the **release**. The names are what the function is on an M1; they are inferred
from behaviour, not from a label.

| row, col | function | evidence |
| --- | --- | --- |
| 0,4 | COMBI (play) | `COMBI I00 Init Combi` / bank grid |
| 0,5 | COMBI edit | `COMBINATION TYPE ( MULTI )` |
| 1,2 | PROG (play) | `PROG I00 Init Prog` / `O+00 F+00 …` |
| 1,3 | PROG edit | `OSC BASIC  OSC Mode` |
| 1,5 | GLOBAL | `Master Tune = +00` |
| 2,0 | +1 / YES | next program, `Master Tune = +01`, `OSC Mode (DOUBLE)` |
| 2,1 | −1 / NO | `Master Tune = -01` |
| 2,2 .. 2,5 | O F L K in Program play (`OSC Balance`, `VDF Cutoff`, `VDA Level`, `VDFA KBD Track`); timbres 1..4 in Combination | letters of the second row |
| 6,0 .. 6,3 | V A R E in Program play (`Vel. Sens`, `Attack Time`, `Release Time`, `Effect Balance`); timbres 5..8 in Combination | the same |
| 6,5 | PAGE next | `OSC1 Multisound`, `Key Transpose`, `Timbre 1 Level` |
| 6,4 | PAGE previous | back from the second Program-edit page |
| 4,0 .. 4,4 | digits 0..4 | `PROG I0*` … `I4*` |
| 5,0 .. 5,4 | digits 5..9 | `PROG I5*` … `I9*` |
| 3,1 | back / exit (to the first Program-edit page) | seen on page 2 only |

Not found: the positions in rows 0, 1, 2, 3 and 6 not listed (and bit 5 of rows 4 and 5). They do nothing in the states tried
(no SEQ or media hardware is emulated; ENTER/EXIT may only act in a state not reached). The keyboard's own scan is not in this path.

## What is not done

- The keys not mapped yet (see above), and the sliders and joystick (analog, 0x0900).
- Loading the factory programs into RAM, to draw real names and values.
- The sound chips' ports (0x500–0x50E, 0x200–0x24E …) are only written to, never answered: enough for the display, not for a full boot.
