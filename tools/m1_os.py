#!/usr/bin/env python3
"""Läser multisound-, sampel- och trumtabellerna ur användarens egen Korg M1 OS-EPROM (IC23 hög + IC32 låg, 2 x 64 KB)
och skriver dem som JSON. Innehåller ingen Korg-data; formatet är uppmätt på OS v1.9 (rev19), se docs/M1_ROM.md.

  python -I tools/m1_os.py "M1 files/rev19-ic23-880219.bin" "M1 files/rev19-ic32-880319.bin" --pcm roms/gen/m1_pcm.bin \
      --out roms/gen/m1_tables.json [--wav roms/gen/ms]

--pcm kontrollerar loopskarvarna mot PCM:en (tools/m1_rom.py --out); --wav skriver varje multisounds zoner och varje trumljud
som WAV (31 250 Hz) för att lyssna på.
"""
import argparse, array, json, os, statistics, struct, sys, wave

PCM_RATE = 31250
NAME_TABLE_PROBE = b"Piano     "   # första multisoundens namn: tabellen hittas på det, inte på en fast adress
DWGS_BLOCK = 0x3FC                 # en DWGS-våg: cykler på 512, 256 ... 4 sampel (summa 1020)


def load_os(hi_path, lo_path):
    hi, lo = open(hi_path, "rb").read(), open(lo_path, "rb").read()
    if len(hi) != len(lo):
        sys.exit("IC23 och IC32 har olika storlek")
    img = bytearray(2 * len(hi))
    img[0::2] = lo
    img[1::2] = hi
    return bytes(img)


def parse(img):
    probe = img.find(NAME_TABLE_PROBE)
    if probe < 4:
        sys.exit("hittar inte multisound-namnen (är det en M1 OS-dump, hög och låg i rätt ordning?)")
    names_at = probe - 4                         # post: u16 zonpekare, u8 typ (2 = DWGS), u8 antal zoner, char[10] namn
    ms = []
    for n in range(100):
        o = names_at + 14 * n
        ptr, kind, nz = struct.unpack_from("<HBB", img, o)
        ms.append({"index": n, "name": img[o + 4:o + 14].decode("ascii").rstrip(), "zone_ptr": ptr,
                   "dwgs": kind == 2, "nzones": nz})
    zone_lo = min(m["zone_ptr"] for m in ms)
    zone_hi = max(m["zone_ptr"] + 10 * m["nzones"] for m in ms)
    seg = names_at - zone_hi                     # zontabellen slutar där namntabellen börjar
    if seg < 0:
        sys.exit("zontabellens segment stämmer inte")

    zones = {}
    for p in range(zone_lo, zone_hi, 10):
        sp, top, b3, fine, root, b6, b7 = struct.unpack_from("<HBBbBbb", img, seg + p)
        zones[p] = {"sample_ptr": sp, "top_key": top, "b3": b3, "fine": fine, "root_key": root, "b6": b6, "b7": b7}
    sample_lo = min(z["sample_ptr"] for z in zones.values())

    # trumljudens namn (char[10] + 1) och, före dem, deras pekare i samma segment
    first_drum_name = seg + struct.unpack_from("<H", img, names_at + 14 * 100)[0]
    drums = []
    for n in range(44):
        o = first_drum_name + 11 * n
        drums.append({"index": n, "name": img[o:o + 10].decode("ascii").rstrip()})

    # sampelposterna: från den första zonen pekar på, upp till zontabellen; 14 byte: u16 0, u32 start, u32 loop | 1<<26 (loop),
    # u32 slut | bank<<20. PCM:en är en följd över båda bankerna: Strings zon 3 och framåt ligger i bank 1, trummorna sist där.
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
    for k, m in enumerate(dwgs):                 # DWGS-våg k: blocket k * 0x3FC, zonernas poster är platser inom blocket
        m["dwgs_wave"] = k
        m["dwgs_base"] = k * DWGS_BLOCK

    # trumljuden: de 44 sampelposter i följd som ingen zon använder och som går i en kedja (slut + 2 = nästa start)
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
        if s["loop"]:                            # loopen ett par gånger, upp till en sekund
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
    print(f"{len(ms)} multisounds ({sum(m['dwgs'] for m in ms)} DWGS), {sum(len(m['zones']) for m in ms)} zoner, "
          f"{len(sa)} sampelposter, {len(dr)} trumljud ({sum('sample' in d for d in dr)} med sampel)")
    top = max(s["end"] for s in sa)
    print(f"högsta PCM-adress {top:#x} (av 0x200000 i dumpen)")
    if a.pcm:
        pcm = array.array("h")
        pcm.frombytes(open(a.pcm, "rb").read())
        seams = check(tab, pcm)
        print(f"loopskarvar: {len(seams)}, median {statistics.median(seams):.2f} av ett normalsteg "
              f"(rätt tolkning: under ~0.5; fel: ~2-3)")
        if a.wav:
            render(tab, pcm, a.wav)
            print(f"WAV-filer i {a.wav}")
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(tab, f, indent=1, ensure_ascii=False)
        print(f"skrev {a.out}")


if __name__ == "__main__":
    main()
