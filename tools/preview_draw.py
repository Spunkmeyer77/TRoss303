#!/usr/bin/env python3
"""Panel preview, part 2: draws the recorded operations with PIL and overlays the live.* controls
from the .amxd (menus and number boxes are visible, the rest are invisible touch areas).

    python3 tools/preview_draw.py ops.json "device/TRoss 303.amxd" out.png
"""
import json
import math
import sys

from PIL import Image, ImageDraw, ImageFont

S = 3
W, H = 760, 169
ops = json.load(open(sys.argv[1]))
dev, out = sys.argv[2], sys.argv[3]
img = Image.new('RGBA', (W * S, H * S), (60, 60, 60, 255))

BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
REG = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'


def font(sz, b):
    return ImageFont.truetype(BOLD if b else REG, max(1, int(sz * S * 0.9)))


def c255(c):
    return tuple(int(max(0, min(1, v)) * 255) for v in c[:3]) + (int(max(0, min(1, c[3])) * 255),)


def lerp_stops(stops, t):
    t = max(0.0, min(1.0, t))
    for k in range(1, len(stops)):
        if t <= stops[k][0]:
            a, b = stops[k - 1], stops[k]
            u = (t - a[0]) / max(1e-9, b[0] - a[0])
            return [a[1][i] + (b[1][i] - a[1][i]) * u for i in range(4)]
    return stops[-1][1]


def mask_for(shape):
    m = Image.new('L', img.size, 0)
    d = ImageDraw.Draw(m)
    t = shape['t']
    if t == 'rect':
        d.rectangle([shape['x'] * S, shape['y'] * S, (shape['x'] + shape['w']) * S, (shape['y'] + shape['h']) * S], fill=255)
    elif t == 'rrect':
        d.rounded_rectangle([shape['x'] * S, shape['y'] * S, (shape['x'] + shape['w']) * S, (shape['y'] + shape['h']) * S],
                            radius=max(0, shape['r'] * S), fill=255)
    elif t == 'ell':
        d.ellipse([shape['x'] * S, shape['y'] * S, (shape['x'] + shape['w']) * S, (shape['y'] + shape['h']) * S], fill=255)
    elif t == 'poly':
        d.polygon([(x * S, y * S) for x, y in shape['pts']], fill=255)
    return m


def bbox(shape):
    if shape['t'] == 'poly':
        xs = [p[0] for p in shape['pts']]
        ys = [p[1] for p in shape['pts']]
        return min(xs), min(ys), max(xs), max(ys)
    return shape['x'], shape['y'], shape['x'] + shape['w'], shape['y'] + shape['h']


def paint_source(src, shape):
    if 'col' in src:
        return Image.new('RGBA', img.size, c255(src['col']))
    g = src['grad']
    x0, y0, x1, y1 = [int(v * S) for v in bbox(shape)]
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    px = layer.load()
    for yy in range(max(0, y0), min(img.size[1], y1 + 1)):
        for xx in range(max(0, x0), min(img.size[0], x1 + 1)):
            X, Y = xx / S, yy / S
            if g['kind'] == 'lin':
                dx, dy = g['x1'] - g['x0'], g['y1'] - g['y0']
                t = ((X - g['x0']) * dx + (Y - g['y0']) * dy) / max(1e-9, dx * dx + dy * dy)
            else:
                t = (math.hypot(X - g['x0'], Y - g['y0']) - g['r0']) / max(1e-9, g['r1'] - g['r0'])
            px[xx, yy] = c255(lerp_stops(g['stops'], t))
    return layer


for op in ops:
    if op['op'] == 'fill':
        for sh in op['shapes']:
            m = mask_for(sh)
            src = paint_source(op['src'], sh)
            a = src.split()[3]
            from PIL import ImageChops
            src.putalpha(ImageChops.multiply(a, m))
            img.alpha_composite(src)
    elif op['op'] == 'stroke':
        col = c255(op['src'].get('col', [0, 0, 0, 1]))
        layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        w = max(1, int(op['lw'] * S))
        for sh in op['shapes']:
            if sh['t'] == 'poly':
                pts = [(x * S, y * S) for x, y in sh['pts']]
                if sh.get('closed'):
                    pts.append(pts[0])
                if len(pts) > 1:
                    d.line(pts, fill=col, width=w)
            elif sh['t'] == 'ell':
                d.ellipse([sh['x'] * S, sh['y'] * S, (sh['x'] + sh['w']) * S, (sh['y'] + sh['h']) * S], outline=col, width=w)
        img.alpha_composite(layer)
    else:
        layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        f = font(op['size'], op['bold'])
        d.text((op['x'] * S, op['y'] * S), op['s'], font=f, fill=c255(op['src'].get('col', [0, 0, 0, 1])), anchor='ls')
        img.alpha_composite(layer)

# Visible live controls (menus and number boxes): draw their text like Live would.
raw = open(dev, 'rb').read()
j = json.loads(raw[32:].rstrip(b'\x00\n').decode())
d = ImageDraw.Draw(img)
for b in j['patcher']['boxes']:
    b = b['box']
    if not b['maxclass'].startswith('live.') or 'presentation_rect' not in b:
        continue
    x, y, w, h = b['presentation_rect']
    v = b['saved_attribute_attributes']['valueof']
    tc = b.get('textcolor')
    if b['maxclass'] in ('live.menu', 'live.numbox') and tc and tc[3] > 0:
        init = (v.get('parameter_initial') or [0])[0]
        label = v['parameter_enum'][int(init)] if v.get('parameter_enum') else (
            f"{init:g} Hz" if v.get('parameter_unitstyle') == 3 else f"{int(init):+d}" if v.get('parameter_unitstyle') == 7 else str(init))
        d.text(((x + w / 2) * S, (y + h / 2) * S), label.upper(), font=font(7, True), fill=tuple(int(c * 255) for c in tc[:3]),
               anchor='mm')
    if '--boxes' in sys.argv:
        d.rectangle([x * S, y * S, (x + w) * S, (y + h) * S], outline=(0, 160, 255, 200), width=2)

img.convert('RGB').save(out)
