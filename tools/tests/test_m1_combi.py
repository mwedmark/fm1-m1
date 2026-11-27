#!/usr/bin/env python3
"""Tests of tools/m1_combi.py and fm1base/m1_combi.c on synthetic combinations (nothing of Korg's).  python -I tools/tests/test_m1_combi.py"""
import os, struct, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import m1_combi  # noqa: E402


def timbre(program, level=99, transpose=0, detune=0, pan=8, kt=127, kb=0, vt=127, vb=1, channel=0, on=True):
    return {"program": program, "level": level, "transpose": transpose, "detune": detune, "pan": pan, "inst": False,
            "key_top": kt, "key_bottom": kb, "vel_top": vt, "vel_bottom": vb, "control_filter": 0, "on": on, "channel": channel}


# the programs are 0 and 1 so that the same data also plays in fm1-sf2's default bank (tools/tests/make_combi_host_test.sh)
COMBIS = [
    {"index": 0, "name": "Single One", "type": "single", "timbres": [timbre(0), timbre(1, on=False)]},
    {"index": 1, "name": "Layer Test", "type": "layer", "timbres": [timbre(0), timbre(1, level=50, transpose=12)]},
    {"index": 2, "name": "Split Test", "type": "split", "timbres": [timbre(0, kt=59), timbre(1, kb=60)]},
    {"index": 3, "name": "VelSw Test", "type": "vel_switch", "timbres": [timbre(0, vt=63), timbre(1, vb=64)]},
    {"index": 4, "name": "Multi Test", "type": "multi", "timbres": [timbre(0, channel=2), timbre(1, channel=5)]},
]


def fx_tail(n=3):
    return bytes(range(20)) * n + struct.pack("<HH", n, 20) + b"M1FX"


class Table(unittest.TestCase):
    def test_table_layout(self):
        t = m1_combi.build_table(COMBIS)
        count, length = struct.unpack("<HH", t[-8:-4])
        self.assertEqual((count, length, t[-4:]), (5, len(t) - 8, b"M1CB"))
        off = struct.unpack("<H", t[2:4])[0]
        self.assertEqual(t[off:off + 10], b"Layer Test")
        self.assertEqual(t[off + 10:off + 12], bytes([1, 2]))          # layer, two timbres
        self.assertEqual(struct.unpack("<H", t[0:2])[0], 2 * count)     # the first record follows the offsets

    def test_off_timbre_left_out_and_name_clipped(self):
        r = m1_combi.pack_record({"name": "A very long name", "type": 4, "timbres": [timbre(1), timbre(2, on=False)]})
        self.assertEqual(r[:10], b"A very lon")
        self.assertEqual((r[10], r[11], len(r)), (4, 1, 12 + 10))

    def test_inserted_before_the_effect_table(self):
        bank = b"FM1B" + bytes(100) + fx_tail()
        t = m1_combi.build_table(COMBIS)
        out = m1_combi.add_table(bank, t)
        self.assertEqual(out[-len(fx_tail()):], fx_tail())              # the effect table is still last, untouched
        self.assertEqual(out[len(bank) - len(fx_tail()):][:len(t)], t)
        with self.assertRaises(SystemExit):
            m1_combi.add_table(out, t)

    def test_appended_without_effect_table(self):
        bank = b"FM1B" + bytes(100)
        out = m1_combi.add_table(bank, m1_combi.build_table(COMBIS))
        self.assertEqual(out[-4:], b"M1CB")


class Firmware(unittest.TestCase):
    def test_router_against_stub_engine(self):
        bank = m1_combi.add_table(b"FM1B" + bytes(200) + fx_tail(), m1_combi.build_table(COMBIS))
        with tempfile.TemporaryDirectory() as d:
            binf, exe = os.path.join(d, "bank.bin"), os.path.join(d, "combi_test")
            with open(binf, "wb") as fh:
                fh.write(bank)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", os.path.join(ROOT, "engine"),
                            os.path.join(ROOT, "fm1base", "m1_combi.c"), os.path.join(HERE, "combi_test.c"), "-o", exe], check=True)
            r = subprocess.run([exe, binf], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("all ok", r.stdout)


if __name__ == "__main__":
    unittest.main()
