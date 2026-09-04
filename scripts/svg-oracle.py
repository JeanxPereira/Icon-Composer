#!/usr/bin/env python3
"""An INDEPENDENT rasteriser for one SVG, and a differential against ours.

WHY THIS EXISTS
---------------
On 2026-09-04 a rendered icon showed two thin spikes where the antenna stem
meets the helmet, and reading the path data could not settle whether the art is
like that or our reader is wrong. Comparing anchor points is not enough: a
defect can live in the subdivision, between the anchors, where both readings
agree on every point they list.

So this is a second implementation, written from the SVG specification rather
than from `Source/CoreSVG`, and the two are compared pixel by pixel. Two
implementations agreeing is evidence; one alone is only itself.

WHAT IT DELIBERATELY DOES DIFFERENTLY
-------------------------------------
The coverage here is SUPERSAMPLED -- an NxN grid of point-in-polygon tests per
pixel -- while ours is the analytic area the target's shader computes. They are
not supposed to agree to the bit, and a diff of a few hundredths along every
edge is the expected shape of the disagreement. What this can prove is
STRUCTURAL: a region filled in one and empty in the other, which is what a
mis-parsed curve or a wrong winding looks like.

It reads only the subset the corpus uses: `path` with `M m L l H h V v C c Z z`,
`transform` of `translate`/`scale`/`matrix`, and `fill-rule` from the attribute
or from a `.class` in a `<style>` block. Anything else it REFUSES by name --
guessing would make the oracle agree with us for the wrong reason.

IT DOES NOT DRAW STROKES, and it refuses a stroked path rather than filling it
and reporting the missing stroke as OUR defect. That is not hypothetical: the
first run over `apollo_ring_large.svg` reported 389 differing pixels, and every
one of them was the 1.75-unit stroke that our renderer draws correctly and this
file cannot. An oracle that manufactures a disagreement out of its own blind
spot is worse than no oracle.

    python scripts/svg-oracle.py <file.svg> --size 512 [--against ours.png]
    python scripts/svg-oracle.py <file.svg> --size 512 --out oracle.png
"""
import argparse
import io
import math
import re
import struct
import sys
import zlib

NUM = re.compile(r'[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?')
CMD = re.compile(r'[MmLlHhVvCcSsQqTtAaZz]')


# ---------------------------------------------------------------- transforms
def parse_transform(text):
    """The 2x3 matrix a `transform` attribute denotes, applied left to right."""
    m = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    for name, args in re.findall(r'(\w+)\s*\(([^)]*)\)', text or ''):
        v = [float(x) for x in NUM.findall(args)]
        if name == 'translate':
            t = (1.0, 0.0, 0.0, 1.0, v[0], v[1] if len(v) > 1 else 0.0)
        elif name == 'scale':
            sx = v[0]
            sy = v[1] if len(v) > 1 else sx
            t = (sx, 0.0, 0.0, sy, 0.0, 0.0)
        elif name == 'matrix':
            t = tuple(v[:6])
        else:
            raise SystemExit('REFUSED: transform `%s` is not read by this oracle' % name)
        # m := m * t
        a, b, c, d, e, f = m
        A, B, C, D, E, F = t
        m = (a * A + c * B, b * A + d * B,
             a * C + c * D, b * C + d * D,
             a * E + c * F + e, b * E + d * F + f)
    return m


def apply(m, x, y):
    a, b, c, d, e, f = m
    return (a * x + c * y + e, b * x + d * y + f)


# --------------------------------------------------------------------- paths
def flatten(d, subdivisions):
    """The path as a list of closed polygons, in the path's own coordinates."""
    i = 0
    cur = (0.0, 0.0)
    start = (0.0, 0.0)
    cmd = None
    polys = []
    poly = []

    def close():
        nonlocal poly
        if len(poly) > 2:
            polys.append(poly)
        poly = []

    while i < len(d):
        mm = CMD.match(d, i)
        if mm:
            cmd = mm.group()
            i = mm.end()
            continue
        if not NUM.match(d, i):
            i += 1
            continue
        if cmd is None:
            raise SystemExit('REFUSED: numbers before any command')

        def take(n):
            nonlocal i
            out = []
            while len(out) < n:
                while i < len(d) and d[i] in ' ,\t\r\n':
                    i += 1
                g = NUM.match(d, i)
                if not g:
                    raise SystemExit('REFUSED: malformed path data')
                out.append(float(g.group()))
                i = g.end()
            return out

        rel = cmd.islower()
        c = cmd.upper()
        if c in 'ASQT':
            raise SystemExit('REFUSED: command `%s` is not read by this oracle' % c)
        if c == 'M':
            x, y = take(2)
            close()
            cur = (cur[0] + x, cur[1] + y) if rel else (x, y)
            start = cur
            poly = [cur]
            cmd = 'l' if rel else 'L'
        elif c == 'L':
            x, y = take(2)
            cur = (cur[0] + x, cur[1] + y) if rel else (x, y)
            poly.append(cur)
        elif c == 'H':
            x, = take(1)
            cur = (cur[0] + x, cur[1]) if rel else (x, cur[1])
            poly.append(cur)
        elif c == 'V':
            y, = take(1)
            cur = (cur[0], cur[1] + y) if rel else (cur[0], y)
            poly.append(cur)
        elif c == 'C':
            v = take(6)
            if rel:
                p1 = (cur[0] + v[0], cur[1] + v[1])
                p2 = (cur[0] + v[2], cur[1] + v[3])
                p3 = (cur[0] + v[4], cur[1] + v[5])
            else:
                p1, p2, p3 = (v[0], v[1]), (v[2], v[3]), (v[4], v[5])
            p0 = cur
            for k in range(1, subdivisions + 1):
                t = k / subdivisions
                u = 1.0 - t
                poly.append((u * u * u * p0[0] + 3 * u * u * t * p1[0] +
                             3 * u * t * t * p2[0] + t * t * t * p3[0],
                             u * u * u * p0[1] + 3 * u * u * t * p1[1] +
                             3 * u * t * t * p2[1] + t * t * t * p3[1]))
            cur = p3
        elif c == 'Z':
            close()
            cur = start
            poly = [cur]
    close()
    return polys


# ------------------------------------------------------------------ the scan
def coverage(polys, w, h, evenodd, samples):
    """Alpha per pixel, by an NxN point-in-polygon grid inside each pixel."""
    edges = []
    for poly in polys:
        n = len(poly)
        for k in range(n):
            x0, y0 = poly[k]
            x1, y1 = poly[(k + 1) % n]
            if y0 != y1:
                edges.append((x0, y0, x1, y1))
    if not edges:
        return bytearray(w * h)

    ymin = max(0, int(math.floor(min(min(e[1], e[3]) for e in edges))))
    ymax = min(h - 1, int(math.ceil(max(max(e[1], e[3]) for e in edges))))
    out = bytearray(w * h)
    inv = 1.0 / samples

    for py in range(ymin, ymax + 1):
        acc = [0] * w
        for sy in range(samples):
            yy = py + (sy + 0.5) * inv
            xs = []
            for (x0, y0, x1, y1) in edges:
                if (y0 <= yy) == (y1 <= yy):
                    continue
                t = (yy - y0) / (y1 - y0)
                xs.append((x0 + t * (x1 - x0), 1 if y1 > y0 else -1))
            if not xs:
                continue
            xs.sort()
            # The spans this scanline is inside, by the chosen rule.
            spans = []
            wind = 0
            for idx in range(len(xs) - 1):
                wind += xs[idx][1]
                inside = (wind % 2 != 0) if evenodd else (wind != 0)
                if inside:
                    spans.append((xs[idx][0], xs[idx + 1][0]))
            if not spans:
                continue
            for px in range(w):
                base = px
                hits = 0
                for sx in range(samples):
                    xx = base + (sx + 0.5) * inv
                    for (a, b) in spans:
                        if a <= xx < b:
                            hits += 1
                            break
                acc[px] += hits
        total = samples * samples
        row = py * w
        for px in range(w):
            if acc[px]:
                out[row + px] = min(255, int(round(255.0 * acc[px] / total)))
    return out


# -------------------------------------------------------------------- PNG io
def read_png_alpha(p):
    d = io.open(p, 'rb').read()
    i, w, h, bd, idat = 8, 0, 0, 8, b''
    while i < len(d):
        ln = struct.unpack('>I', d[i:i + 4])[0]
        typ = d[i + 4:i + 8]
        if typ == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', d[i + 8:i + 18])
        elif typ == b'IDAT':
            idat += d[i + 8:i + 8 + ln]
        i += 12 + ln
    raw = zlib.decompress(idat)
    bpp = 8 if bd == 16 else 4
    stride = w * bpp
    rows = []
    prev = bytearray(stride)
    o = 0
    for _ in range(h):
        f = raw[o]; o += 1
        line = bytearray(raw[o:o + stride]); o += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(line)
        prev = line
    step = 2 if bd == 16 else 1
    alpha = bytearray(w * h)
    for y in range(h):
        r = rows[y]
        for x in range(w):
            alpha[y * w + x] = r[x * bpp + 3 * step]
    return w, h, alpha


def write_gray_png(p, w, h, a):
    raw = b''.join(b'\x00' + bytes(a[y * w:(y + 1) * w]) for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
    io.open(p, 'wb').write(b'\x89PNG\r\n\x1a\n'
                          + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
                          + chunk(b'IDAT', zlib.compress(raw, 6))
                          + chunk(b'IEND', b''))


# ---------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('svg')
    ap.add_argument('--size', type=int, default=512)
    ap.add_argument('--subdivisions', type=int, default=16)
    ap.add_argument('--samples', type=int, default=4)
    ap.add_argument('--against')
    ap.add_argument('--out')
    ap.add_argument('--diff-out')
    args = ap.parse_args()

    text = io.open(args.svg, encoding='utf-8').read()
    vb = re.search(r'viewBox\s*=\s*"([^"]*)"', text)
    if not vb:
        raise SystemExit('REFUSED: no viewBox')
    bx, by, bw, bh = [float(x) for x in NUM.findall(vb.group(1))][:4]

    # `fill-rule` from a `<style>` class, which is how CorelDRAW writes it.
    rules = {}
    strokes = set()
    for cls, body in re.findall(r'\.([A-Za-z0-9_-]+)\s*\{([^}]*)\}', text):
        r = re.search(r'fill-rule\s*:\s*([a-z]+)', body)
        if r:
            rules[cls] = r.group(1)
        st = re.search(r'stroke\s*:\s*([^;}\s]+)', body)
        if st and st.group(1) != 'none':
            strokes.add(cls)
    doc_rule = re.search(r'style\s*=\s*"[^"]*fill-rule\s*:\s*([a-z]+)', text)

    # Every `<path>` with the transform of every `<g>` that encloses it. The
    # corpus never nests groups more than one deep, and a deeper one is REFUSED
    # rather than flattened wrongly.
    if len(re.findall(r'<g\b', text)) > 1:
        raise SystemExit('REFUSED: more than one <g>; this oracle does not nest')
    g = re.search(r'<g\b[^>]*>', text)
    gm = parse_transform(re.search(r'transform\s*=\s*"([^"]*)"', g.group()).group(1)
                         if g and 'transform' in g.group() else '')

    s = min(args.size / (bw if bw > 0 else 1.0), args.size / (bh if bh > 0 else 1.0))
    e = (args.size - s * bw) * 0.5 - s * bx
    f = (args.size - s * bh) * 0.5 - s * by

    total = bytearray(args.size * args.size)
    count = 0
    for tag in re.findall(r'<path\b[^>]*>', text):
        dm = re.search(r'\sd\s*=\s*"([^"]*)"', tag)
        if not dm:
            continue
        # A STROKED path is refused, not filled. See the header.
        cls = re.search(r'class\s*=\s*"([^"]*)"', tag)
        names = cls.group(1).split() if cls else []
        stroked = any(n in strokes for n in names)
        sa = re.search(r'stroke\s*=\s*"([^"]*)"', tag)
        if sa and sa.group(1).strip() not in ('none', ''):
            stroked = True
        if stroked:
            raise SystemExit('REFUSED: this path is STROKED, and this oracle only fills.'
                             ' The difference would be our stroke, not a defect.')

        rule = None
        cm = re.search(r'class\s*=\s*"([^"]*)"', tag)
        if cm and cm.group(1).strip() in rules:
            rule = rules[cm.group(1).strip()]
        fr = re.search(r'fill-rule\s*=\s*"([a-z]+)"', tag)
        if fr:
            rule = fr.group(1)
        if rule is None and doc_rule:
            rule = doc_rule.group(1)
        evenodd = (rule == 'evenodd')

        polys = flatten(dm.group(1), args.subdivisions)
        placed = []
        for poly in polys:
            pp = []
            for (x, y) in poly:
                gx, gy = apply(gm, x, y)
                pp.append((s * gx + e, s * gy + f))
            placed.append(pp)
        a = coverage(placed, args.size, args.size, evenodd, args.samples)
        for k in range(len(total)):
            if a[k] > total[k]:
                total[k] = a[k]
        count += 1

    print('%d path(s), fill-rule %s' % (count, 'evenodd' if evenodd else 'nonzero'))
    if args.out:
        write_gray_png(args.out, args.size, args.size, total)
        print('oracle:', args.out)

    if args.against:
        w, h, ours = read_png_alpha(args.against)
        if (w, h) != (args.size, args.size):
            raise SystemExit('sizes differ: ours %dx%d, oracle %d' % (w, h, args.size))
        diff = bytearray(w * h)
        big = 0
        worst = 0
        worst_at = (0, 0)
        for k in range(w * h):
            d = abs(int(ours[k]) - int(total[k]))
            diff[k] = min(255, d)
            if d > worst:
                worst, worst_at = d, (k % w, k // w)
            if d > 128:
                big += 1
        print('DIFERENCIAL contra %s' % args.against)
        print('  pixels com diferenca > 0.5 de alpha: %d de %d (%.4f%%)'
              % (big, w * h, 100.0 * big / (w * h)))
        print('  maior diferenca: %.3f em (%d, %d)' % (worst / 255.0, *worst_at))
        if args.diff_out:
            write_gray_png(args.diff_out, w, h, diff)
            print('  mapa da diferenca:', args.diff_out)
        return 1 if big else 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
