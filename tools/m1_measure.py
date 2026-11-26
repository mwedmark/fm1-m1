#!/usr/bin/env python3
"""Spelar ett test på en FM-1 med M1-firmware över USB-MIDI och läser av dess mätvärden (fm1base/app.c app_sysex):
CPU, missade ljudblock, röster, CPU-vaktens kapningar och live cachens räknare. Behöver mido med python-rtmidi.

  python tools/m1_measure.py [--port "M1 FM-1"] [--test chords|programs|drums|full|all]

chords: åttaklangsackord på program 0, 31 och 5 (16 röster); programs: ackord på varannan multisound i snabb takt (cachen tvingas
avkoda); drums: trumljuden på kanal 10; full: 40 toner Strings samtidigt (alla röster). Under och efter varje del skrivs mätvärdena ut som skillnad mot början.
"""
import argparse, sys, time

NAMES = ["cpu_medel", "cpu_topp", "cpu_nu", "missade_block", "röster", "röstgräns", "röster_kapade", "vakt_kapade",
         "cache_träff", "cache_miss", "cache_utkastade", "cache_utan_plats", "mp3_ramar", "cache_begärda", "noter_tappade",
         "cache_väntar"]
CUMULATIVE = {"missade_block", "röster_kapade", "vakt_kapade", "cache_träff", "cache_miss", "cache_utkastade",
              "cache_utan_plats", "mp3_ramar", "cache_begärda", "noter_tappade"}


def main():
    import mido
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="M1 FM-1")
    ap.add_argument("--test", default="all")
    a = ap.parse_args()
    inn = next((n for n in mido.get_input_names() if a.port in n), None)
    out = next((n for n in mido.get_output_names() if a.port in n), None)
    if not inn or not out:
        sys.exit(f"ingen MIDI-port med {a.port!r}: {mido.get_input_names()}")
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
            sys.exit("FM-1 svarar inte på mätfrågan (kör den M1-firmware med APP_SYSEX?)")

        def note(ch, key, vel):
            mo.send(mido.Message("note_on" if vel else "note_off", channel=ch, note=key, velocity=vel))

        def show(label, before, now, peaks):
            diff = {k: now[k] - before[k] if k in CUMULATIVE else now[k] for k in NAMES}
            print(f"\n== {label}")
            print(f"  CPU: medel {max(p['cpu_medel'] for p in peaks)} % (högsta medel), topp {max(p['cpu_topp'] for p in peaks)} %; "
                  f"röster högst {max(p['röster'] for p in peaks)}, gräns lägst {min(p['röstgräns'] for p in peaks)}")
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
            keys = list(range(36, 76))         # 40 toner på Strings (loopad, håller): alla röster
            st = [(mo.send, mido.Message("program_change", channel=0, program=31))]
            st += [(note, 0, k, 100) for k in keys] + [("wait", 4000)] + [(note, 0, k, 0) for k in keys]
            return st + [("wait", 800)]

        def effects():                         # programmen med M1-effekter som firmware gör själv (m1_fx2.c): 8-tonsackord vardera
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
