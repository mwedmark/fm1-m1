#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read the A00 character patterns (codes 0x20..0x7F) out of Table 4 of Hitachi's HD44780U datasheet (page 17, vector dots) and
print them as the C table in fm1base/m1_lcd.h (m1_font_a00). Needs pdftoppm (poppler) and numpy + Pillow.

  python -I tools/hd44780_a00.py HD44780.pdf [--json out.json]

The table's geometry (column boundaries in the page at 150 dpi, a dot pitch of 25 px at 600 dpi) is that of the Hitachi PDF with 60 pages
(sparkfun / adafruit copies); each band of sixteen rows is found from the dots themselves."""
import argparse, json, os, subprocess, sys, tempfile
from collections import deque

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None
S = 4                                               # 600 dpi / 150 dpi
COLX = [262, 310, 361, 412, 462, 512, 562, 612, 663]   # the table's column edges at 150 dpi (upper nibbles 0 .. 7)
PAGE = 17


def components(a):
    h, w = a.shape
    seen = np.zeros_like(a)
    for y in range(h):
        for x in range(w):
            if a[y, x] and not seen[y, x]:
                q, n, sy, sx = deque([(y, x)]), 0, 0, 0
                seen[y, x] = 1
                while q:
                    cy, cx = q.popleft()
                    n, sy, sx = n + 1, sy + cy, sx + cx
                    for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                        if 0 <= ny < h and 0 <= nx < w and a[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = 1
                            q.append((ny, nx))
                yield n, sy / n, sx / n


def read_table(pdf):
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(['pdftoppm', '-r', '600', '-f', str(PAGE), '-l', str(PAGE), '-gray', '-png', pdf, os.path.join(d, 'p')], check=True)
        name = [f for f in os.listdir(d) if f.endswith('.png')][0]
        im = np.array(Image.open(os.path.join(d, name)).convert('L')) < 128
    y0, y1 = 300 * S, int((308 + 16 * 63.95 + 10) * S)
    dots = []
    for c in range(2, 8):                          # upper nibbles 2..7 = codes 0x20..0x7F
        for n, y, x in components(im[y0:y1, COLX[c] * S:COLX[c + 1] * S]):
            if 300 < n < 600:                      # a dot (the cell borders are far larger)
                dots.append((y + y0, c, x))
    ys = sorted({round(y) for y, _, _ in dots})
    bands = [[ys[0]]]
    for y in ys[1:]:
        (bands[-1].append if y - bands[-1][-1] <= 60 else lambda v: bands.append([v]))(y)
    if len(bands) != 16:
        sys.exit('expected 16 rows of characters, found %d: is this the 60-page HD44780U datasheet?' % len(bands))
    glyphs = {code: [0] * 8 for code in range(0x20, 0x80)}
    for y, c, x in dots:
        r = next(i for i, b in enumerate(bands) if b[0] - 12 <= y <= b[-1] + 12)
        row, col = round((y - bands[r][0]) / 25), round((x - 48) / 25)
        if not (0 <= row < 8 and 0 <= col < 5):
            sys.exit('a dot off the 5 x 8 grid at code 0x%02X' % (c * 16 + r))
        glyphs[c * 16 + r][row] |= 0x10 >> col
    return glyphs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('pdf')
    ap.add_argument('--json')
    a = ap.parse_args()
    g = read_table(a.pdf)
    for code in range(0x20, 0x80):
        print('    {%s},  /* %02X */' % (', '.join('0x%02X' % v for v in g[code]), code))
    if a.json:
        json.dump(g, open(a.json, 'w'))


if __name__ == '__main__':
    main()
