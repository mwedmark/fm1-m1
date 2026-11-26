#!/usr/bin/env python3
"""Lägger M1:s effektdata sist i en packad FM1B-bank, så att firmware (fm1base/m1_fx2.c) kan slå upp programmets effekter.

  python -I tools/m1_fxtable.py roms/gen/m1_patches.json roms/gen/m1_prog_packed.fm1b roms/out/m1.fm1b

Tabellen är en post om 20 byte per program (0..99): effekttyp 1 och 2 (M1:s nummer minus 1, som i programdatan), deras DRY:EFF-balans
(0..100) och deras 8 parameterbyte var. Sist 8 byte: u16 antal poster, u16 postens storlek, "M1FX". FM1B-läsaren bryr sig inte om
data efter bankens egna avsnitt, och bankens längd i resursregionen räknar med tillägget. Programdatan är användarens egen
(M1 files/, utanför git) och hamnar bara i roms/.
"""
import json, struct, sys

ENTRY = 20


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    pat = json.load(open(sys.argv[1], encoding="utf-8"))["programs"]
    bank = open(sys.argv[2], "rb").read()
    if bank[-4:] == b"M1FX":
        sys.exit("banken har redan en effekttabell")
    out = bytearray()
    for p in pat:
        e = p["effects"]
        b1, b2 = e["balance"][0], e["balance"][2]
        out += bytes([e["type1"] & 255, e["type2"] & 255, min(100, b1), min(100, b2)])
        out += bytes(e["param1"][:8]) + bytes(e["param2"][:8])
    out += struct.pack("<HH", len(pat), ENTRY) + b"M1FX"
    open(sys.argv[3], "wb").write(bank + out)
    print(f"{sys.argv[3]}: {len(bank) + len(out)} B (banken {len(bank)} + effekttabell {len(out)})")


if __name__ == "__main__":
    main()
