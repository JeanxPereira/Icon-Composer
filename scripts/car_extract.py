#!/usr/bin/env python3
"""Extrai os payloads de um asset catalog compilado (`.car`) -- inclusive os
tipos que o Icon Composer usa para gravar um documento `.icon` COMPILADO.

Por que existe: `car_dump.py` (do projeto AquaKit) LISTA as renditions, mas nao
tira o conteudo de dentro. E, no caso do `Assets.car` do proprio Icon Composer,
o conteudo que interessa nao esta nem no payload: as renditions
`AppIcon.iconstack` (layout 1019) e `IconGroup` (layout 1020) tem payload
`RAWD` de comprimento ZERO -- o grafo de camadas inteiro mora na secao TLV do
`csiheader`. Este script le a TLV.

O que se extrai, por tipo de payload:

  RAWD (bytes 'DWAR')  dado cru; quando comprimido vem como um stream de blocos
                       LZFSE ('bvx1'/'bvx2'/'bvxn'/'bvx-'), que e como os seis
                       `image.svg` estao guardados.
  CELM (bytes 'MLEC')  bitmap comprimido; o container interno e 'KCBC', uma
                       sequencia de pedacos LZFSE.
  COLR (bytes 'RLOC')  cor em componentes `double`; vai para o manifesto.
  ARGG                 gradiente nomeado: inicio, fim e as paradas, cada uma
                       apontando para uma COLR pelo nome; vai para o manifesto.

A secao TLV de um documento em camadas. Um registro por filho em cada tag, na
mesma ordem; todas abrem com `u32 count, u32 pad`.

Os nomes NAO sao palpite: sao os setters de `CUIRenditionLayerReference` que
`-[_CUILayerStackRendition _initWithCSIHeader:version:]` (CoreUI 26A5416b,
0x18b51a1a4..0x18b51a724) chama com cada campo, na ordem em que os le:

  0x3f4 (1012)  [u32 bit0 fixedFrame][i32 x][i32 y][u32 w][u32 h]   -> setFrame:
                [u32 blendMode][f32 opacity][u32 keyLen][key]       -> setReferenceKey:
                a chave sao pares u16 (atributo, valor) que apontam para um
                FACETKEY; w x h e 1024 x 1024 nas folhas e 0 x 0 nos grupos.
  0x3fc (1020)  [u32 bit0 hasLightingEffects, bit1 gathersSpecularByElement]
                [f32 blurStrength][u32 strlen][char gradientOrColorName[strlen]]
  0x3fd (1021)  [u32 bit0 hasSpecular][f32 translucency][u32 shadowStyle]
                [f32 shadowOpacity][u32 specularPlacement]
  0x3fe (1022)  [f32 refractionStrength][f32 refractionHeight][u32 ?]
                -- o terceiro campo esse leitor nao le.
  0x3ec (1004)  [f32][f32]
  0x3ed (1005)  UTI: [u32 len][u32][char str[]]  -> "public.layeredimage"
  0x3ee (1006)  [u32]

Os seletores saem da ordem dos stubs: as chamadas vao a enderecos espacados de
0x10 que sobem em ordem alfabetica de seletor, e os quinze desta funcao caem,
um a um e com o tipo de argumento certo (bool, inteiro, float), nos setters que
`CUIRenditionLayerReference` declara. A regiao dos stubs nao esta mapeada na
extracao do cache, entao nenhum stub foi desmontado para confirmar o nome.

Uso:
    python scripts/car_extract.py <file.car> --list
    python scripts/car_extract.py <file.car> --out <dir>
    python scripts/car_extract.py <file.car> --out <dir> --grep svg
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

# car_dump.py vive no projeto AquaKit e e SO LEITURA; so o leitor de BOM e
# reaproveitado daqui.
_AQUAKIT = Path(r"D:/CodingProjects/AquaKit/References/scripts")
if _AQUAKIT.is_dir():
    sys.path.insert(0, str(_AQUAKIT))
try:
    from car_dump import Bom, cstr, CSI_HEADER_LEN  # type: ignore
except ImportError:  # pragma: no cover
    print("error: car_dump.py nao encontrado (AquaKit/References/scripts)", file=sys.stderr)
    raise

APPEARANCE_TOKEN = 7

# ------------------------------------------------------------------ descompressao


def lzfse_decompress(blob: bytes, expected: int = 0) -> bytes | None:
    """Stream de blocos LZFSE/LZVN. Precisa de `liblzfse`; sem ele, devolve None
    para os blocos que nao sejam 'bvx-' (nao comprimido), que e tratado aqui."""
    if blob[:4] == b"bvx-":
        n = struct.unpack_from("<I", blob, 4)[0]
        return blob[8:8 + n]
    try:
        import liblzfse  # type: ignore
    except ImportError:
        return None
    try:
        return liblzfse.decompress(blob)
    except Exception:
        return None


def decode_rawd(pay: bytes) -> tuple[bytes, str]:
    """RAWD: tag(4) version(4) length(4) data[length]."""
    if len(pay) < 12:
        return b"", "rawd-curto"
    _tag, _ver, ln = struct.unpack_from("<4sII", pay, 0)
    data = pay[12:12 + ln]
    if data[:3] == b"bvx":
        out = lzfse_decompress(data)
        if out is None:
            return data, "lzfse-sem-decoder"
        return out, "lzfse"
    if data[:2] == b"\x78\x9c" or data[:2] == b"\x78\x01":
        try:
            return zlib.decompress(data), "zlib"
        except zlib.error:
            pass
    return data, "cru"


def decode_celm(pay: bytes) -> tuple[bytes, str]:
    """CELM: tag(4) version(4) compression(4) rawLength(4) [container].

    Com compression 4 o corpo e uma CADEIA de registros 'KCBC', um por faixa de
    linhas -- NAO um unico stream. Cada registro:

        'KCBC'(4) u32 flags(4) u32 zero(4) u32 nRows(4) u32 nCompressed(4)
        + nCompressed bytes de stream LZFSE (que termina no seu proprio 'bvx$')

    Medido no Assets.car do Icon Composer 27.0-129: faixas de 170 linhas, 3
    registros para uma imagem de 512 linhas (170+170+172). Tratar o corpo como
    um stream unico devolve so a primeira faixa -- 1/3 da imagem."""
    if len(pay) < 16:
        return b"", "celm-curto"
    _tag, _ver, comp, raw_len = struct.unpack_from("<4sIII", pay, 0)
    body = pay[16:]
    if body[:4] == b"KCBC":
        out, p, chunks, rows = bytearray(), 0, 0, 0
        while p + 20 <= len(body) and body[p:p + 4] == b"KCBC":
            n_rows, csize = struct.unpack_from("<II", body, p + 12)
            blk = body[p + 20:p + 20 + csize]
            part = lzfse_decompress(blk)
            if part is None:
                return bytes(body), f"kcbc-sem-decoder(comp={comp})"
            out += part
            rows += n_rows
            chunks += 1
            p += 20 + csize
        if chunks:
            return bytes(out), f"kcbc[{chunks}ch,{rows}lin](comp={comp})"
        return bytes(body), f"kcbc-ilegivel(comp={comp})"
    if body[:3] == b"bvx":
        out2 = lzfse_decompress(body)
        if out2 is not None:
            return out2, f"lzfse(comp={comp},raw={raw_len})"
    return body, f"cru(comp={comp},raw={raw_len})"


def decode_colr(pay: bytes) -> dict | None:
    """COLR: tag(4) version(4) colorSpaceID(4) nComponents(4) double[nComponents].

    Os componentes sao `double` e ficam como estao; quem escolhe a precisao de
    escrita e quem monta o `icon.json`."""
    if len(pay) < 16:
        return None
    _tag, _ver, cs, n = struct.unpack_from("<4sIII", pay, 0)
    if n > 8 or 16 + 8 * n > len(pay):
        return None
    return {"colorSpaceID": cs & 0xFF,
            "components": list(struct.unpack_from(f"<{n}d", pay, 16))}


def decode_argg(pay: bytes) -> dict | None:
    """ARGG (gradiente nomeado de um documento `.icon` compilado):

        'ARGG'(4) u32 nStops u32 (=1) u32 (=0)
        f32 startX f32 startY f32 endX f32 endY      -- unidade do quadro, y p/ baixo
        nStops * [f32 offset][u32 strlen][char name[strlen]]   -- name com o NUL

    Cada parada NOMEIA uma rendition COLR do mesmo catalogo; a cor nao esta
    aqui. Medido em dois catalogos (Icon Composer 27.0-129 e SF Symbols 8), os
    dois so com gradientes de duas paradas -- por isso `nStops` e conferido
    contra o fim do payload em vez de so acreditado."""
    if len(pay) < 32 or pay[:4] != b"ARGG":
        return None
    n, kind, pad = struct.unpack_from("<III", pay, 4)
    sx, sy, ex, ey = struct.unpack_from("<4f", pay, 16)
    stops, p = [], 32
    while p + 8 <= len(pay):
        off, slen = struct.unpack_from("<fI", pay, p)
        p += 8
        if p + slen > len(pay):
            return None
        stops.append({"offset": off,
                      "color": pay[p:p + slen].split(b"\x00")[0].decode("utf-8", "replace")})
        p += slen
    return {"count": n, "kind": kind, "pad": pad,
            "start": [sx, sy], "end": [ex, ey], "stops": stops}


# ------------------------------------------------------------------ TLV


def tlvs(blob: bytes):
    p = 0
    while p + 8 <= len(blob):
        tag, ln = struct.unpack_from("<II", blob, p)
        p += 8
        if ln > len(blob) - p:
            return
        yield tag, blob[p:p + ln]
        p += ln


def key_pairs(b: bytes) -> list[tuple[int, int]]:
    out = []
    for o in range(0, len(b) - 3, 4):
        a, v = struct.unpack_from("<HH", b, o)
        if (a, v) == (0, 0):
            break
        out.append((a, v))
    return out


def parse_children(b: bytes) -> list[dict]:
    """TLV 0x3f4: u32 count, u32 pad, depois count registros de 32 bytes + chave."""
    if len(b) < 8:
        return []
    n = struct.unpack_from("<I", b, 0)[0]
    out, p = [], 8
    for _ in range(n):
        if p + 32 > len(b):
            break
        flags, x, y, w, h, blend, opacity, klen = struct.unpack_from("<IiiIIIfI", b, p)
        p += 32
        out.append({
            "fixedFrame": bool(flags & 1),
            "origin": [x, y],
            "size": [w, h],
            "blend": blend,
            "opacity": round(opacity, 6),
            "key": key_pairs(b[p:p + klen]),
        })
        p += klen
    return out


def parse_nodes(b: bytes) -> list[dict]:
    """TLV 0x3fc: u32 count, u32 pad, depois registros de tamanho variavel."""
    if len(b) < 8:
        return []
    n = struct.unpack_from("<I", b, 0)[0]
    out, p = [], 8
    for _ in range(n):
        if p + 12 > len(b):
            break
        bits, blur, slen = struct.unpack_from("<IfI", b, p)
        p += 12
        s = b[p:p + slen]
        p += slen
        out.append({"hasLightingEffects": bool(bits & 1),
                    "gathersSpecularByElement": bool(bits & 2),
                    "blurStrength": round(blur, 6),
                    "gradientOrColorName": s.split(b"\x00")[0].decode("utf-8", "replace")})
    return out


def parse_effects(b: bytes) -> list[dict]:
    """TLV 0x3fd: u32 count, u32 pad, count * 20."""
    if len(b) < 8:
        return []
    n = struct.unpack_from("<I", b, 0)[0]
    out = []
    for i in range(n):
        o = 8 + 20 * i
        if o + 20 > len(b):
            break
        spec, transl, style, shadow, placement = struct.unpack_from("<IfIfI", b, o)
        out.append({"hasSpecular": bool(spec & 1), "translucency": round(transl, 6),
                    "shadowStyle": style, "shadowOpacity": round(shadow, 6),
                    "specularPlacement": placement})
    return out


def parse_refraction(b: bytes) -> list[dict]:
    """TLV 0x3fe: u32 count, u32 pad, count * 12."""
    if len(b) < 8:
        return []
    n = struct.unpack_from("<I", b, 0)[0]
    out = []
    for i in range(n):
        o = 8 + 12 * i
        if o + 12 > len(b):
            break
        strength, height, tail = struct.unpack_from("<ffI", b, o)
        out.append({"refractionStrength": round(strength, 6),
                    "refractionHeight": round(height, 6), "unread": tail})
    return out


def parse_tlv_section(blob: bytes) -> dict:
    out: dict = {"raw": {}}
    for tag, b in tlvs(blob):
        if tag == 0x3f4:
            out["children"] = parse_children(b)
        elif tag == 0x3fc:
            out["nodes"] = parse_nodes(b)
        elif tag == 0x3fd:
            out["effects"] = parse_effects(b)
        elif tag == 0x3fe:
            out["refraction"] = parse_refraction(b)
        elif tag == 0x3ed and len(b) >= 8:
            out["uti"] = b[8:].split(b"\x00")[0].decode("utf-8", "replace")
        elif tag == 0x3ec and len(b) >= 8:
            out["scale"] = [round(v, 6) for v in struct.unpack_from("<2f", b, 0)]
        out["raw"][f"0x{tag:x}"] = b.hex()
    return out


# ------------------------------------------------------------------ catalogo


EXT = {"DWAR": ".bin", "MLEC": ".bin", "RLOC": ".json", "SISM": ".bin", "ARGG": ".bin"}


def write_png(path: Path, w: int, h: int, rgba8: bytes) -> None:
    """PNG RGBA8 sem filtro -- evita depender de Pillow para gravar o gabarito."""
    raw = b"".join(b"\x00" + rgba8[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b""))


def bitmap_to_rgba8(data: bytes, w: int, h: int, bpr: int) -> tuple[bytes, str] | None:
    """Converte o bitmap cru do CELM em RGBA8 NAO-premultiplicado.

    Duas formas, e elas NAO tem a mesma ordem de canal (medido no Assets.car do
    Icon Composer 27.0-129, comparando os dois bitmaps do mesmo `AppIcon256x256`
    pixel a pixel):

      bpr == w*4  BGRA, 8 bits por canal, premultiplicado.
      bpr == w*8  RGBA, half-float (binary16) por canal, premultiplicado --
                  ordem INVERTIDA em relacao ao de 8 bits.

    Os dois carregam a MESMA funcao de transferencia: em (100,100) o de 8 bits
    da 48 e o half da 0.1882, e 0.1882*255 = 48.0. O de 16 bits e so precisao,
    nao espaco linear."""
    if h <= 0 or w <= 0 or bpr <= 0 or len(data) < bpr * h:
        return None
    if bpr == w * 4:
        out = bytearray(w * h * 4)
        for y in range(h):
            row = data[y * bpr:y * bpr + w * 4]
            o = y * w * 4
            for x in range(w):
                b, g, r, a = row[4 * x:4 * x + 4]
                if a and a != 255:
                    r = min(255, (r * 255 + a // 2) // a)
                    g = min(255, (g * 255 + a // 2) // a)
                    b = min(255, (b * 255 + a // 2) // a)
                out[o + 4 * x] = r
                out[o + 4 * x + 1] = g
                out[o + 4 * x + 2] = b
                out[o + 4 * x + 3] = a
        return bytes(out), "bgra8-premul"
    if bpr == w * 8:
        out = bytearray(w * h * 4)
        for y in range(h):
            px = struct.unpack_from(f"<{w*4}e", data, y * bpr)
            o = y * w * 4
            for x in range(w):
                r, g, b, a = px[4 * x:4 * x + 4]
                if a > 0.0:
                    r, g, b = r / a, g / a, b / a
                for k, v in enumerate((r, g, b, a)):
                    out[o + 4 * x + k] = max(0, min(255, int(v * 255.0 + 0.5)))
        return bytes(out), "rgba16f-premul->8"
    return None


def sniff(data: bytes) -> str:
    if data[:5] == b"<?xml" or data[:4] == b"<svg":
        return ".svg"
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return ".png"
    if data[:8] == b"bplist00":
        return ".plist"
    if data[:1] in (b"{", b"["):
        return ".json"
    return ".bin"


def load(car: Path) -> tuple[Bom, dict]:
    bom = Bom(car.read_bytes())
    facets: dict[tuple, str] = {}
    for key, val in bom.tree_pairs("FACETKEYS"):
        name = cstr(key)
        if not name:
            continue
        pairs = []
        for o in range(2, len(val) - 3, 4):
            pairs.append(struct.unpack_from("<HH", val, o))
        facets[tuple(sorted(pairs))] = name
    return bom, facets


def resolve(facets: dict, pairs: list[tuple[int, int]]) -> str:
    """Casa um conjunto de pares (atributo, valor) com um FACETKEY.

    A direcao importa: a chave da rendition traz atributos que o facet nao tem
    (escala, gamut, ...), entao quem tem de ser subconjunto e o FACET -- menos o
    par de atributo 0, que e constante no catalogo e nunca aparece do outro
    lado."""
    have = set(pairs)
    for k, name in facets.items():
        need = {p for p in k if p[0] != 0}
        if need and need.issubset(have):
            return name
    return "?"


def extract(car: Path, out: Path | None = None, grep: str | None = None,
            list_only: bool = False) -> list[dict]:
    """Le o catalogo e devolve o manifesto; com `out`, grava os payloads e o
    `manifest.json` la. E o que `car_to_icon.py` chama quando recebe um `.car`."""
    bom, facets = load(car)
    tokens = bom.key_tokens()
    apps = bom.appearances()
    app_pos = tokens.index(APPEARANCE_TOKEN) if APPEARANCE_TOKEN in tokens else None
    needle = grep.lower() if grep else None
    write = out is not None and not list_only

    if out:
        out.mkdir(parents=True, exist_ok=True)

    manifest = []
    seen: dict[str, int] = {}
    for key, val in bom.tree_pairs("RENDITIONS"):
        if len(val) < CSI_HEADER_LEN:
            continue
        name = cstr(val[40:168])
        if not name or (needle and needle not in name.lower()):
            continue
        width, height, scale, _pf = struct.unpack_from("<4I", val, 12)
        layout = struct.unpack_from("<H", val, 36)[0]
        tlv_len = struct.unpack_from("<I", val, 168)[0]
        tlv_blob = val[CSI_HEADER_LEN:CSI_HEADER_LEN + tlv_len]
        off = CSI_HEADER_LEN + tlv_len
        tag = val[off:off + 4].decode("ascii", "replace")

        app = "-"
        if app_pos is not None and len(key) >= 2 * (app_pos + 1):
            aid = struct.unpack_from("<H", key, 2 * app_pos)[0]
            app = apps.get(aid, f"id{aid}").replace("NSAppearanceName", "")

        # A chave de uma rendition e uma LISTA de valores na ordem do KEYFORMAT;
        # a de um FACETKEY e um conjunto de pares (atributo, valor). Casar os
        # dois exige reetiquetar os valores com o token da sua posicao.
        vals = struct.unpack_from(f"<{len(key)//2}H", key, 0)
        kpairs = [(tokens[i], v) for i, v in enumerate(vals) if i < len(tokens) and v]
        facet = resolve(facets, kpairs)

        rec: dict = {"name": name, "appearance": app, "facet": facet,
                     "layout": layout, "payload": tag,
                     "width": width, "height": height, "scale": scale}

        data, how = b"", "-"
        if tag == "DWAR":
            data, how = decode_rawd(val[off:])
        elif tag == "MLEC":
            data, how = decode_celm(val[off:])
        elif tag == "RLOC":
            color = decode_colr(val[off:])
            if color:
                rec["color"], how = color, "colr"
        elif tag == "ARGG":
            grad = decode_argg(val[off:])
            if grad:
                rec["gradient"], how = grad, "argg"
        rec["decoded"] = how
        rec["decodedLength"] = len(data)

        if tlv_len and layout in (1019, 1020, 1021):
            sec = parse_tlv_section(tlv_blob)
            for ch in sec.get("children", []):
                ch["facet"] = resolve(facets, ch["key"])
            rec["tlv"] = sec

        if tag == "MLEC" and data:
            bpr = 0
            for t2, b2 in tlvs(tlv_blob):
                if t2 == 0x3ef and len(b2) >= 4:
                    bpr = struct.unpack_from("<I", b2, 0)[0]
            rec["bytesPerRow"] = bpr
            conv = bitmap_to_rgba8(data, width, height, bpr)
            if conv:
                rec["bitmap"] = conv[1]
                rec["_rgba8"] = conv[0]

        base = name.replace("/", "_").replace(" ", "_")
        if len(base) > 60:
            base = base[:60]
        slot = f"{base}_{app}"
        seen[slot] = seen.get(slot, 0) + 1
        if seen[slot] > 1:
            slot = f"{slot}_{seen[slot]}"
        ext = sniff(data) if data else EXT.get(tag, ".bin")
        rec["file"] = slot + ext

        rgba = rec.pop("_rgba8", None)
        if write:
            if data:
                (out / rec["file"]).write_bytes(data)
            if rgba:
                png = Path(rec["file"]).with_suffix("").name + f"_{width}x{height}.png"
                write_png(out / png, width, height, rgba)
                rec["png"] = png
        manifest.append(rec)

    manifest.sort(key=lambda r: (r["name"], r["appearance"]))
    if write:
        (out / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
    return manifest


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("car", type=Path)
    ap.add_argument("--out", type=Path, help="diretorio onde gravar os payloads")
    ap.add_argument("--list", action="store_true", help="so lista, nao grava")
    ap.add_argument("--grep", help="filtra por substring do nome")
    args = ap.parse_args()

    manifest = extract(args.car, args.out, args.grep, args.list)
    for r in manifest:
        print(f"{r['name'][:44]:<46} {r['appearance']:<10} {r['facet'][:26]:<28} "
              f"layout={r['layout']:<5} {r['payload']} {r['decoded']:<22} "
              f"{r['decodedLength']:>8}  -> {r['file']}")

    if args.out and not args.list:
        print(f"\n{len(manifest)} rendition(s) -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
