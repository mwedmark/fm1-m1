#!/usr/bin/env python3
"""Puts the M1's combinations in a bank, so that the firmware (fm1base/m1_combi.c) can play them.

  python -I tools/m1_combi.py roms/gen/m1_patches.json roms/out/m1.fm1b [--out roms/out/m1.fm1b]

The input is the JSON that tools/m1_syx.py writes (the dump's "combinations"); the bank is the finished bank, normally with the effect table
(tools/m1_fxtable.py) at the end. Run this last, or in any order: if the bank ends in an effect table "M1FX" the combination table is put in
front of it, so that fm1base/m1_fx2.c still finds its own table at the very end. The FM1B reader ignores everything after the bank's sections.

Table "M1CB", variable length, little-endian:
    u16 offset[count]            from the start of the table to each record
    record: name[10] (ASCII, space padded), type (0 single, 1 layer, 2 split, 3 velocity switch, 4 multi), n (number of timbres that are ON),
            n * { program, level 0..99, transpose (s8), detune (s8), pan (raw byte: 0..15, bit 7 = INST), key top, key bottom,
                  velocity top, velocity bottom, channel 0..15 }
    trailer (8 bytes): u16 count, u16 length of everything before the trailer (offsets and records), "M1CB"
Timbres that are OFF are left out. Only the first MAX_TIMBRES (8) are used. The combination's own effects are not stored yet.

The table takes room from the samples: with 100 factory combinations it is about 2-4 KB. Lower pack_bank.js's --flash by the same amount
(the tool prints the size), the bank region has 696 288 B and the effect table already takes 2 008.
The combination data is the user's own (M1 files/, outside git) and only ends up in roms/.
"""
import argparse
import json
import struct
import sys

MAGIC = b"M1CB"
FXMAGIC = b"M1FX"
TYPES = {"single": 0, "layer": 1, "split": 2, "vel_switch": 3, "multi": 4}
MAX_TIMBRES = 8
TIMBRE_BYTES = 10
NAME_BYTES = 10


def clamp(v, lo, hi):
    return max(lo, min(hi, int(v)))


def pack_record(c):
    name = str(c.get("name", ""))[:NAME_BYTES].encode("ascii", "replace").ljust(NAME_BYTES, b" ")
    t = c["type"]
    typ = TYPES[t] if isinstance(t, str) else clamp(t, 0, 4)
    timbres = [x for x in c["timbres"][:MAX_TIMBRES] if x.get("on", True)]
    out = bytearray(name)
    out += bytes([typ, len(timbres)])
    for x in timbres:
        out += struct.pack(
            "<BBbbBBBBBB",
            clamp(x["program"], 0, 255),
            clamp(x["level"], 0, 99),
            clamp(x["transpose"], -128, 127),
            clamp(x["detune"], -128, 127),
            (clamp(x["pan"], 0, 15) | (0x80 if x.get("inst") else 0)),
            clamp(x["key_top"], 0, 127),
            clamp(x["key_bottom"], 0, 127),
            clamp(x["vel_top"], 0, 127),
            clamp(x["vel_bottom"], 0, 127),
            clamp(x["channel"], 0, 15),
        )
    return bytes(out)


def build_table(combis):
    recs = [pack_record(c) for c in combis]
    head = 2 * len(recs)
    offs, body = [], bytearray()
    for r in recs:
        offs.append(head + len(body))
        body += r
    if head + len(body) > 0xFFFF:
        sys.exit("the combination table is larger than 64 KB")
    data = struct.pack("<%dH" % len(offs), *offs) + bytes(body)
    return data + struct.pack("<HH", len(recs), len(data)) + MAGIC


def fx_table_start(bank):
    """Start of the effect table if the bank ends in one (m1_fxtable.py: n records of `size` bytes, then n, size, "M1FX"), else None."""
    if bank[-4:] != FXMAGIC:
        return None
    n, size = struct.unpack("<HH", bank[-8:-4])
    start = len(bank) - 8 - n * size
    if start < 0:
        sys.exit("the bank's effect table is damaged")
    return start


def add_table(bank, table):
    if bank[-4:] == MAGIC:
        sys.exit("the bank already has a combination table")
    start = fx_table_start(bank)
    if start is not None and bank[start - 4:start] == MAGIC:
        sys.exit("the bank already has a combination table")
    if start is None:
        return bank + table
    return bank[:start] + table + bank[start:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("patches", help="JSON from tools/m1_syx.py")
    ap.add_argument("bank", help="the finished FM1B bank")
    ap.add_argument("--out", help="default: rewrite the bank in place")
    a = ap.parse_args()
    combis = json.load(open(a.patches, encoding="utf-8")).get("combinations")
    if not combis:
        sys.exit("no combinations in the JSON (does the .syx hold the all-combinations dump?)")
    bank = open(a.bank, "rb").read()
    table = build_table(combis)
    out = add_table(bank, table)
    dest = a.out or a.bank
    open(dest, "wb").write(out)
    print(f"{dest}: {len(out)} B (bank {len(bank)} + combination table {len(table)}): "
          f"lower pack_bank.js --flash by {len(table)} B if the bank no longer fits")


if __name__ == "__main__":
    main()
