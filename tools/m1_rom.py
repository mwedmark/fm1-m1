#!/usr/bin/env python3
"""Läser användarens egna Korg M1-PCM-dumpar (IC36..IC43, 8 x 512 KB), kontrollerar storlek,
skriver CRC32/SHA-1 och gissar hur chipen ska slås ihop till en 4 MB-bild.

Innehåller ingen Korg-data. Kör med `python -I tools/m1_rom.py <mapp>` (-I: inga moduler från
dumpens mapp laddas).

  python -I tools/m1_rom.py DUMPMAPP                 # visa filer, checksummor, layoutgissning
  python -I tools/m1_rom.py DUMPMAPP --layout pairs16 --out m1_pcm.bin
  python -I tools/m1_rom.py DUMPMAPP --expect kanda.json  # jämför mot CRC32:or du själv fört in

Layouter:
  m1              uppmätt: (IC40,IC36) (IC41,IC37) (IC42,IC38) (IC43,IC39), hög byte | låg-chipets övre halvbyte,
                  ut som int16 LE (12 bitar), 2 097 152 sampel. Standard för --out.
  concat          IC36 | IC37 | ... | IC43
  pairs16         (IC36,IC37) bytevis interfolierade som (låg,hög), sedan (IC38,IC39) ...
  pairs16-swap    som pairs16 men (hög,låg)
"""
import argparse, hashlib, json, os, re, sys, zlib

CHIP = 512 * 1024
CHIPS = [f"ic{n}" for n in range(36, 44)]
LAYOUTS = ("m1", "concat", "pairs16", "pairs16-swap")


def find_chips(folder):
    found, other = {}, []
    for name in sorted(os.listdir(folder)):
        path = os.path.join(folder, name)
        if not os.path.isfile(path):
            continue
        m = re.search(r"(ic\d+|u\d+|plus1[_\w]*)", name.lower())
        key = m.group(1) if m else None
        if key in CHIPS:
            if key in found:
                sys.exit(f"Två filer för {key.upper()}: {found[key]} och {path}")
            found[key] = path
        else:
            other.append((key, path))
    return found, other


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def merge(chips, layout):
    if layout == "m1":
        # uppmätt 2026-11-25 på korgm1p1: IC40..43 är de höga 8 bitarna (med tecken), låg-chipet IC36..39 med samma
        # index ger 4 bitar till i sin övre halvbyte. Sampel = hög << 8 | (låg & 0xF0) som int16 LE: 12 bitar, 2M sampel.
        out = bytearray()
        for i in range(4):
            hi, lo = chips[CHIPS[4 + i]], chips[CHIPS[i]]
            pair = bytearray(2 * CHIP)
            pair[1::2] = hi
            pair[0::2] = bytes(b & 0xF0 for b in lo)
            out += pair
        return bytes(out)
    if layout == "concat":
        return b"".join(chips[k] for k in CHIPS)
    out = bytearray()
    for i in range(0, 8, 2):
        a, b = chips[CHIPS[i]], chips[CHIPS[i + 1]]
        if layout == "pairs16-swap":
            a, b = b, a
        pair = bytearray(2 * CHIP)
        pair[0::2] = a
        pair[1::2] = b
        out += pair
    return bytes(out)


def roughness(values):
    """Medel |x[n]-x[n-1]| delat med medel |x - medel|. Ljud: klart under 1. Brus/fel ordning: ~1.1-1.4."""
    n = len(values)
    if n < 3:
        return 9.0
    mean = sum(values) / n
    dev = sum(abs(v - mean) for v in values) / n
    if dev == 0:
        return 9.0
    diff = sum(abs(values[i] - values[i - 1]) for i in range(1, n)) / (n - 1)
    return diff / dev


def decode(buf, fmt):
    if fmt == "s8":
        return [b - 256 if b > 127 else b for b in buf]
    if fmt == "u8":
        return list(buf)
    le = fmt == "s16le"
    vals = []
    for i in range(0, len(buf) - 1, 2):
        v = buf[i] | buf[i + 1] << 8 if le else buf[i] << 8 | buf[i + 1]
        vals.append(v - 65536 if v > 32767 else v)
    return vals


def score(image, fmt, probes=64, span=4096):
    """Median-roughness över utspridda prov som inte är tysta/fyllda."""
    results = []
    step = max(1, (len(image) - span) // probes)
    for off in range(0, len(image) - span, step):
        win = image[off:off + span]
        if win.count(win[0]) > span * 0.9:   # tyst eller 0xFF-fyllt
            continue
        results.append(roughness(decode(win, fmt)))
    if not results:
        return None
    results.sort()
    return results[len(results) // 2]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--layout", choices=LAYOUTS)
    ap.add_argument("--out")
    ap.add_argument("--expect", help="JSON {\"ic36\": \"crc32 i hex\", ..., \"merged\": \"...\"}")
    args = ap.parse_args()

    found, other = find_chips(args.folder)
    for key, path in other:
        size = os.path.getsize(path)
        note = ""
        if key and key.startswith("u2"):
            note = "  <- Plus1-modifierad PCM, inte original-M1; används inte"
        elif key and key.startswith("plus1"):
            note = "  <- Plus1-modifierat OS/kort; används inte"
        print(f"  (hoppar över) {os.path.basename(path)}  {size} B{note}")

    missing = [k.upper() for k in CHIPS if k not in found]
    if missing:
        sys.exit("Saknas: " + ", ".join(missing))

    expect = {}
    if args.expect:
        with open(args.expect, encoding="utf-8") as f:
            expect = {k.lower(): v.lower().removeprefix("0x") for k, v in json.load(f).items()}

    chips, bad = {}, False
    print(f"{'chip':6} {'byte':>8}  {'CRC32':8}  SHA-1")
    for k in CHIPS:
        data = open(found[k], "rb").read()
        c = f"{crc32(data):08x}"
        mark = ""
        if len(data) != CHIP:
            mark, bad = f"  FEL STORLEK (väntat {CHIP})", True
        if k in expect:
            ok = expect[k] == c
            mark += "  stämmer" if ok else f"  AVVIKER (väntat {expect[k]})"
            bad |= not ok
        print(f"{k.upper():6} {len(data):8}  {c}  {hashlib.sha1(data).hexdigest()}{mark}")
        chips[k] = data
    if bad:
        sys.exit("Kontrollen misslyckades.")

    print("\nLayoutgissning (lägre = mer ljudlikt; ljud brukar ligga under ~0.6):")
    best = None
    for layout in LAYOUTS:
        img = merge(chips, layout)
        for fmt in ("s16le", "s16be", "s8", "u8") if layout != "concat" else ("s8", "u8", "s16le", "s16be"):
            s = score(img, fmt)
            if s is None:
                continue
            print(f"  {layout:13} {fmt:6} {s:.3f}")
            if best is None or s < best[0]:
                best = (s, layout, fmt)
    if best:
        print(f"Bäst: {best[1]} som {best[2]} ({best[0]:.3f}). Det är en gissning, inte en verifiering.")

    if args.out:
        layout = args.layout or "m1"
        if not layout:
            sys.exit("Ange --layout.")
        img = merge(chips, layout)
        with open(args.out, "wb") as f:
            f.write(img)
        c = f"{crc32(img):08x}"
        mark = ""
        if "merged" in expect:
            mark = "  stämmer" if expect["merged"] == c else f"  AVVIKER (väntat {expect['merged']})"
        print(f"\nSkrev {args.out}: {len(img)} B, layout {layout}, CRC32 {c}{mark}")


if __name__ == "__main__":
    main()
