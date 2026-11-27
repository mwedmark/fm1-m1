#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read the M1's Global pages out of its OS (tools/m1_lcd_emu.py) into a table that the firmware (fm1base/m1_pages.h) draws and edits.

  python -I tools/m1_pages.py IC23.bin IC32.bin roms/gen/m1_pages.bin [--json roms/gen/m1_pages.json]
  python -I tools/m1_pages.py --append roms/gen/m1_pages.bin roms/out/m1.fm1b roms/out/m1_pg.fm1b

For every page the OS draws, the table keeps the 40 x 2 screen as it first appears and, for each parameter on the page (the OS's cursor stops):
the cursor position, the label it puts on row 0 while the parameter is selected, and the values the parameter can take, as the text the OS shows in
the parameter's slot. The values are found by pressing +1 / -1 until the OS clamps or wraps. Pages whose parameters depend on each other (the user scale, the drum kits)
or act on a disk or the sequencer (memory allocation, dump, card) are kept as a picture only (no parameters).

The text and the layout are Korg's, from your own dump: the table is built into the resource region of your own bank (--append), with the trailer
"M1PG" after the bank's other data, and never goes into git (roms/ is ignored).

Table, little endian:  [blob][u32 blob length]["M1PG"]
  blob   = u8 version (1), u8 pages, pages
  page   = u8 parameters, 80 bytes of screen, parameters
  param  = u8 cursor (0..79), u8 label start, u8 label width (0: none), label bytes, u8 slot start, u8 slot width, u8 values, u8 initial,
           u8 wraps, u8 id (the settings slot, 255: none), values * slot width bytes
"""
import argparse, importlib.util, json, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('m1_lcd_emu', os.path.join(HERE, 'm1_lcd_emu.py'))
emu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(emu)

FIELD_KEYS = [(2, 2), (2, 3), (2, 4), (2, 5), (6, 0), (6, 1), (6, 2), (6, 3)]   # the eight keys under the 5-column groups of the display
GLOBAL, PAGE_NEXT, INC, DEC = (1, 5), (6, 5), (2, 0), (2, 1)
PICTURE_ONLY = {5, 6, 7, 8, 9, 15, 16, 17, 18, 19}     # see the module's text
MAX_VALUES, MAX_IDS = 255, 32


def enumerate_values(m, state):
    """The screens of every value of the selected parameter, lowest first; returns (screens, index of the start, wraps)."""
    m.restore(state)
    first = m.screen()
    seen, down, prev, wraps = [first], [], first, False
    while len(down) < MAX_VALUES:
        m.tap(*DEC)
        s = m.screen()
        if s == prev:
            break
        if s in seen:
            wraps = True
            break
        down.append(s)
        seen.append(s)
        prev = s
    m.restore(state)
    up, prev = [], first
    while len(up) < MAX_VALUES - len(down):
        m.tap(*INC)
        s = m.screen()
        if s == prev:
            break
        if s in seen:
            wraps = True
            break
        up.append(s)
        seen.append(s)
        prev = s
    return down[::-1] + [first] + up, len(down), wraps


def read_page(m, base, index, next_id):
    m.restore(base)
    screen = m.screen()
    page = {'screen': screen, 'params': []}
    if index in PICTURE_ONLY:
        return page, next_id
    cursors = {}
    for key in FIELD_KEYS:                          # (the cursor the page starts with can be left over from the last page: only the keys' count)
        m.restore(base)
        m.tap(*key)
        cursors.setdefault(m.cursor_index(), key)
    selected = []
    for cursor, key in sorted(cursors.items()):
        m.restore(base)
        if key:
            m.tap(*key)
        selected.append((cursor, key, m.snapshot(), m.screen()))
    for cursor, key, state, sel_screen in selected:
        screens, init, wraps = enumerate_values(m, state)
        if len(screens) < 2:
            continue
        diff = [i for i in range(80) if any(s[i] != screens[init][i] for s in screens)]
        slot = (min(diff), max(diff) + 1)
        page['params'].append({'cursor': cursor, 'key': key, 'sel': sel_screen, 'slot': slot, 'init': init, 'wraps': wraps,
                               'values': [s[slot[0]:slot[1]] for s in screens]})
    # the label: what differs between the parameters' selected screens, outside the value slots
    if len(page['params']) > 1:
        diff = [i for i in range(80) if any(p['sel'][i] != page['params'][0]['sel'][i] for p in page['params'])
                and not any(p['slot'][0] <= i < p['slot'][1] for p in page['params'])]
        if diff:
            lab = (min(diff), max(diff) + 1)
            for p in page['params']:
                p['label'] = (lab[0], p['sel'][lab[0]:lab[1]])
    for p in page['params']:
        p.setdefault('label', (0, b''))
        p['id'] = next_id if next_id < MAX_IDS else 255
        next_id += 1
    return page, next_id


def read_global(ic23, ic32, log=print):
    m = emu.Machine(ic23, ic32)
    m.battery_ok()
    m.run(300_000_000)
    m.tap(*GLOBAL, settle=2_500_000)
    pages, first, last, next_id = [], None, None, 0
    while len(pages) < 40:
        s = m.screen()
        if first is None:
            first = s
        elif s == first or s == last:                 # round again, or the OS stops at its last page
            break
        last = s
        base = m.snapshot()
        page, next_id = read_page(m, base, len(pages), next_id)
        log('page %2d: %2d parameters  %r' % (len(pages), len(page['params']), page['screen'][:40].decode('latin1').strip()
                                              or page['screen'][40:].decode('latin1').strip()))
        pages.append(page)
        m.restore(base)
        m.tap(*PAGE_NEXT, settle=2_500_000)
    return pages


def encode(pages):
    b = bytearray([1, len(pages)])
    for pg in pages:
        b += bytes([len(pg['params'])]) + pg['screen']
        for p in pg['params']:
            lab_start, lab = p['label']
            w = p['slot'][1] - p['slot'][0]
            b += bytes([p['cursor'], lab_start, len(lab)]) + lab
            b += bytes([p['slot'][0], w, len(p['values']), p['init'], 1 if p['wraps'] else 0, p['id']])
            for v in p['values']:
                b += v
    return bytes(b)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('args', nargs='+')
    ap.add_argument('--append', action='store_true', help='BANK_IN BANK_OUT: add a table (made first) to a bank, after its other data')
    ap.add_argument('--json')
    a = ap.parse_args()
    if a.append:
        table, bank_in, bank_out = a.args
        bank, blob = open(bank_in, 'rb').read(), open(table, 'rb').read()
        if bank[-4:] == b'M1PG':
            sys.exit('the bank already has a page table')
        open(bank_out, 'wb').write(bank + blob)
        print('%s: %d B (bank %d + pages %d)' % (bank_out, len(bank) + len(blob), len(bank), len(blob)))
        return
    ic23, ic32, out = a.args
    pages = read_global(open(ic23, 'rb').read(), open(ic32, 'rb').read())
    blob = encode(pages)
    open(out, 'wb').write(blob + struct.pack('<I', len(blob)) + b'M1PG')
    print('%s: %d B, %d pages, %d parameters' % (out, len(blob) + 8, len(pages), sum(len(p['params']) for p in pages)))
    if a.json:
        dump = [{'screen': [p['screen'][:40].decode('latin1'), p['screen'][40:].decode('latin1')],
                 'params': [{'cursor': q['cursor'], 'id': q['id'], 'slot': q['slot'], 'init': q['init'], 'wraps': q['wraps'],
                             'label': q['label'][1].decode('latin1'), 'values': [v.decode('latin1') for v in q['values']]}
                            for q in p['params']]} for p in pages]
        json.dump(dump, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
