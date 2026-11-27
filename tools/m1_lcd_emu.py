#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the M1's OS EPROMs in an x86 emulator (Unicorn) and read out what it writes to the LCD.

The M1's CPU is an NEC V50 (a V30 core): 8086 code plus the V20 bit instructions (0F 10..1F: TEST1/CLR1/SET1/NOT1), which
Unicorn lacks and this script emulates. Only enough of the hardware is faked to get the OS to its play page: I/O reads return 0
(LCD not busy, no key, no sound chip busy) and the two interrupts the OS waits on (tick, vector 10; sound timer, vector 8) are
injected between chunks of instructions. See docs/M1_LCD.md for what has been measured. Nothing from Korg is in this file: the
ROM is your own dump (IC23 high, IC32 low, 64 KB each) and the output stays in roms/ (ignored by git).

  python -I tools/m1_lcd_emu.py IC23.bin IC32.bin [--instr 300000000] [--json out.json]

Needs: pip install unicorn
"""
import argparse, ctypes, json, sys

try:
    from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_PROT_ALL, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INSN_INVALID)
    from unicorn.x86_const import *
except ImportError:
    sys.exit("needs the unicorn package: pip install unicorn")

DS = 0xF75C                      # the OS's data segment: 0xF75C0 + offset wraps at 1 MB onto the low RAM (A20 wrap)
LCD_DATA, LCD_CTL = 0xA06, 0xA08  # HD44780-compatible: control bit 0 = RS, bit 1 = R/W, bit 2 = E
TICK_ISR, SND_ISR = 0x11E8D, 0x11F40   # image offsets of the two interrupt handlers
BATTERY_CHECK = 0x1EF6                 # image offset of the start-up compare of the battery reading (RAM DS:C4E1, ok = 0x81..0xCD)


def phys(seg, off):
    return (seg * 16 + off) & 0xFFFFF


class Lcd:
    """40 x 2 characters. The OS writes its 80-character buffer in one burst and relies on the module to continue from column 40
    of row 0 into row 1 (it sets the address only after a skipped character), so the model is one linear 80-character memory."""
    def __init__(self):
        self.dd = [0x20] * 80
        self.cg = [0] * 64
        self.pos = 0
        self.cgram = False

    def command(self, v):
        if v == 1:
            self.dd = [0x20] * 80
            self.pos, self.cgram = 0, False
        elif v & 0x80:
            a = v & 0x7F
            self.pos = a if a < 0x28 else a - 0x40 + 40 if 0x40 <= a < 0x68 else 0
            self.cgram = False
        elif v & 0x40:
            self.pos, self.cgram = v & 0x3F, True

    def data(self, v):
        if self.cgram:
            self.cg[self.pos & 63] = v & 0x1F
            self.pos = (self.pos + 1) & 63
        else:
            self.dd[self.pos % 80] = v
            self.pos = (self.pos + 1) % 80

    def rows(self):
        return [bytes(self.dd[:40]), bytes(self.dd[40:])]

    def glyphs(self):
        return [self.cg[i * 8:i * 8 + 8] for i in range(8)]


class Machine:
    R16 = [UC_X86_REG_AX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_BX, UC_X86_REG_SP, UC_X86_REG_BP, UC_X86_REG_SI, UC_X86_REG_DI]
    R8 = [UC_X86_REG_AL, UC_X86_REG_CL, UC_X86_REG_DL, UC_X86_REG_BL, UC_X86_REG_AH, UC_X86_REG_CH, UC_X86_REG_DH, UC_X86_REG_BH]
    SEG = [UC_X86_REG_ES, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS]

    def __init__(self, ic23, ic32):
        img = bytearray(2 * len(ic23))
        img[0::2], img[1::2] = ic32, ic23          # the low chip is at even addresses
        self.image = bytes(img)
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.low = (ctypes.c_char * 0x10000)()      # 64 KB of RAM seen twice: at 0 and at 1 MB, because the OS's stack wraps
        mu.mem_map_ptr(0, 0x10000, UC_PROT_ALL, ctypes.addressof(self.low))
        mu.mem_map_ptr(0x100000, 0x10000, UC_PROT_ALL, ctypes.addressof(self.low))
        mu.mem_map(0x10000, 0xF0000)
        mu.mem_write(0x100000 - len(img), self.image)
        mu.reg_write(UC_X86_REG_CS, 0xF000)
        mu.reg_write(UC_X86_REG_IP, 0xFFF0)
        self.lcd, self.ports, self.v20_ops, self.ticks = Lcd(), {}, 0, 0
        mu.hook_add(UC_HOOK_INSN, lambda uc, port, size, ud: 0, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        mu.hook_add(UC_HOOK_INSN_INVALID, self._v20)

    def _out(self, uc, port, size, val, ud):
        self.ports[port] = val
        if port == LCD_CTL and (val & 6) == 4:      # E high, write: latch what is on the data port
            (self.lcd.data if val & 1 else self.lcd.command)(self.ports.get(LCD_DATA, 0))

    def _v20(self, uc, ud):
        """NEC V20/V30 bit instructions 0F 10..1F with a ModRM byte (and an 8-bit immediate for 18..1F)."""
        cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
        b = bytes(uc.mem_read(phys(cs, ip), 8))
        i, seg = 0, None
        while b[i] in (0x26, 0x2E, 0x36, 0x3E):
            seg = {0x26: 0, 0x2E: 1, 0x36: 2, 0x3E: 3}[b[i]]
            i += 1
        if b[i] != 0x0F or not 0x10 <= b[i + 1] <= 0x1F:
            return False
        op, m = b[i + 1], b[i + 2]
        i += 3
        mod, rm = m >> 6, m & 7
        sg = ea = None
        if mod != 3:
            if mod == 0 and rm == 6:
                ea, dseg = int.from_bytes(b[i:i + 2], 'little'), 3
                i += 2
            else:
                names = ['BX+SI', 'BX+DI', 'BP+SI', 'BP+DI', 'SI', 'DI', 'BP', 'BX'][rm]
                dseg = 2 if 'BP' in names else 3
                regs = {'BX': UC_X86_REG_BX, 'SI': UC_X86_REG_SI, 'DI': UC_X86_REG_DI, 'BP': UC_X86_REG_BP}
                ea = sum(uc.reg_read(regs[t]) for t in names.split('+'))
                if mod == 1:
                    ea += b[i] - 256 if b[i] > 127 else b[i]
                    i += 1
                elif mod == 2:
                    ea += int.from_bytes(b[i:i + 2], 'little')
                    i += 2
            ea &= 0xFFFF
            sg = uc.reg_read(self.SEG[seg if seg is not None else dseg])
        wide = op & 1
        bits = 16 if wide else 8
        if op < 0x18:
            bit = uc.reg_read(UC_X86_REG_CL) & (bits - 1)
        else:
            bit = b[i] & (bits - 1)
            i += 1
        regs = self.R16 if wide else self.R8
        val = uc.reg_read(regs[rm]) if mod == 3 else int.from_bytes(uc.mem_read(phys(sg, ea), bits // 8), 'little')
        kind = ((op & 0xF) if op < 0x18 else (op & 0xF) - 8) >> 1      # 0 TEST1, 1 CLR1, 2 SET1, 3 NOT1
        old = val >> bit & 1
        if kind == 0:
            fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~0x841       # CF and OF cleared, ZF = the bit was clear
            uc.reg_write(UC_X86_REG_EFLAGS, fl | (0 if old else 0x40))
        else:
            val = val & ~(1 << bit) if kind == 1 else val | 1 << bit if kind == 2 else val ^ 1 << bit
            if mod == 3:
                uc.reg_write(regs[rm], val)
            else:
                uc.mem_write(phys(sg, ea), (val & (1 << bits) - 1).to_bytes(bits // 8, 'little'))
        uc.reg_write(UC_X86_REG_IP, (ip + i) & 0xFFFF)
        self.v20_ops += 1
        return True

    def battery_ok(self):
        """The OS reads the battery from hardware that is not emulated and would show `Battery Low` on the play page: give it a good reading."""
        def fix(uc, addr, size, ud):
            uc.mem_write(phys(DS, 0xC4E1), b'\xA0')
        a = 0x100000 - len(self.image) + BATTERY_CHECK
        self.mu.hook_add(UC_HOOK_CODE, fix, None, a, a)

    def vector_for(self, image_off):
        for v in range(256):
            w = bytes(self.mu.mem_read(v * 4, 4))
            if phys(int.from_bytes(w[2:], 'little'), int.from_bytes(w[:2], 'little')) == 0x100000 - len(self.image) + image_off:
                return v
        return None

    def interrupt(self, vec):
        mu = self.mu
        cs, ip, fl = (mu.reg_read(r) for r in (UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_EFLAGS))
        ss, sp = mu.reg_read(UC_X86_REG_SS), mu.reg_read(UC_X86_REG_SP)
        for v in (fl & 0xFFFF, cs, ip):
            sp = (sp - 2) & 0xFFFF
            mu.mem_write(phys(ss, sp), v.to_bytes(2, 'little'))
        mu.reg_write(UC_X86_REG_SP, sp)
        w = bytes(mu.mem_read(vec * 4, 4))
        mu.reg_write(UC_X86_REG_EFLAGS, fl & ~0x300)
        mu.reg_write(UC_X86_REG_CS, int.from_bytes(w[2:], 'little'))
        mu.reg_write(UC_X86_REG_IP, int.from_bytes(w[:2], 'little'))

    def run(self, instructions, chunk=20000):
        mu, done, first, vt = self.mu, 0, True, None
        while done < instructions:
            start = 0xFFFF0 if first else phys(mu.reg_read(UC_X86_REG_CS), mu.reg_read(UC_X86_REG_IP))
            first = False
            mu.emu_start(start, 0xFFFFFFFF, count=chunk)
            done += chunk
            if vt is None:
                vt = self.vector_for(TICK_ISR)
            if vt is not None and mu.reg_read(UC_X86_REG_EFLAGS) & 0x200:
                self.interrupt(vt)
                self.ticks += 1


def show(b):
    return ''.join(chr(c) if 32 <= c < 127 else '\\x%02x' % c for c in b)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('ic23')
    ap.add_argument('ic32')
    ap.add_argument('--instr', type=int, default=300_000_000, help='instructions to run (the play page needs ~300 M)')
    ap.add_argument('--battery-low', action='store_true', help='leave the battery reading at 0: the play page shows Battery Low')
    ap.add_argument('--json', help='write the rows and the custom characters here')
    a = ap.parse_args()
    ic23, ic32 = open(a.ic23, 'rb').read(), open(a.ic32, 'rb').read()
    if len(ic23) != len(ic32) or len(ic23) != 65536:
        sys.exit('IC23 and IC32 must both be 65536 bytes')
    m = Machine(ic23, ic32)
    if not a.battery_low:
        m.battery_ok()
    try:
        m.run(a.instr)
    except UcError as e:
        print('emulation stopped:', e, file=sys.stderr)
    print('ticks %d, V20 bit instructions %d' % (m.ticks, m.v20_ops))
    for r in m.lcd.rows():
        print('|' + show(r) + '|')
    for i, g in enumerate(m.lcd.glyphs()):
        print('char %d: %s' % (i, ' '.join('%02x' % x for x in g)))
    if a.json:
        json.dump({'rows': [list(r) for r in m.lcd.rows()], 'glyphs': [list(g) for g in m.lcd.glyphs()]}, open(a.json, 'w'))


if __name__ == '__main__':
    main()
