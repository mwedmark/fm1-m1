#!/usr/bin/env python3
"""Reads the multisound, sample and drum tables from the user's own Korg M1 OS EPROM (IC23 high + IC32 low, 2 x 64 KB)
and writes them as JSON. Contains no Korg data; the format was measured on OS v1.9 (rev19), see docs/M1_ROM.md.

  python -I tools/m1_os.py "M1 files/rev19-ic23-880219.bin" "M1 files/rev19-ic32-880319.bin" --pcm roms/gen/m1_pcm.bin \
      --out roms/gen/m1_tables.json [--wav roms/gen/ms]

--pcm checks the loop seams against the PCM (tools/m1_rom.py --out); --wav writes each multisound's zones and each drum sound
as WAV (31 250 Hz) to listen to.
"""
import argparse, array, json, os, statistics, struct, sys, wave

PCM_RATE = 31250
NAME_TABLE_PROBE = b"Piano     "   # the first multisound's name: the table is found by it, not at a fixed address
DWGS_BLOCK = 0x3FC                 # one DWGS wave: cycles of 512, 256 ... 4 samples (sum 1020)


def load_os(hi_path, lo_path):
    hi, lo = open(hi_path, "rb").read(), open(lo_path, "rb").read()
    if len(hi) != len(lo):
        sys.exit("IC23 and IC32 differ in size")
    img = bytearray(2 * len(hi))
    img[0::2] = lo
    img[1::2] = hi
    return bytes(img)


def parse(img):
    probe = img.find(NAME_TABLE_PROBE)
    if probe < 4:
        sys.exit("cannot find the multisound names (is it an M1 OS dump, high and low in the right order?)")
    names_at = probe - 4                         # record: u16 zone pointer, u8 type (2 = DWGS), u8 number of zones, char[10] name
    ms = []
    for n in range(100):
        o = names_at + 14 * n
        ptr, kind, nz = struct.unpack_from("<HBB", img, o)
        ms.append({"index": n, "name": img[o + 4:o + 14].decode("ascii").rstrip(), "zone_ptr": ptr,
                   "dwgs": kind == 2, "nzones": nz})
    zone_lo = min(m["zone_ptr"] for m in ms)
    zone_hi = max(m["zone_ptr"] + 10 * m["nzones"] for m in ms)
    seg = names_at - zone_hi                     # the zone table ends where the name table begins
    if seg < 0:
        sys.exit("the zone table's segment does not add up")

    zones = {}
    for p in range(zone_lo, zone_hi, 10):
        sp, top, b3, fine, root, b6, b7 = struct.unpack_from("<HBBbBbb", img, seg + p)
        zones[p] = {"sample_ptr": sp, "top_key": top, "b3": b3, "fine": fine, "root_key": root, "b6": b6, "b7": b7}
    sample_lo = min(z["sample_ptr"] for z in zones.values())

    # the drum sounds' names (char[10] + 1) and, before them, their pointers in the same segment
    first_drum_name = seg + struct.unpack_from("<H", img, names_at + 14 * 100)[0]
    drums = []
    for n in range(44):
        o = first_drum_name + 11 * n
        drums.append({"index": n, "name": img[o:o + 10].decode("ascii").rstrip()})

    # the sample records: from the first one a zone points to, up to the zone table; 14 bytes: u16 0, u32 start, u32 loop | 1<<26 (loop),
    # u32 end | bank<<20. The PCM is one sequence across both banks: Strings zone 3 onwards is in bank 1, the drums last there.
    samples = []
    by_ptr = {}
    for p in range(sample_lo, zone_lo, 14):
        z0, s, l, e = struct.unpack_from("<HIII", img, seg + p)
        rec = {"ptr": p, "start": s & 0xFFFFF, "loop_start": l & 0xFFFFF, "end": e & 0xFFFFF,
               "loop": bool(l >> 26 & 1), "bank": e >> 20 & 1, "flags": [z0, s >> 20, l >> 20, e >> 20]}
        by_ptr[p] = len(samples)
        samples.append(rec)

    for m in ms:
        m["zones"] = []
        for k in range(m["nzones"]):
            z = dict(zones[m["zone_ptr"] + 10 * k])
            z["sample"] = by_ptr.get(z.pop("sample_ptr"))
            m["zones"].append(z)
        del m["zone_ptr"]
    dwgs = [m for m in ms if m["dwgs"]]
    for k, m in enumerate(dwgs):                 # DWGS wave k: the block k * 0x3FC, the zones' records are positions within the block
        m["dwgs_wave"] = k
        m["dwgs_base"] = k * DWGS_BLOCK

    # the drum sounds: the 44 consecutive sample records that no zone uses and that form a chain (end + 2 = next start)
    used = {z["sample"] for m in ms for z in m["zones"]}
    free = [i for i in range(len(samples)) if i not in used]
    run = []
    for i in free:
        if run and i == run[-1] + 1:
            run.append(i)
        elif len(run) < 44:
            run = [i]
    run = [i for i in run if samples[i]["flags"][0] == 0 and samples[i]["start"] < samples[i]["end"]][-44:]
    if len(run) == 44:
        for d, i in zip(drums, run):
            d["sample"] = i
    return {"pcm_rate": PCM_RATE, "bank_samples": 0x100000, "segment": seg, "multisounds": ms, "drums": drums,
            "samples": samples}


def check(tab, pcm):
    seams = []
    for s in tab["samples"]:
        base = tab["bank_samples"] * s.get("bank", 0)
        l, e = base + s["loop_start"], base + s["end"]
        if s["loop"] and 0 < l < e <= len(pcm) and e - l > 8:
            step = statistics.mean(abs(pcm[i] - pcm[i - 1]) for i in range(l + 1, e)) or 1
            seams.append(abs(pcm[e - 1] - pcm[l - 1]) / step)
    return seams


def write_wav(path, frames):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(PCM_RATE)
        w.writeframes(frames.tobytes())


def render(tab, pcm, folder):
    os.makedirs(folder, exist_ok=True)
    gap = array.array("h", bytes(2 * (PCM_RATE // 4)))
    def one(s, base=0):
        out = array.array("h", pcm[base + s["start"]:base + s["end"]])
        if s["loop"]:                            # the loop a couple of times, up to one second
            l, e = base + s["loop_start"], base + s["end"]
            while len(out) < PCM_RATE and e > l:
                out += pcm[l:e]
        return out
    for m in tab["multisounds"]:
        frames = array.array("h")
        for z in m["zones"]:
            s = tab["samples"][z["sample"]]
            frames += one(s, m.get("dwgs_base", 0)) + gap
        write_wav(os.path.join(folder, f"ms{m['index']:02d}_{m['name'].replace(' ', '_').replace('/', '_')}.wav"), frames)
    for d in tab["drums"]:
        if "sample" in d:
            write_wav(os.path.join(folder, f"drum{d['index']:02d}_{d['name'].replace(' ', '_')}.wav"),
                      one(tab["samples"][d["sample"]], tab["bank_samples"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ic23_hi")
    ap.add_argument("ic32_lo")
    ap.add_argument("--pcm")
    ap.add_argument("--out")
    ap.add_argument("--wav")
    a = ap.parse_args()
    tab = parse(load_os(a.ic23_hi, a.ic32_lo))
    ms, dr, sa = tab["multisounds"], tab["drums"], tab["samples"]
    print(f"{len(ms)} multisounds ({sum(m['dwgs'] for m in ms)} DWGS), {sum(len(m['zones']) for m in ms)} zones, "
          f"{len(sa)} sample records, {len(dr)} drum sounds ({sum('sample' in d for d in dr)} with a sample)")
    top = max(s["end"] for s in sa)
    print(f"highest PCM address {top:#x} (of 0x200000 in the dump)")
    if a.pcm:
        pcm = array.array("h")
        pcm.frombytes(open(a.pcm, "rb").read())
        seams = check(tab, pcm)
        print(f"loop seams: {len(seams)}, median {statistics.median(seams):.2f} of a normal step "
              f"(correct reading: under ~0.5; wrong: ~2-3)")
        if a.wav:
            render(tab, pcm, a.wav)
            print(f"WAV files in {a.wav}")
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(tab, f, indent=1, ensure_ascii=False)
        print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
