#!/usr/bin/env python3
"""Reads the user's own Korg M1 PCM dumps (IC36..IC43, 8 x 512 KB), checks the size,
writes CRC32/SHA-1 and guesses how the chips should be merged into a 4 MB image.

Contains no Korg data. Run with `python -I tools/m1_rom.py <folder>` (-I: no modules from
the dump's folder are loaded).

  python -I tools/m1_rom.py DUMPFOLDER                 # show files, checksums, layout guess
  python -I tools/m1_rom.py DUMPFOLDER --layout pairs16 --out m1_pcm.bin
  python -I tools/m1_rom.py DUMPFOLDER --expect known.json  # compare against CRC32s you entered yourself

Layouts:
  m1              measured: (IC40,IC36) (IC41,IC37) (IC42,IC38) (IC43,IC39), high byte | the low chip's upper nibble,
                  out as int16 LE (12 bits), 2 097 152 samples. The default for --out.
  concat          IC36 | IC37 | ... | IC43
  pairs16         (IC36,IC37) interleaved byte by byte as (low,high), then (IC38,IC39) ...
  pairs16-swap    like pairs16 but (high,low)
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
                sys.exit(f"Two files for {key.upper()}: {found[key]} and {path}")
            found[key] = path
        else:
            other.append((key, path))
    return found, other


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def merge(chips, layout):
    if layout == "m1":
        # measured 2026-11-25 on korgm1p1: IC40..43 are the high 8 bits (signed), the low chip IC36..39 with the same
        # index gives 4 more bits in its upper nibble. Sample = high << 8 | (low & 0xF0) as int16 LE: 12 bits, 2M samples.
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
    """Mean |x[n]-x[n-1]| divided by mean |x - mean|. Sound: clearly under 1. Noise/wrong order: ~1.1-1.4."""
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
    """Median roughness over spread-out probes that are not silent/filled."""
    results = []
    step = max(1, (len(image) - span) // probes)
    for off in range(0, len(image) - span, step):
        win = image[off:off + span]
        if win.count(win[0]) > span * 0.9:   # silent or 0xFF-filled
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
    ap.add_argument("--expect", help="JSON {\"ic36\": \"crc32 in hex\", ..., \"merged\": \"...\"}")
    args = ap.parse_args()

    found, other = find_chips(args.folder)
    for key, path in other:
        size = os.path.getsize(path)
        note = ""
        if key and key.startswith("u2"):
            note = "  <- Plus1-modified PCM, not an original M1; not used"
        elif key and key.startswith("plus1"):
            note = "  <- Plus1-modified OS/card; not used"
        print(f"  (skipping) {os.path.basename(path)}  {size} B{note}")

    missing = [k.upper() for k in CHIPS if k not in found]
    if missing:
        sys.exit("Missing: " + ", ".join(missing))

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
            mark, bad = f"  WRONG SIZE (expected {CHIP})", True
        if k in expect:
            ok = expect[k] == c
            mark += "  matches" if ok else f"  DIFFERS (expected {expect[k]})"
            bad |= not ok
        print(f"{k.upper():6} {len(data):8}  {c}  {hashlib.sha1(data).hexdigest()}{mark}")
        chips[k] = data
    if bad:
        sys.exit("The check failed.")

    print("\nLayout guess (lower = more sound-like; sound is usually under ~0.6):")
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
        print(f"Best: {best[1]} as {best[2]} ({best[0]:.3f}). It is a guess, not a verification.")

    if args.out:
        layout = args.layout or "m1"
        if not layout:
            sys.exit("Give --layout.")
        img = merge(chips, layout)
        with open(args.out, "wb") as f:
            f.write(img)
        c = f"{crc32(img):08x}"
        mark = ""
        if "merged" in expect:
            mark = "  matches" if expect["merged"] == c else f"  DIFFERS (expected {expect['merged']})"
        print(f"\nWrote {args.out}: {len(img)} B, layout {layout}, CRC32 {c}{mark}")


if __name__ == "__main__":
    main()
