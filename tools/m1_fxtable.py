#!/usr/bin/env python3
"""Puts the M1's effect data at the end of a packed FM1B bank, so that the firmware (fm1base/m1_fx2.c) can look up a program's effects.

  python -I tools/m1_fxtable.py roms/gen/m1_patches.json roms/gen/m1_prog_packed.fm1b roms/out/m1.fm1b

The table is one 20-byte record per program (0..99): effect type 1 and 2 (the M1's number minus 1, as in the program data), their DRY:EFF balance
(0..100) and 8 parameter bytes each. Last 8 bytes: u16 record count, u16 record size, "M1FX". The FM1B reader ignores
data after the bank's own sections, and the bank's length in the resource region includes the addition. The program data is the user's own
(M1 files/, outside git) and only ends up in roms/.
"""
import json, struct, sys

ENTRY = 20


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    pat = json.load(open(sys.argv[1], encoding="utf-8"))["programs"]
    bank = open(sys.argv[2], "rb").read()
    if bank[-4:] == b"M1FX":
        sys.exit("the bank already has an effect table")
    out = bytearray()
    for p in pat:
        e = p["effects"]
        b1, b2 = e["balance"][0], e["balance"][2]
        out += bytes([e["type1"] & 255, e["type2"] & 255, min(100, b1), min(100, b2)])
        out += bytes(e["param1"][:8]) + bytes(e["param2"][:8])
    out += struct.pack("<HH", len(pat), ENTRY) + b"M1FX"
    open(sys.argv[3], "wb").write(bank + out)
    print(f"{sys.argv[3]}: {len(bank) + len(out)} B (bank {len(bank)} + effect table {len(out)})")


if __name__ == "__main__":
    main()
