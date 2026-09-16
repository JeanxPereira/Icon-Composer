#!/usr/bin/env python3
"""Compara dois PNG RGBA do mesmo tamanho e diz ONDE eles diferem, nao so quanto.

Por que existe: um maximo global nao distingue "a borda pontilhou" de "o efeito
esta no lugar errado". Os dois dao delta 255. A media por bloco 8x8 distingue --
uma borda mal amostrada acende poucos blocos com media baixa, um efeito deslocado
acende uma regiao inteira com media alta.

A conta e feita entre PIXELS VISIVEIS: alpha > 0 em PELO MENOS UM dos dois. Um
pixel transparente nos dois lados nao e acerto nem erro, e conta-lo como acerto
infla qualquer icone com canto arredondado.

Cor e comparada NAO-premultiplicada, que e como os dois PNG a guardam.

Uso:
    python scripts/png-diff.py <a.png> <b.png>
    python scripts/png-diff.py <a.png> <b.png> --map <saida.png> --blocks 12
    python scripts/png-diff.py apple-512.png ours-412.png --legacy-inset
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
import zlib
from pathlib import Path


def read_png(path: Path) -> tuple[int, int, bytearray]:
    """Le um PNG RGBA8 nao-entrelacado, desfazendo os cinco filtros."""
    d = path.read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: nao e PNG")
    pos, idat, w, h, depth, ctype = 8, bytearray(), 0, 0, 0, 0
    while pos + 8 <= len(d):
        ln, tag = struct.unpack_from(">I4s", d, pos)
        body = d[pos + 8:pos + 8 + ln]
        if tag == b"IHDR":
            w, h, depth, ctype, _c, _f, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: so RGB/RGBA 8 bits nao-entrelacado "
                                 f"(depth={depth} type={ctype} interlace={interlace})")
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
        pos += 12 + ln
    nch = 4 if ctype == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = w * nch
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                c = prev[i - nch] if i >= nch else 0
                b = prev[i]
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise ValueError(f"{path}: filtro {ft} desconhecido")
        prev = line
        o = y * w * 4
        if nch == 4:
            out[o:o + w * 4] = line
        else:
            for x in range(w):
                out[o + 4 * x:o + 4 * x + 3] = line[3 * x:3 * x + 3]
                out[o + 4 * x + 3] = 255
    return w, h, out


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def place_inset(wa: int, ha: int, wb: int, hb: int, B: bytearray,
                rel: float) -> tuple[int, int, bytearray]:
    """Poe um render menor no lugar que o RECUO LEGADO lhe daria dentro do quadro.

    `[BIN]` `IconRendering.arm64` `0x4202C` (`FinalizedIcon.Configuration`), ramo
    de `useLegacyInsetting`: `lado' = lado - 2 * floor(lado * recuo + 0.5)`, com
    `recuo = globalConfig.relativeIconInset ?? 0.09765625` -- o imediato
    `0x3FB9000000000000` de `0x4224C`, que e 100/1024. E o unico 100/1024 do
    bundle. Nao e coisa deste renderizador: o app nunca liga esse modo (ele so
    importa o `Configuration(icon:style:parametersOverride:)`, que grava o campo
    em zero), entao o alinhamento pertence ao COMPARADOR e nao ao desenho.

    Copia pixel a pixel, sem reamostrar: nenhum erro de filtro entra na conta.
    """
    inset = int(math.floor(wa * rel + 0.5))
    want_w, want_h = wa - 2 * inset, ha - 2 * int(math.floor(ha * rel + 0.5))
    if (wb, hb) != (want_w, want_h):
        raise SystemExit(f"--legacy-inset: {wa}x{ha} pede um corpo de "
                         f"{want_w}x{want_h}, e veio {wb}x{hb}")
    out = bytearray(wa * ha * 4)
    dy = int(math.floor(ha * rel + 0.5))
    for y in range(hb):
        src = y * wb * 4
        dst = ((y + dy) * wa + inset) * 4
        out[dst:dst + wb * 4] = B[src:src + wb * 4]
    return wa, ha, out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    ap.add_argument("--map", type=Path, help="grava um mapa de calor do erro")
    ap.add_argument("--blocks", type=int, default=8, help="lado do bloco (8)")
    ap.add_argument("--top", type=int, default=12, help="quantos blocos piores listar")
    ap.add_argument("--legacy-inset", action="store_true",
                    help="poe o B (menor) dentro do quadro do A no recuo do "
                         "renderizador legado, sem reamostrar")
    ap.add_argument("--relative-inset", type=float, default=100.0 / 1024.0,
                    help="a fracao recuada por lado (default 100/1024)")
    args = ap.parse_args()

    wa, ha, A = read_png(args.a)
    wb, hb, B = read_png(args.b)
    if args.legacy_inset and (wa, ha) != (wb, hb):
        wb, hb, B = place_inset(wa, ha, wb, hb, B, args.relative_inset)
    if (wa, ha) != (wb, hb):
        print(f"erro: {wa}x{ha} contra {wb}x{hb}", file=sys.stderr)
        return 2
    w, h, bs = wa, ha, args.blocks

    visible = diff_px = 0
    max_ch = [0, 0, 0, 0]
    sum_ch = [0, 0, 0, 0]
    # A banda de SILHUETA: onde um lado e opaco e o outro nao existe, os quatro
    # canais saturam de uma vez. Ela domina o ranking de blocos sem ter peso na
    # media -- no gabarito da Apple sao 1,33 % dos visiveis carregando os doze
    # piores blocos 8x8. Separa-la e o que impede ler "os piores blocos sao os
    # quatro cantos" como "o maior erro do projeto sao os cantos".
    band_n = 0
    band_ch = [0, 0, 0, 0]
    nbx, nby = (w + bs - 1) // bs, (h + bs - 1) // bs
    blocks = [[0.0, 0] for _ in range(nbx * nby)]
    heat = bytearray(w * h * 4)

    for y in range(h):
        row = y * w * 4
        by = y // bs
        for x in range(w):
            o = row + 4 * x
            aa, ab = A[o + 3], B[o + 3]
            if aa == 0 and ab == 0:
                continue
            visible += 1
            d = [abs(A[o + k] - B[o + k]) for k in range(4)]
            worst = max(d)
            if d[3] > 128:
                band_n += 1
                for k in range(4):
                    band_ch[k] += d[k]
            if worst:
                diff_px += 1
                for k in range(4):
                    if d[k] > max_ch[k]:
                        max_ch[k] = d[k]
                    sum_ch[k] += d[k]
            bi = by * nbx + (x // bs)
            blocks[bi][0] += worst
            blocks[bi][1] += 1
            v = min(255, worst * 3)
            heat[o] = v
            heat[o + 1] = 255 - v if worst else 0
            heat[o + 2] = 0
            heat[o + 3] = 255

    if not visible:
        print("nada visivel nos dois")
        return 1

    print(f"{args.a.name}  x  {args.b.name}   {w}x{h}")
    print(f"  pixels visiveis (alpha>0 num dos dois): {visible}  "
          f"({100.0*visible/(w*h):.1f} % do quadro)")
    print(f"  pixels diferentes: {diff_px}  ({100.0*diff_px/visible:.2f} % dos visiveis)")
    print("  delta por canal   R     G     B     A")
    print("    maximo       " + "".join(f"{v:>6}" for v in max_ch))
    print("    medio(vis)   " + "".join(f"{v/visible:>6.2f}" for v in sum_ch))
    if band_n:
        resto = visible - band_n
        print(f"  banda de silhueta (|dAlpha| > 128): {band_n} px "
              f"({100.0*band_n/visible:.2f} % dos visiveis)")
        print("    medio na banda" + "".join(f"{v/band_n:>6.1f}" for v in band_ch))
        if resto:
            print("    medio SEM ela "
                  + "".join(f"{(s-b)/resto:>6.2f}" for s, b in zip(sum_ch, band_ch)))

    scored = sorted(((s / n if n else 0.0, i) for i, (s, n) in enumerate(blocks)),
                    reverse=True)
    print(f"  piores blocos {bs}x{bs} (media do delta maximo por pixel):")
    for avg, i in scored[:args.top]:
        print(f"    ({(i % nbx)*bs:4d},{(i // nbx)*bs:4d})  {avg:7.2f}")
    acesos = sum(1 for s, n in blocks if n and s / n >= 8.0)
    print(f"  blocos com media >= 8: {acesos} de {nbx*nby} "
          f"({100.0*acesos/(nbx*nby):.1f} %)")

    if args.map:
        write_png(args.map, w, h, bytes(heat))
        print(f"  mapa -> {args.map}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
