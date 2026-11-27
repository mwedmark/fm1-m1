#!/usr/bin/env python3
"""Plays a test on an FM-1 with the M1 firmware over USB-MIDI and reads its measurements (fm1base/app.c app_sysex):
CPU, missed audio blocks, voices, the CPU guard's cuts and the live cache's counters. Needs mido with python-rtmidi.

  python tools/m1_measure.py [--port "M1 FM-1"] [--test chords|programs|drums|full|all]

chords: eight-note chords on programs 0, 31 and 5 (16 voices); programs: chords on every other multisound at a fast pace (the cache is forced to
decode); drums: the drum sounds on channel 10; full: 40 notes of Strings at once (all voices). During and after each part the measurements are printed as the difference from the start.
"""
import argparse, sys, time

NAMES = ["cpu_avg", "cpu_peak", "cpu_now", "missed_blocks", "voices", "voice_limit", "voices_cut", "guard_cut",
         "cache_hit", "cache_miss", "cache_evicted", "cache_no_room", "mp3_frames", "cache_requested", "notes_dropped",
         "cache_waiting"]
CUMULATIVE = {"missed_blocks", "voices_cut", "guard_cut", "cache_hit", "cache_miss", "cache_evicted",
              "cache_no_room", "mp3_frames", "cache_requested", "notes_dropped"}


def main():
    import mido
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="M1 FM-1")
    ap.add_argument("--test", default="all")
    a = ap.parse_args()
    inn = next((n for n in mido.get_input_names() if a.port in n), None)
    out = next((n for n in mido.get_output_names() if a.port in n), None)
    if not inn or not out:
        sys.exit(f"no MIDI port with {a.port!r}: {mido.get_input_names()}")
    with mido.open_input(inn) as mi, mido.open_output(out) as mo:
        def ask():
            for _ in mi.iter_pending():
                pass
            mo.send(mido.Message("sysex", data=[0x7D, ord("M"), ord("1"), 1]))
            t = time.time() + 1.0
            while time.time() < t:
                for m in mi.iter_pending():
                    d = list(m.data) if m.type == "sysex" else []
                    if d[:4] == [0x7D, ord("M"), ord("1"), 0x41] and len(d) == 4 + 80:
                        return {NAMES[i]: sum(d[4 + 5 * i + k] << (7 * k) for k in range(5)) for i in range(16)}
                time.sleep(0.005)
            sys.exit("the FM-1 does not answer the measurement query (is it running the M1 firmware with APP_SYSEX?)")

        def note(ch, key, vel):
            mo.send(mido.Message("note_on" if vel else "note_off", channel=ch, note=key, velocity=vel))

        def show(label, before, now, peaks):
            diff = {k: now[k] - before[k] if k in CUMULATIVE else now[k] for k in NAMES}
            print(f"\n== {label}")
            print(f"  CPU: average {max(p['cpu_avg'] for p in peaks)} % (highest average), peak {max(p['cpu_peak'] for p in peaks)} %; "
                  f"voices at most {max(p['voices'] for p in peaks)}, limit lowest {min(p['voice_limit'] for p in peaks)}")
            print("  " + ", ".join(f"{k} {diff[k]}" for k in NAMES if k in CUMULATIVE))

        def run(label, steps):
            before = ask()
            peaks = []
            for s in steps:
                if s[0] == "wait":
                    end = time.time() + s[1] / 1000
                    while time.time() < end:
                        time.sleep(0.25)
                        peaks.append(ask())
                else:
                    s[0](*s[1:])
            time.sleep(0.8)
            peaks.append(ask())
            show(label, before, ask(), peaks)

        chord = [48, 52, 55, 60, 64, 67, 72, 76]
        def chords():
            st = []
            for prog in (0, 31, 5):
                st.append((mo.send, mido.Message("program_change", channel=0, program=prog)))
                st += [(note, 0, k, 100) for k in chord] + [("wait", 2500)] + [(note, 0, k, 0) for k in chord] + [("wait", 600)]
            return st
        def programs():
            st = []
            for prog in range(0, 77, 2):
                st.append((mo.send, mido.Message("program_change", channel=0, program=prog)))
                st += [(note, 0, k, 90) for k in chord[:4]] + [("wait", 350)] + [(note, 0, k, 0) for k in chord[:4]]
            return st + [("wait", 500)]
        def drums():
            st = []
            for k in range(36, 80):
                st += [(note, 9, k, 120), ("wait", 120), (note, 9, k, 0)]
            return st + [("wait", 1500)]

        def full():
            keys = list(range(36, 76))         # 40 notes on Strings (looped, sustains): all voices
            st = [(mo.send, mido.Message("program_change", channel=0, program=31))]
            st += [(note, 0, k, 100) for k in keys] + [("wait", 4000)] + [(note, 0, k, 0) for k in keys]
            return st + [("wait", 800)]

        def effects():                         # the programs with M1 effects that the firmware makes itself (m1_fx2.c): an 8-note chord each
            st = []
            for prog in (14, 74, 15, 67, 23, 43, 61, 13, 20, 38):
                st.append((mo.send, mido.Message("program_change", channel=0, program=prog)))
                st += [(note, 0, k, 100) for k in chord] + [("wait", 2000)] + [(note, 0, k, 0) for k in chord] + [("wait", 400)]
            return st

        print("FM-1:", inn, "start:", ask())
        tests = {"chords": chords, "programs": programs, "drums": drums, "full": full, "effects": effects}
        for name in (tests if a.test == "all" else [a.test]):
            run(name, tests[name]())
        mo.send(mido.Message("control_change", channel=0, control=123, value=0))
        mo.send(mido.Message("control_change", channel=9, control=123, value=0))


if __name__ == "__main__":
    main()
