#!/usr/bin/env python3
"""Skriver --keep-listan till pack_bank.js: de ljud i en opackad (BFP8) bank som har en kort loop som börjar tidigt.

  node ../fm1-sf2/pack_bank.js roms/gen/m1_bfp8.fm1b roms/gen/m1_packed.fm1b --kbit 64 --flash 696288 --keep "$(python3 -I tools/m1_keep.py roms/gen/m1_bfp8.fm1b)"

En MP3-kodad kort loop går inte att slinga sömlöst (uthållna toner på Organ 2, A.Bass, Kalimba lät som en dålig samplingloop), så ljud med
en loop under 130 sampel som börjar inom de 3000 första samplen ligger förlustfritt (BFP8): 94 ljud, 81 KB. Se docs/M1_PROGRAMS.md.
Ljudens nummer är pack_bank.js:s (s0, s1 ...: sorterade efter plats i banken).
"""
import struct, sys

MAX_LOOP, MAX_START = 130, 3000


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    b = open(sys.argv[1], "rb").read()
    if b[:4] != b"FM1B":
        sys.exit("ingen FM1B-bank")
    nz = struct.unpack_from("<H", b, 14)[0]
    off_zone = struct.unpack_from("<I", b, 32)[0]
    sounds = {}
    for i in range(nz):
        o = off_zone + i * 24
        off, n, ls, le = struct.unpack_from("<4I", b, o)
        if off not in sounds:
            sounds[off] = (n, ls, le, b[o + 17] & 1)
    keep = [f"s{k}" for k, (off, (n, ls, le, loop)) in enumerate(sorted(sounds.items()))
            if loop and le - ls < MAX_LOOP and ls < MAX_START]
    print(",".join(keep))


if __name__ == "__main__":
    main()
