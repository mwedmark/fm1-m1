#!/usr/bin/env python3
"""Decodes a Korg M1 SysEx dump (.syx) to JSON with named fields: all programs (func 4C), all combinations (4D) and
global data with the drum kits (51). The format is the M1 Owner's Manual, "MIDI Exclusive Format", tables 1–3 (p. 124–131).

  python -I tools/m1_syx.py "M1 files/MPC-00/Int_M1.syx" --out roms/gen/m1_patches.json

Signed values (-99..99, -12..12 ...) are two's complement in one byte and are printed with their sign. The program data is the user's
own and only ends up in roms/ (ignored by git).
"""
import argparse, json, sys

KORG, M1 = 0x42, 0x19
FUNC_ALL_PROG, FUNC_ALL_COMBI, FUNC_GLOBAL, FUNC_PROG = 0x4C, 0x4D, 0x51, 0x40
PROG_BYTES, COMBI_BYTES = 143, 124


def s8(b):
    return b - 256 if b > 127 else b


def unpack7(data):
    """Korg's packing: one byte with the bit 7 of the seven following bytes, then the seven bytes."""
    out = bytearray()
    for g in range(0, len(data), 8):
        msb = data[g]
        for k, b in enumerate(data[g + 1:g + 8]):
            out.append(b | ((msb >> k) & 1) << 7)
    return bytes(out)


def messages(raw):
    i = 0
    while True:
        a = raw.find(b"\xF0", i)
        if a < 0:
            return
        b = raw.find(b"\xF7", a)
        if b < 0:
            return
        yield raw[a:b + 1]
        i = b + 1


def name(b):
    return b[:10].decode("ascii", "replace").rstrip()


def eg_switches(v):
    """*1: bit 0..3 attack, decay, slope, release on; bit 4..7 their polarity (0 +, 1 -)"""
    st = ("attack", "decay", "slope", "release")
    return {s: {"on": bool(v >> i & 1), "neg": bool(v >> (4 + i) & 1)} for i, s in enumerate(st)}


def osc(p, o):
    """parameters 63..102 for OSC 1 (o = 0) or 103..142 for OSC 2 (o = 40)"""
    b = lambda n: p[n + o]
    sb = lambda n: s8(p[n + o])
    return {
        "pitch_eg": {"start_level": sb(63), "attack_time": b(64), "attack_level": sb(65), "decay_time": b(66),
                     "release_time": b(67), "release_level": sb(68), "time_vel": sb(69), "level_vel": sb(70)},
        "vdf": {"cutoff": b(71), "kbd_center": b(72), "kbd_track": sb(73), "eg_int": b(74), "eg_time_kbd": b(75),
                "eg_time_vel": b(76), "eg_int_vel": sb(77)},
        "vdf_eg": {"attack_time": b(78), "attack_level": sb(79), "decay_time": b(80), "break_point": sb(81),
                   "slope_time": b(82), "sustain_level": sb(83), "release_time": b(84), "release_level": sb(85)},
        "vda": {"level": b(86), "kbd_center": b(87), "kbd_track": sb(88), "vel": sb(89), "eg_time_kbd": b(90),
                "eg_time_vel": b(91)},
        "vda_eg": {"attack_time": b(92), "attack_level": b(93), "decay_time": b(94), "break_point": b(95),
                   "slope_time": b(96), "sustain_level": b(97), "release_time": b(98)},
        "vdf_eg_time_kbd_sw": eg_switches(b(99)), "vdf_eg_time_vel_sw": eg_switches(b(100)),
        "vda_eg_time_kbd_sw": eg_switches(b(101)), "vda_eg_time_vel_sw": eg_switches(b(102)),
    }


def mg(p, n):
    v = p[n]
    return {"wave": ("triangle", "saw_up", "saw_down", "square")[v & 3], "osc1": bool(v >> 5 & 1),
            "osc2": bool(v >> 6 & 1), "key_sync": bool(v >> 7 & 1), "freq": p[n + 1], "delay": p[n + 2],
            "intensity": p[n + 3]}


def effects(p, at):
    """*11: 25 bytes of effect parameters"""
    e = p[at:at + 25]
    return {"type1": e[0], "type2": e[1], "balance": [e[2], e[3], e[4], e[5]], "pan3": e[6], "pan4": e[7],
            "io": e[8], "param1": list(e[9:17]), "param2": list(e[17:25])}


def program(p, idx):
    return {
        "index": idx, "name": name(p),
        "mode": ("single", "double", "drum")[p[10]] if p[10] < 3 else p[10],
        "mono": bool(p[11] & 1), "hold": bool(p[11] >> 1 & 1),
        "osc1_ms": p[12], "osc1_octave": s8(p[13]), "osc2_ms": p[14], "osc2_octave": s8(p[15]),
        "interval": s8(p[16]), "detune": s8(p[17]), "delay_start": p[18],
        "pitch_mg": mg(p, 19), "vdf_mg": mg(p, 23),
        "aftertouch": {"pitch": s8(p[27]), "pitch_mg": p[28], "vdf_cutoff": s8(p[29]), "vdf_mg": p[30], "vda_amp": s8(p[31])},
        "joystick": {"pitch_bend": s8(p[32]), "vdf_sweep": s8(p[33]), "pitch_mg_int": p[34], "pitch_mg_freq": p[35],
                     "vdf_mg_int": p[36], "vdf_mg_freq": p[37]},
        "effects": effects(p, 38),
        "osc1": osc(p, 0), "osc2": osc(p, 40),
    }


def combination(c, idx):
    t = []
    for k in range(8):
        b = c[36 + 11 * k:47 + 11 * k]
        t.append({"program": b[0], "level": b[1], "transpose": s8(b[2]), "detune": s8(b[3]),
                  "pan": b[4] & 15, "inst": bool(b[4] >> 7 & 1), "key_top": b[5], "key_bottom": b[6],
                  "vel_top": b[7], "vel_bottom": b[8], "control_filter": b[9] & 15,
                  "on": not (b[10] >> 4 & 1), "channel": b[10] & 15})
    return {"index": idx, "name": name(c),
            "type": ("single", "layer", "split", "vel_switch", "multi")[c[10]] if c[10] < 5 else c[10],
            "effects": effects(c, 11), "timbres": t}


def global_data(g):
    kits = []
    for k in range(4):
        keys = []
        for i in range(30):
            b = g[21 + 7 * (30 * k + i):28 + 7 * (30 * k + i)]
            if b[0] == 0:
                continue
            keys.append({"drum": b[0], "key": b[1], "pan": b[2], "tune": s8(b[3]), "level": s8(b[4]), "decay": s8(b[5])})
        kits.append(keys)
    return {"master_tune": s8(g[0]), "key_transpose": s8(g[1]), "scale_type": g[5], "pure_key": g[6],
            "user_scale": [s8(x) for x in g[7:19]], "drum_kits": kits}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("syx")
    ap.add_argument("--out")
    a = ap.parse_args()
    res = {}
    for m in messages(open(a.syx, "rb").read()):
        if len(m) < 6 or m[1] != KORG or m[3] != M1:
            continue
        f = m[4]
        if f in (FUNC_ALL_PROG, FUNC_ALL_COMBI, FUNC_GLOBAL):
            d = unpack7(m[6:-1])                 # m[5]: memory allocation and bank (NOTE 3)
            if f == FUNC_ALL_PROG:
                n = len(d) // PROG_BYTES
                res["programs"] = [program(d[i * PROG_BYTES:(i + 1) * PROG_BYTES], i) for i in range(n)]
            elif f == FUNC_ALL_COMBI:
                n = len(d) // COMBI_BYTES
                res["combinations"] = [combination(d[i * COMBI_BYTES:(i + 1) * COMBI_BYTES], i) for i in range(n)]
            else:
                res["global"] = global_data(d)
        elif f == FUNC_PROG:
            d = unpack7(m[5:-1])
            res.setdefault("programs", []).append(program(d[:PROG_BYTES], len(res.get("programs", []))))
    if not res:
        sys.exit("no M1 dumps in the file")
    P, C = res.get("programs", []), res.get("combinations", [])
    print(f"{len(P)} programs, {len(C)} combinations, global data {'yes' if 'global' in res else 'no'}")
    for p in P[:6]:
        print(f"  P{p['index']:02d} {p['name']:10} {p['mode']:6} OSC1 ms {p['osc1_ms']:2} OSC2 ms {p['osc2_ms']:2} "
              f"VDF {p['osc1']['vdf']['cutoff']:2} VDA-EG A{p['osc1']['vda_eg']['attack_time']} D{p['osc1']['vda_eg']['decay_time']} "
              f"S{p['osc1']['vda_eg']['sustain_level']} R{p['osc1']['vda_eg']['release_time']}")
    if "global" in res:
        print("  drum kits:", [len(k) for k in res["global"]["drum_kits"]], "keys")
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1, ensure_ascii=False)
        print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
