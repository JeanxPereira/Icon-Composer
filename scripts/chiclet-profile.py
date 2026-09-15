#!/usr/bin/env python3
"""Mede o CANTO de um chiclet num PNG e diz qual raio o explica.

Por que existe: um mapa de calor diz que os cantos erram, nao diz de quanto nem
por que. Este script troca o mapa por dois numeros por imagem:

  diag   onde o contorno cruza a diagonal do canto, em pixels a partir do
         vertice geometrico da caixa do corpo (e em fracao do lado N);
  r/N    o raio do canto CONTINUO transcrito de `add_rounded_rect`
         (`Source/RenderBox/ChicletShape.cpp`) que melhor reproduz o perfil
         medido, com o erro RMS do ajuste em pixels.

A medida e por LINHA, e isso a torna independente do filtro de antialiasing:
a soma da cobertura de uma linha dentro da caixa e o comprimento de dentro; pela
simetria esquerda/direita o recuo da borda naquela linha e (N - L) / 2. O modelo
e integrado do mesmo jeito (media sobre sub-linhas), entao os dois lados da
comparacao passam pela mesma media.

A cobertura sai do alfa. Numa saida com sombra externa o alfa de um pixel de
borda e `c + (1 - c) * s`; `--shadow` e o `s` (o default le o pixel logo fora da
borda na linha central) e `c = (a - s) / (1 - s)`.

O ajuste NAO escolhe numero para o renderizador. Ele diz qual forma a imagem tem;
qual forma o binario desenha e outra pergunta (`Docs/Laudos/2026-09-15-chiclet-geometria.md`).

Uso:
    python scripts/chiclet-profile.py <png>
    python scripts/chiclet-profile.py <png> --box 50 50 412 --shadow 0.0706
"""
from __future__ import annotations

import argparse
import bisect
import importlib.util
import math
import struct
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "png_diff", Path(__file__).with_name("png-diff.py"))
_png = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_png)

# `[BIN]` RenderBox, `0x15EBC0`/`0x15EC78` e `0x15EBD0`-`0x15EC00`.
EXT, CTL, SHO = 1.5286649465560913, 1.0884900093078613, 0.8684070110321045
NEAR, MID1, MID2, FAR = (0.074911400675773621, 0.16906000673770905,
                         0.37282401323318481, 0.63149398565292358)


def f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", v))[0]


def corner_params(edge: float, r: float) -> tuple[float, float, float]:
    """A folga por aresta, `0x7F664`-`0x7F690`, em float como o alvo."""
    s = f32(f32(r) + f32(r))
    t = f32((f32(edge) - s) / f32(s * f32(0.528664947)))
    if t >= 1.0:
        return EXT, CTL, SHO
    u = min(max(t, 0.0), 1.0)
    return 1 + 0.528664947 * u, 0.96 + 0.128490031 * u, 0.82 + 0.0484070182 * u


def corner_curve(r: float, edge: float, per_cubic: int = 400) -> tuple[list, list]:
    """Um canto como x(y): recuo horizontal da borda em funcao da distancia y ao
    vertice, crescente em y."""
    ext, ctl, sho = corner_params(edge, r)
    # (recuo x, profundidade y), com o vertice em (0, 0)
    cubics = [
        ((0, ext * r), (0, ctl * r), (0, sho * r), (NEAR * r, FAR * r)),
        ((NEAR * r, FAR * r), (MID1 * r, MID2 * r), (MID2 * r, MID1 * r), (FAR * r, NEAR * r)),
        ((FAR * r, NEAR * r), (sho * r, 0), (ctl * r, 0), (ext * r, 0)),
    ]
    pts = []
    for p0, p1, p2, p3 in cubics:
        for i in range(per_cubic + 1):
            t = i / per_cubic
            u = 1 - t
            a, b, c, d = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
            pts.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                        a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    pts.sort(key=lambda p: p[1])
    return [p[1] for p in pts], [p[0] for p in pts]


def model_rows(r: float, n: int, rows: int, sub: int = 64) -> list[float]:
    ys, xs = corner_curve(r, n)
    out = []
    for py in range(rows):
        acc = 0.0
        for k in range(sub):
            sy = py + (k + 0.5) / sub
            if sy >= ys[-1]:
                continue
            j = bisect.bisect_left(ys, sy)
            if j == 0:
                acc += xs[0]
                continue
            y0, y1 = ys[j - 1], ys[j]
            f = 0.0 if y1 == y0 else (sy - y0) / (y1 - y0)
            acc += xs[j - 1] + f * (xs[j] - xs[j - 1])
        out.append(acc / sub)
    return out


def measured_rows(alpha, w, x0, y0, n, s, rows):
    """Recuo por linha nos quatro lados: topo, base, esquerda, direita."""
    def a(x, y):
        return alpha[y * w + x] / 255.0

    def cov(v):
        return 0.0 if v < s * 1.5 else min(1.0, max(0.0, (v - s) / (1 - s)))

    sides = {"topo": [], "base": [], "esq": [], "dir": []}
    for k in range(rows):
        for name, get in (
                ("topo", lambda i: a(x0 + i, y0 + k)),
                ("base", lambda i: a(x0 + i, y0 + n - 1 - k)),
                ("esq", lambda i: a(x0 + k, y0 + i)),
                ("dir", lambda i: a(x0 + n - 1 - k, y0 + i))):
            length = sum(cov(get(i)) for i in range(n))
            sides[name].append((n - length) / 2)
    return sides


def diag(prof: list[float]) -> float:
    for i in range(1, len(prof)):
        d0, d1 = prof[i - 1] - (i - 0.5), prof[i] - (i + 0.5)
        if d1 <= 0:
            return (i - 0.5) + d0 / (d0 - d1)
    return float("nan")


def rms(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", type=Path)
    ap.add_argument("--box", type=int, nargs=3, metavar=("X", "Y", "N"),
                    help="caixa do corpo; default: alfa>127 na linha e coluna centrais")
    ap.add_argument("--shadow", type=float, help="alfa da sombra sob a borda (0..1)")
    args = ap.parse_args()

    w, h, rgba = _png.read_png(args.png)
    alpha = rgba[3::4]
    if args.box:
        x0, y0, n = args.box
    else:
        row = [alpha[(h // 2) * w + x] for x in range(w)]
        col = [alpha[y * w + w // 2] for y in range(h)]
        x0 = next(i for i, v in enumerate(row) if v > 127)
        y0 = next(i for i, v in enumerate(col) if v > 127)
        n = w - 2 * x0
    s = args.shadow
    if s is None:
        s = alpha[(h // 2) * w + x0 - 1] / 255.0 if x0 > 0 else 0.0

    rows = int(n * 0.45)
    sides = measured_rows(alpha, w, x0, y0, n, s, rows)
    mean = [sum(v[i] for v in sides.values()) / 4 for i in range(rows)]

    fits = []
    frac = 0.15
    while frac <= 0.35 + 1e-9:
        fits.append((rms(model_rows(frac * n, n, rows), mean), frac))
        frac += 0.0025
    best = min(fits)
    m26 = model_rows(0.26 * n, n, rows)

    print(f"{args.png.name}: {w}x{h}, corpo {n} em ({x0},{y0}), sombra s={s:.4f}")
    print(f"  diag   medido {diag(mean):8.3f} px  ({diag(mean)/n:.5f} N)   "
          f"lados " + " ".join(f"{k} {diag(v):.2f}" for k, v in sides.items()))
    print(f"  diag   modelo r=0.26N {diag(m26):8.3f} px  ({diag(m26)/n:.5f} N)")
    print(f"  ajuste melhor r/N {best[1]:.4f} (r = {best[1]*n:.2f} px, {best[1]*824:.1f} em 824)"
          f"  rms {best[0]:.3f} px")
    print(f"  ajuste r/N 0.2600 rms {rms(m26, mean):.3f} px")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
