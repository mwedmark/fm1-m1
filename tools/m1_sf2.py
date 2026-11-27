#!/usr/bin/env python3
"""Makes a SoundFont (.sf2) from the M1 tables (tools/m1_os.py), the PCM (tools/m1_rom.py) and, with --patches, the M1's programs and
drum kits (tools/m1_syx.py). fm1-sf2's converter then turns it into an FM1B bank (make_bank.js, pack_bank.js). The file contains
Korg's samples and programs from YOUR dump: it is for your own use only and does not belong in git or online (roms/ is ignored).

  python -I tools/m1_sf2.py roms/gen/m1_tables.json roms/gen/m1_pcm.bin roms/gen/m1.sf2 --patches roms/gen/m1_patches.json

With --patches: bank 0, program 0..99 = the M1's programs (OSC 1 and, for Double, OSC 2 as a layer; Drum programs play their drum kit),
bank 128, program 0..3 = the drum kits. Without: bank 0 = the raw multisounds, bank 128 = the drum sounds on key 36 and up.

How an M1 program becomes SF2 (approximations, see docs/M1_PROGRAMS.md): VDA-EG -> volume envelope (the M1's decay and slope become one
decay), VDF -> low-pass with cutoff and VDF-EG as the modulation envelope to cutoff, pitch MG -> vibrato LFO, VDF MG -> mod LFO to
cutoff, octave and interval -> coarse tuning, detune -> fine tuning, VDA level -> attenuation, effect type -> reverb/chorus send.
The M1's times and levels 0..99 are converted with the curves below (estimated: adjusted against recordings of real M1 programs).
"""
import argparse, array, json, math, struct, sys

# SF2-generatorer
G = dict(modLfoToPitch=5, vibLfoToPitch=6, modEnvToPitch=7, m1VelLevel=14, m1VelFc=18, m1FxLeft=19, m1FxRight=20, m1FxFeedback=42, m1FxFlags=49, m1ChoRate=55, m1ChoDelay=59, m1KeyFc=63, m1KeyCenter=64, m1ProgIdx=65, initialFilterFc=8, modLfoToFilterFc=10, modEnvToFilterFc=11, chorusSend=15,
         reverbSend=16, pan=17, delayModLFO=21, freqModLFO=22, delayVibLFO=23, freqVibLFO=24, attackModEnv=26, decayModEnv=28,
         sustainModEnv=29, releaseModEnv=30, attackVolEnv=34, holdVolEnv=35, decayVolEnv=36, sustainVolEnv=37,
         releaseVolEnv=38, instrument=41, keyRange=43, initialAttenuation=48, coarseTune=51, fineTune=52, sampleID=53,
         sampleModes=54, overridingRootKey=58)
LOOP_GUARD = 8                     # SF2: eight valid points after the loop end (copies of the loop's start)
RAW_RELEASE_S = 0.3
RAW_DRUM_KEY0 = 36


# ---- the M1's 0..99 in physical units (estimated) ----
def m1_time(p):
    """EG time 0..99 in seconds for a whole step (linear level): 0 instant, 11 about 0.12 s, 33 about 0.45 s, 62 about 1.2 s,
    99 about 3.1 s. Fitted to the releases in the factory examples (Strings R33, Choir R44, Lore R62: 20 dB in 0.4, 0.6 and 1.1 s)
    and the attacks (Choir, Strings A11-12 under 0.1-0.2 s)."""
    return 0.0 if p <= 0 else 0.002 + 0.457 * (1.021 ** p - 1)


FALL = 4.5       # SF2's decay and release are the time for 100 dB at a dB-linear rate. The factor is measured: with 4.5 Choir,
                 # Strings and Lore fall after the release, and Vibes (D89) and A.Bass (D75) in decay, as in the factory examples (10-50 dB/s)


def tc(seconds):
    return -12000 if seconds <= 0.001 else max(-12000, min(8000, round(1200 * math.log2(seconds))))


def mg_hz(p):
    """MG frequency 0..99: about 0.06 Hz to 20 Hz"""
    return 0.06 * 1.06 ** p


def lfo_cents_abs(hz):
    return max(-16000, min(4500, round(1200 * math.log2(hz / 8.176))))


def cB_ratio(a, b):
    """attenuation in centibels for level a compared with b (a, b > 0)"""
    return max(0, min(1440, round(200 * math.log10(b / a))))


# The M1's effect types (Owner's Manual p. 40-53). In the program data the type is the manual's number minus 1 (0 = Hall): the parameters show it
# (type 0 has the reverb's layout, 9 and 10 the delay's, 17 tremolo in Vibes, 24 rotary in Organ 1). The engine has reverb, chorus and a
# delay on the whole output (fm1base/m1_fx.c); each type gets (bus, weight) for the reverb and chorus sends:
#   0-5 hall, room, live stage: reverb 1.0. 6-8 early reflection: reverb 0.6.
#   9 stereo delay, 10 cross delay, 28 delay/delay: only the delay.
#   11-12 chorus, 13-14 flanger, 15-16 phaser, 23 symphonic ensemble, 24 rotary: chorus 1.0.
#   25-27 delay/hall, delay/room, delay/early reflection: delay and reverb 0.8. 29-30 delay/chorus, delay/flanger: delay and chorus.
#   31-32 delay/phaser, delay/tremolo: delay and chorus 0.5 (phaser) or nothing (tremolo).
#   17-18 tremolo, 19 EQ, 20 overdrive, 21 distortion, 22 exciter: nothing.
DRUM_RELEASE_S = 3.0                         # drums: release after key up (they play to the end)
FX_BUS = {**{t: ("reverb", 1.0) for t in range(0, 6)}, **{t: ("reverb", 0.6) for t in (6, 7, 8)},
          **{t: ("chorus", 1.0) for t in (11, 12, 15, 16, 29, 30)},      # 13-14 flanger, 23 symphonic, 24 rotary: effects of their own (m1_fx2.c)
          **{t: ("reverb", 0.8) for t in (25, 26, 27)}, 31: ("chorus", 0.5)}
DELAY_TYPES = {9: "stereo", 10: "cross", 25: "a", 26: "a", 27: "a", 28: "dual", 29: "a", 30: "a", 31: "a", 32: "a"}


def m1_mod_mhz(v):
    """MG rate (table *11-3-2): 0..99 = 0.03..3.00 Hz in steps of 0.03; 100..199 = 3.1..13.0 in steps of 0.1; 200..216 = 14..30"""
    if v <= 99:
        return 30 * (v + 1)
    if v <= 199:
        return 3100 + 100 * (v - 100)
    return 14000 + 1000 * min(16, v - 200)


CHORUS_PARAMS = {11: (0, 1, 4), 12: (0, 1, 4), 13: (0, 1, 4), 14: (0, 1, 4), 15: (0, 1, None), 16: (0, 1, None),
                 29: (4, 5, None), 30: (4, 5, None)}   # (depth, rate, delay) in the effect's 8 parameter bytes (table *11-3)


def chorus_fx(e):
    """The program's chorus/flanger/phaser as (rate mHz, depth 0..99, delay ms) for the engine's chorus, or None (fm1-sf2's fixed one)"""
    for t, p in ((e["type1"], e["param1"]), (e["type2"], e["param2"])):
        if t not in CHORUS_PARAMS:
            continue
        di, si, dl = CHORUS_PARAMS[t]
        delay = p[dl] if dl is not None else (1 if t in (15, 16) else 5)   # phaser: short; delay/chorus: 5 ms
        return m1_mod_mhz(p[si]), min(99, p[di]), max(1, min(200, delay))
    return None


def delay_fx(e):
    """The program's delay (the first effect that has one) as (left ms, right ms, feedback -99..99, high damp 0..99,
    share 0..100, crossed), or None. The parameters per table *11-3: stereo/cross delay: time L (2 bytes), feedback, high damp,
    time R (2 bytes); the combination types: the delay's time (2 bytes), feedback, high damp on channel A, the share is channel A's balance."""
    s8 = lambda v: v - 256 if v > 127 else v
    for t, p, bal in ((e["type1"], e["param1"], e["balance"][0:2]), (e["type2"], e["param2"], e["balance"][2:4])):
        kind = DELAY_TYPES.get(t)
        if not kind:
            continue
        left = min(500, p[0] | p[1] << 8)
        if kind in ("stereo", "cross"):
            right, share = min(500, p[4] | p[5] << 8), sum(bal) / 2
        elif kind == "dual":
            right, share = min(500, p[4] | p[5] << 8), sum(bal) / 2
        else:
            right, share = left, bal[0]
        if share <= 0 or (left == 0 and right == 0):
            return None
        return left, right, max(-99, min(99, s8(p[2]))), min(99, p[3]), min(100, round(share)), kind == "cross"
    return None


def chunk(tag, data):
    pad = b"\0" if len(data) & 1 else b""
    return tag + struct.pack("<I", len(data)) + data + pad


def name20(s):
    return s.encode("ascii", "replace")[:19].ljust(20, b"\0")


class Builder:
    def __init__(self, tab, pcm):
        self.tab, self.pcm = tab, pcm
        self.smpl = array.array("h")
        self.shdr = []             # (name, start, end, loop start, loop end, loop)
        self.ids = {}
        self.insts = []            # (name, [[(gen, value), ...] per zone])

    def sample(self, i, base, label):
        key = (i, base)
        if key in self.ids:
            return self.ids[key]
        s, pcm = self.tab["samples"][i], self.pcm
        st, lp, en = base + s["start"], base + s["loop_start"], base + s["end"]
        if not (0 <= st < en <= len(pcm)):
            return None
        loop = s["loop"] and 0 <= lp < en
        if loop and st - 16 <= lp < st:             # the loop starts just before the start (the whole sample loops): include those samples
            st = lp
        at = len(self.smpl)
        self.smpl.extend(pcm[st:en])
        if loop and lp < st:                        # the loop lies before the start (DWGS, Rhythm): play start..end, then the loop
            ls = len(self.smpl)
            self.smpl.extend(pcm[lp:en])
        else:
            ls = at + (lp - st)
        le = len(self.smpl)
        if loop:
            for k in range(LOOP_GUARD):
                self.smpl.append(pcm[lp + k % (en - lp)])
        end = len(self.smpl)
        self.smpl.extend([0] * 46)
        self.shdr.append((label, at, end, ls if loop else at, le if loop else at, loop))
        self.ids[key] = len(self.shdr) - 1
        return self.ids[key]

    def multisound_zones(self, ms_index, common, label):
        """The M1 multisound's zones as SF2 zones, each with common (generators) before its own"""
        m = self.tab["multisounds"][ms_index]
        base = m.get("dwgs_base", 0)
        out, lo = [], 0
        for k, z in enumerate(m["zones"]):
            hi = 127 if k == len(m["zones"]) - 1 else z["top_key"]
            if hi < lo or z["sample"] is None:
                lo = max(lo, hi + 1)
                continue
            bank = self.tab["bank_samples"] * self.tab["samples"][z["sample"]].get("bank", 0)
            sid = self.sample(z["sample"], base + bank, f"{m['name'][:14]}{k}")
            if sid is not None:
                fine = z["fine"] + dict(common).get("_fine", 0)
                gens = [("keyRange", (lo, hi))] + [g for g in common if not g[0].startswith("_")]
                gens += [("overridingRootKey", z["root_key"]), ("fineTune", fine),
                         ("sampleModes", 1 if self.shdr[sid][5] else 0), ("sampleID", sid)]
                out.append(gens)
            lo = hi + 1
        return out

    def drum_zones(self, kit, common):
        out = []
        bank1 = self.tab["bank_samples"]
        for k in kit:
            d = k["drum"] - 1
            if not 0 <= d < len(self.tab["drums"]) or "sample" not in self.tab["drums"][d]:
                continue
            dr = self.tab["drums"][d]
            sid = self.sample(dr["sample"], bank1 * self.tab["samples"][dr["sample"]].get("bank", 0), dr["name"][:19])
            if sid is None:
                continue
            cents = 10 * k["tune"]                  # tuning -120..120: interpreted as tenths of semitones (not verified)
            # decay per key (-99..99, 0 = the program's): the M1's kits have no choke (closing an open hi-hat with a closed one): the length is controlled by this
            # value (kit 2's open hi-hat has -22 and -99). Scale the decay time: 60 timecents (1/20 octave in time) per step; scale not verified.
            com = list(common)
            if k["decay"]:
                com = [(g, v + 60 * k["decay"] if g == "decayVolEnv" else v) if g == "decayVolEnv" else (g, v) for g, v in com]
                com = [(g, max(-12000, min(8000, v)) if g == "decayVolEnv" else v) for g, v in com]
            gens = [("keyRange", (k["key"], k["key"]))] + com
            gens += [("initialAttenuation", max(0, -6 * k["level"])) if k["level"] < 0 else ("initialAttenuation", 0),
                     ("pan", (min(k["pan"], 10) - 5) * 100), ("overridingRootKey", k["key"]),
                     ("coarseTune", int(cents / 100)), ("fineTune", int(cents % 100 if cents >= 0 else -(-cents % 100))),
                     ("sampleModes", 1 if self.shdr[sid][5] else 0), ("sampleID", sid)]
            out.append(gens)
        return out

    def write(self, path, presets):
        """presets: (name, program, bank, instrument index)"""
        inst_b, ibag, igen = b"", b"", b""
        for n, zones in self.insts:
            inst_b += name20(n) + struct.pack("<H", len(ibag) // 4)
            for gens in zones:
                ibag += struct.pack("<HH", len(igen) // 4, 0)
                for g, v in gens:
                    if g == "keyRange":
                        igen += struct.pack("<HBB", G[g], v[0], v[1])
                    elif g in ("sampleID", "sampleModes", "overridingRootKey"):
                        igen += struct.pack("<HH", G[g], v)
                    else:
                        igen += struct.pack("<Hh", G[g], int(v))
        inst_b += name20("EOI") + struct.pack("<H", len(ibag) // 4)
        ibag += struct.pack("<HH", len(igen) // 4, 0)
        igen += b"\0" * 4
        phdr, pbag, pgen = b"", b"", b""
        for n, prog, bank, inst in presets:
            phdr += name20(n) + struct.pack("<HHHIII", prog, bank, len(pbag) // 4, 0, 0, 0)
            pbag += struct.pack("<HH", len(pgen) // 4, 0)
            pgen += struct.pack("<HH", G["instrument"], inst)
        phdr += name20("EOP") + struct.pack("<HHHIII", 0, 0, len(pbag) // 4, 0, 0, 0)
        pbag += struct.pack("<HH", len(pgen) // 4, 0)
        pgen += b"\0" * 4
        rate = self.tab["pcm_rate"]
        sh = b"".join(name20(n) + struct.pack("<IIIIIBbHH", st, en, ls, le, rate, 60, 0, 0, 1)
                      for n, st, en, ls, le, loop in self.shdr) + name20("EOS") + b"\0" * 26
        info = chunk(b"ifil", struct.pack("<HH", 2, 1)) + chunk(b"isng", b"EMU8000\0") + chunk(b"INAM", b"M1 (own dump)\0")
        pdta = (chunk(b"phdr", phdr) + chunk(b"pbag", pbag) + chunk(b"pmod", b"\0" * 10) + chunk(b"pgen", pgen) +
                chunk(b"inst", inst_b) + chunk(b"ibag", ibag) + chunk(b"imod", b"\0" * 10) + chunk(b"igen", igen) + chunk(b"shdr", sh))
        body = (b"sfbk" + chunk(b"LIST", b"INFO" + info) + chunk(b"LIST", b"sdta" + chunk(b"smpl", self.smpl.tobytes())) +
                chunk(b"LIST", b"pdta" + pdta))
        with open(path, "wb") as f:
            f.write(b"RIFF" + struct.pack("<I", len(body)) + body)
        used = sum(e - s for _, s, e, *_ in self.shdr)
        print(f"wrote {path}: {len(presets)} presets, {len(self.shdr)} samples, {used} sample points ({used / rate:.1f} s)")


# ---- an M1 program ----
def eg_segments(eg, al, bp):
    """The M1 EG's attack and decay + slope as SF2's attack and decay. If the level rises after the attack (break point above attack level)
    and the attack level is low, the attack lasts to the break point; if the attack already reaches half the peak (Piano: 90 and then 99) the
    remaining rise is too small to hear and the attack stays short."""
    if bp > al and al < bp / 2:
        return m1_time(eg["attack_time"]) + m1_time(eg["decay_time"]), m1_time(eg["slope_time"])
    if bp > al:
        return m1_time(eg["attack_time"]), m1_time(eg["slope_time"])
    return m1_time(eg["attack_time"]), m1_time(eg["decay_time"]) + m1_time(eg["slope_time"])


def amp_env(eg, level):
    """VDA-EG (times 0..99, levels 0..99) and the oscillator's level as SF2's volume envelope and attenuation"""
    al, bp, sl = eg["attack_level"], eg["break_point"], eg["sustain_level"]
    peak = max(al, bp, sl, 1)
    attack, decay = eg_segments(eg, al, bp)
    gens = [("attackVolEnv", tc(attack)), ("decayVolEnv", tc(max(FALL * decay, 0.002))),
            ("sustainVolEnv", 1440 if sl <= 0 else cB_ratio(sl, peak)),
            ("releaseVolEnv", tc(max(FALL * m1_time(eg["release_time"]), 0.005)))]
    atten = 1440 if level <= 0 else cB_ratio(level * peak / 99, 99)
    return gens, atten


FC_LO, FC_HI = 5000, 13500                       # VDF cutoff 0 and 99 in absolute cents (about 150 Hz and 20 kHz): estimated
FC_STEP = (FC_HI - FC_LO) / 99


def filter_gens(vdf, eg):
    """VDF: cutoff 0..99 linear in cents between FC_LO and FC_HI. VDF-EG (levels -99..99) times the EG intensity moves the cutoff
    as many steps as the cutoff itself (intensity 99 and level 99 = 99 steps). SF2's modulation envelope only goes 0..1, so the
    lowest level (sustain or 0) becomes the bottom and goes in initialFilterFc."""
    k = vdf["eg_int"] / 99 * FC_STEP                # cents per EG level
    lv = [eg["attack_level"], eg["break_point"], eg["sustain_level"]]
    hold = eg["release_level"] > eg["sustain_level"]
    if hold:                                        # the filter should open towards the release level when the key is released; SF2's envelope
        lv[2] = (eg["sustain_level"] + eg["release_level"]) // 2   # can only fall, so sustain ends up in between and
                                                    # the release holds that level (long releaseModEnv)
    lo, hi = min(min(lv), 0), max(max(lv), 0)
    fc = FC_LO + vdf["cutoff"] * FC_STEP
    if not k or hi == lo:
        return [("initialFilterFc", round(min(FC_HI, fc)))]
    if hi <= 0:                                     # only negative levels: the envelope inverted, the peak is the lowest
        span, base = round(k * (lo - 0)), fc
        al, bp, sl = (-x for x in lv)
        peak = -lo
    else:
        span, base = round(k * (hi - lo)), fc + k * lo
        al, bp, sl = (x - lo for x in lv)
        peak = hi - lo
    attack, decay = eg_segments(eg, al, bp)
    return [("initialFilterFc", round(max(1500, min(FC_HI, base)))), ("modEnvToFilterFc", span),
            ("attackModEnv", tc(attack)), ("decayModEnv", tc(max(decay, 0.002))),
            ("sustainModEnv", round(1000 * (1 - max(0, sl) / peak))),
            ("releaseModEnv", tc(20.0) if hold else tc(max(m1_time(eg["release_time"]), 0.005)))]


PITCH_CENTS = 1200 / 99                 # pitch EG level ±99 = about ±1 octave (the manual p. 24)
PITCH_WINS = 20                         # when the filter also has an envelope: the pitch EG only gets it for sweeps from level 20


def pitch_gens(peg):
    """Pitch EG: start level -> attack level (attack time) -> 0 (decay time), as SF2's modulation envelope to pitch.
    Gives (level, generators) or (0, []). Release time and release level (a glide when the key is released) do not come along:
    the envelope can only go back to 0."""
    S, A = peg["start_level"], peg["attack_level"]
    at, dt = m1_time(peg["attack_time"]), m1_time(peg["decay_time"])
    if A and dt > 0.001 and abs(A) > abs(S):          # rises to the attack level and then falls to 0
        lvl, attack, decay = A, at, dt
    elif S and at > 0.001:                             # starts at the start level and glides to the note
        lvl, attack, decay = S, 0.0, at + (dt if A else 0.0)
    else:
        return 0, []
    return lvl, [("modEnvToPitch", round(lvl * PITCH_CENTS)), ("attackModEnv", tc(attack)),
                 ("decayModEnv", tc(max(decay, 0.002))), ("sustainModEnv", 1000), ("releaseModEnv", tc(0.005))]


def mg_gens(prog, which):
    out = []
    pm, fm = prog["pitch_mg"], prog["vdf_mg"]
    if pm[which] and pm["intensity"]:
        out += [("vibLfoToPitch", round(pm["intensity"] ** 2 * 1200 / 99 ** 2)), ("freqVibLFO", lfo_cents_abs(mg_hz(pm["freq"]))),
                ("delayVibLFO", tc(m1_time(pm["delay"])))]
    if fm[which] and fm["intensity"]:
        out += [("modLfoToFilterFc", fm["intensity"] * 48), ("freqModLFO", lfo_cents_abs(mg_hz(fm["freq"]))),
                ("delayModLFO", tc(m1_time(fm["delay"])))]
    return out


def program_zones(b, prog, kits):
    e = prog["effects"]
    send = {"reverb": 0.0, "chorus": 0.0}
    for t, bal in ((e["type1"], e["balance"][0:2]), (e["type2"], e["balance"][2:4])):
        if t in FX_BUS:                          # balance 0..100 per channel: the effect's share of the output (DRY:EFF)
            bus, k = FX_BUS[t]
            send[bus] = max(send[bus], k * min(100, sum(bal) / 2) / 100)
    sends = [(f"{bus}Send", round(1000 * min(1.0, 1.5 * v))) for bus, v in send.items() if v > 0]
    # The M1's DRY:EFF lowers the dry signal by the effect share: dry × (1 - share) + effect × share. The engine adds the effect on top of full dry,
    # so the level per zone is lowered by the same factor (the send follows, the dry/effect ratio is unchanged). Without it the sum
    # went over full scale and clipped (E.Piano 3: C-E-G).
    d0 = delay_fx(e)
    dry = (1 - min(0.8, send["chorus"])) * (1 - 0.5 * min(0.8, send["reverb"])) * (1 - 0.8 * min(0.8, d0[4] / 100 if d0 else 0))
    fx_atten = round(-200 * math.log10(dry))     # cB
    d = delay_fx(e)                             # the delay (fm1base/m1_fx.c) in SF2's unused generators, bit 15 = the record exists
    fx = [("m1FxFlags", -0x8000 + ((d[5] << 14 | d[3] << 7 | d[4]) if d else 0))]
    if d:
        fx += [("m1FxLeft", d[0]), ("m1FxRight", d[1]), ("m1FxFeedback", d[2])]
    c = chorus_fx(e)                            # the chorus's shape (fm1base/m1_fx.c -> sf2fx_chorus_shape)
    if c:
        packed = min(3000, c[0] // 10) | round(c[1] * 15 / 99) << 12          # 16 bits, written signed
        fx += [("m1ChoRate", packed - 0x10000 if packed > 0x7FFF else packed), ("m1ChoDelay", c[2] * 10)]
    fx += [("m1ProgIdx", prog["index"] + 1)]     # the program number + 1: the app looks up the program's effects (m1_fx2.c) in the table at the end of the bank
    if prog["mode"] == "drum":
        kit = kits[prog["osc1_ms"]] if prog["osc1_ms"] < len(kits) else []
        env, atten = amp_env(prog["osc1"]["vda_eg"], prog["osc1"]["vda"]["level"])
        # the drum sounds play to the end: key up does not cut them off (the programs have decay 96 but release 1, which only makes sense if the sound
        # lives on after the key is released). Long release instead of the short one.
        env = [g for g in env if g[0] != "releaseVolEnv"] + [("releaseVolEnv", tc(DRUM_RELEASE_S))]
        return b.drum_zones(kit, env + sends + fx)
    zones = []
    oscs = [("osc1", prog["osc1_ms"], prog["osc1_octave"] * 12, 0)]
    if prog["mode"] == "double":
        oscs.append(("osc2", prog["osc2_ms"], prog["osc2_octave"] * 12 + prog["interval"], prog["detune"]))
    for which, ms, coarse, fine in oscs:
        o = prog[which]
        env, atten = amp_env(o["vda_eg"], o["vda"]["level"])
        if atten >= 1440 or ms > 99:
            continue
        filt = filter_gens(o["vdf"], o["vdf_eg"])
        lvl, pitch = pitch_gens(o["pitch_eg"])
        if pitch and any(g[0] == "modEnvToFilterFc" for g in filt):
            if abs(lvl) >= PITCH_WINS:                  # large sweep: the pitch gets the envelope, the filter stays at its sustain
                f = dict(filt)
                fc = f["initialFilterFc"] + f["modEnvToFilterFc"] * (1 - f["sustainModEnv"] / 1000)
                filt = [("initialFilterFc", round(max(1500, min(FC_HI, fc))))]
            else:
                pitch = []
        # velocity (tools/fm1b_make_bank.js): VDA velocity sense and VDF EG intensity velocity sense in SF2's unused generators
        vel = [("m1VelLevel", o["vda"]["vel"] + 100), ("m1VelFc", o["vdf"]["eg_int_vel"])]
        # VDF cutoff keyboard tracking (the manual p. 27): ±99 = the cutoff follows the pitch 1:1 (100 cents per key), zero at the center key
        # (MIDI note). The engine adds (note - center) × 0.1 cent × the field to the cutoff when the note starts.
        track = o["vdf"]["kbd_track"]
        key = [("m1KeyFc", round(track * 1000 / 99)), ("m1KeyCenter", o["vdf"]["kbd_center"])] if track else []
        common = env + filt + pitch + mg_gens(prog, which) + sends + vel + fx + key
        common += [("initialAttenuation", min(1440, atten + fx_atten)), ("coarseTune", coarse), ("_fine", fine)]
        zones += b.multisound_zones(ms, common, prog["name"])
    return zones


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tables")
    ap.add_argument("pcm")
    ap.add_argument("out")
    ap.add_argument("--patches")
    ap.add_argument("--variants", action="store_true", help="adds five test variants of E.Piano 3 at program slots 100-104")
    a = ap.parse_args()
    tab = json.load(open(a.tables, encoding="utf-8"))
    pcm = array.array("h")
    pcm.frombytes(open(a.pcm, "rb").read())
    b = Builder(tab, pcm)
    presets = []
    if a.patches:
        pat = json.load(open(a.patches, encoding="utf-8"))
        kits = pat["global"]["drum_kits"]
        for prog in pat["programs"]:
            b.insts.append((prog["name"], program_zones(b, prog, kits)))
            presets.append((prog["name"], prog["index"], 0, len(b.insts) - 1))
        if a.variants:                          # debugging: one thing at a time switched off in E.Piano 3 (program 91)
            import copy
            base = pat["programs"][91]
            def variant(name, edit):
                v = copy.deepcopy(base); v["name"] = name; edit(v)
                b.insts.append((name, program_zones(b, v, kits)))
                presets.append((name, 100 + variant.n, 0, len(b.insts) - 1)); variant.n += 1
            variant.n = 0
            def nofilter(v):
                v["osc1"]["vdf"].update(cutoff=99, eg_int=0, kbd_track=0)
            def att(v):
                v["osc1"]["vda_eg"]["attack_time"] = 15
            def eghalf(v):
                v["osc1"]["vdf"]["eg_int"] //= 2
            def dry(v):
                v["effects"].update(type1=19, type2=19, balance=[0, 0, 0, 0])
            def darker(v):
                v["osc1"]["vdf"]["cutoff"] = max(0, v["osc1"]["vdf"]["cutoff"] - 20)
            variant("T1 NoFilter", nofilter); variant("T2 Att 15", att); variant("T3 EG half", eghalf)
            variant("T4 Dry", dry); variant("T5 Darker", darker)
        for k, kit in enumerate(kits):
            if not kit:
                continue
            env = [("attackVolEnv", -12000), ("decayVolEnv", tc(8.0)), ("sustainVolEnv", 1440), ("releaseVolEnv", tc(DRUM_RELEASE_S))]
            b.insts.append((f"M1 Drum Kit {k + 1}", b.drum_zones(kit, env + [("reverbSend", 200)])))
            presets.append((f"M1 Drum Kit {k + 1}", k, 128, len(b.insts) - 1))
    else:
        rel = [("releaseVolEnv", tc(RAW_RELEASE_S))]
        for m in tab["multisounds"]:
            b.insts.append((m["name"], b.multisound_zones(m["index"], rel, m["name"])))
            presets.append((m["name"], m["index"], 0, len(b.insts) - 1))
        kit = [{"drum": d["index"] + 1, "key": RAW_DRUM_KEY0 + d["index"], "pan": 5, "tune": 0, "level": 0}
               for d in tab["drums"] if RAW_DRUM_KEY0 + d["index"] <= 127]
        b.insts.append(("M1 Drums", b.drum_zones(kit, rel)))
        presets.append(("M1 Drums", 0, 128, len(b.insts) - 1))
    b.write(a.out, presets)


if __name__ == "__main__":
    main()
