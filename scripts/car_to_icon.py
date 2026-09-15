#!/usr/bin/env python3
"""Reconstroi um bundle `.icon` a partir do que `car_extract.py` tirou do
`Assets.car` compilado -- para que o renderizador deste projeto possa ser
confrontado, pixel a pixel, com a saida que a propria Apple gravou no mesmo
catalogo.

O QUE E MEDIDO E O QUE E SUPOSTO
--------------------------------
Este script existe para manter essa fronteira visivel. Tudo que ele escreve sai
de uma destas duas gavetas, e a segunda vai inteira para `inferences.json` ao
lado do bundle:

  [BIN] medido no catalogo
    - a arte: os seis SVG, byte a byte como sairam do RAWD/LZFSE;
    - o agrupamento: quais folhas em qual IconGroup, e quais IconGroup no
      iconstack, resolvido por chave de rendition contra FACETKEYS;
    - a opacidade de cada filho (float[7] do registro de 48 bytes);
    - os numeros de efeito por grupo (TLV 0x3fd e 0x3fe);
    - o fundo: gradiente linear de `AppIcon_Assets/Color-2` (cinza 0,192) para
      `AppIcon_Assets/Color-3` (cinza 0,078), paradas em 0,0 e 1,0 (TLV ARGG).

  [INF] suposto por este script -- cada item tem uma chave em `inferences.json`
    - ordem: o array compilado e tratado como TRAS->FRENTE e invertido, porque
      `groups[0]` e o da FRENTE no `icon.json`. Apoio: no recorte Tintable as
      opacidades caem 1,0 / 0,9 / 0,7 na direcao do fundo, e a numeracao dos
      assets (2,4 cinza / 6,8 verde / 10,12 azul) sobe na mesma direcao.
    - o mapa de campos de 0x3fd: [specular, shadow.opacity, shadow.kind,
      translucency.value, translucency.enabled]. Escolhido pela ADJACENCIA
      (o par (opacity,kind) junto e o par (value,enabled) junto), NAO por
      diferenca de pixel. A leitura rival esta registrada em `inferences.json`.
    - o mapa de campos de 0x3fe: [blur-material, ?, lighting]. O segundo float
      (0,0 / 0,02) fica SEM nome.
    - `glass: true` em toda folha -- o compilado nao tem bit de vidro achado, e
      a saida da Apple tem realce e translucidez.
    - `position` identidade -- os 7 floats que precedem a opacidade sao todos
      zero no registro de filho.
    - nenhum `fill` de camada -- o campo de nome do TLV 0x3fc vem vazio nas
      folhas em System, e a arte carrega o proprio gradiente.
    - `color-space-for-untagged-svg-colors: display-p3` -- os SVG usam hex sem
      espaco declarado. [ART] 20 dos 145 documentos do corpus declaram a chave e
      os 20 dizem `display-p3`.
    - o nome de arquivo de cada asset -- o catalogo guarda o nome do FACET
      (`AppIcon_Assets/10.blue1`), nao o nome do arquivo que o autor importou.

Uso:
    python scripts/car_to_icon.py <dir-do-car_extract> --out <Nome.icon>
    python scripts/car_to_icon.py <dir> --out <Nome.icon> --background system-dark
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path

# `<linearGradient>` FORA de `<defs>`, que e onde o Illustrator o poe.
#
# Os seis SVG do proprio Icon Composer saem do "Adobe Illustrator 29.0.0 SVG
# Export Plug-In", que escreve o gradiente dentro do `<g>` da camada, logo antes
# do `<path>` que o usa. Pelo SVG isso e legal -- um elemento de gradiente e
# referenciado por id, esteja onde estiver -- mas `CoreSVG/Document.cpp` so
# chama `collectGradient` para filhos de `<defs>` (a cadeia do `else if
# (e.name == "defs")`), entao o `url(#SVGID_1_)` nao resolve e a arte sai sem
# cor nenhuma.
#
# Esta funcao MOVE o elemento para um `<defs>` no topo, sem tocar em nenhum
# atributo. E uma reescrita neutra: o SVG resultante tem exatamente a mesma
# semantica. Ela existe para separar o defeito do LEITOR do erro do
# RENDERIZADOR no diff -- nao para aproximar pixel.
_GRAD = re.compile(r"<(linearGradient|radialGradient)\b.*?</\1>", re.S)


def hoist_gradients(svg: str) -> tuple[str, int]:
    found = _GRAD.findall(svg)
    if not found:
        return svg, 0
    blocks = [m.group(0) for m in _GRAD.finditer(svg)]
    svg = _GRAD.sub("", svg)
    i = svg.find(">", svg.find("<svg"))
    if i < 0:
        return svg, 0
    defs = "\n<defs>\n" + "\n".join(blocks) + "\n</defs>\n"
    return svg[:i + 1] + defs + svg[i + 1:], len(blocks)

SHADOW_KIND = {0: "automatic", 1: "neutral", 2: "layer-color", 3: "none"}
# `[OBS]` A ordem acima e a de `Values.cpp` (automatic, neutral, layer-color,
# none). O compilado diz 3 nos tres grupos. Se o enum compilado tiver essa mesma
# ordem, 3 = `none` -- e um icone com sombra visivel nao pode dizer `none`. A
# leitura adotada e a INVERSA da tabela de `Values.cpp`; veja `inferences.json`.
SHADOW_KIND_ADOPTED = {3: "neutral"}


def build(src: Path, background: str) -> tuple[dict, dict, list[tuple[str, str]]]:
    man = json.loads((src / "manifest.json").read_text(encoding="utf-8"))

    by_facet_app: dict[tuple[str, str], dict] = {}
    svg_file: dict[str, str] = {}
    for r in man:
        if r["name"] == "image.svg":
            svg_file[r["facet"]] = r["file"]
        if "tlv" in r:
            by_facet_app[(r["facet"], r["appearance"])] = r

    stack = next(r for r in man
                 if r["name"] == "AppIcon.iconstack" and r["appearance"] == "Aqua")

    inferences: dict = {
        "ordem-do-array": "compilado tratado como TRAS->FRENTE e invertido para icon.json",
        "0x3fd-mapa": "[specular, shadow.opacity, shadow.kind, translucency.value, translucency.enabled]",
        "0x3fd-mapa-rival": "[specular, blur-material, shadow.kind, shadow.opacity, translucency.enabled]",
        "0x3fe-mapa": "[blur-material, SEM-NOME, lighting]",
        "0x3fe-campo-2-sem-nome": {"Group 3": 0.0, "Group 2": 0.02, "Group": 0.02},
        "shadow.kind": "compilado diz 3; adotado 'neutral' (a tabela de Values.cpp poria 'none' em 3)",
        "glass": "true em toda folha; nenhum bit de vidro foi achado no compilado",
        "position": "identidade; os 7 floats antes da opacidade sao zero",
        "fill-de-camada": "omitido; campo de nome vazio nas folhas em System",
        "color-space-for-untagged-svg-colors": "display-p3 ([ART] 20/20 do corpus que declaram)",
        "nomes-de-arquivo-dos-assets": "derivados do nome do FACET, nao do nome importado",
        "background": background,
    }

    groups = []
    assets: list[tuple[str, str]] = []
    children = stack["tlv"]["children"]
    fx_a = stack["tlv"]["fx_a"]
    fx_b = stack["tlv"]["fx_b"]

    # O filho 0 e o fundo (`system-dark`); ele vira a `fill` da raiz, nao um
    # grupo. Os demais sao os IconGroup, na ordem compilada tras->frente.
    ordered = list(range(1, len(children)))
    for i in reversed(ordered):                     # -> frente->tras
        ch = children[i]
        a, b = fx_a[i], fx_b[i]
        grp = by_facet_app[(ch["facet"], "Aqua")]
        layers = []
        for lch in reversed(grp["tlv"]["children"]):   # -> frente->tras
            leaf = lch["facet"]                        # AppIcon_Assets/10.blue1
            short = leaf.split("/")[-1]
            fname = short + ".svg"
            assets.append((svg_file[leaf], fname))
            layer = {"name": short, "image-name": fname, "glass": True,
                     "hidden": False}
            if lch["opacity"] != 1.0:
                layer["opacity"] = lch["opacity"]
            layers.append(layer)

        g: dict = {
            "name": ch["facet"].split("/")[-1],
            "layers": layers,
            "specular": bool(a["a"]),
            "shadow": {"kind": SHADOW_KIND_ADOPTED.get(a["c"], SHADOW_KIND.get(a["c"], "neutral")),
                       "opacity": a["f1"]},
            "translucency": {"enabled": bool(a["e"]), "value": a["f2"]},
            "blur-material": b["f1"],
            "hidden": False,
        }
        if ch["opacity"] != 1.0:
            g["opacity"] = ch["opacity"]
        groups.append(g)

    if background == "system-dark":
        fill: dict | str = "system-dark"
    else:
        # [BIN] as duas paradas do ARGG, com os `double` exatos do catalogo.
        fill = {"linear-gradient": ["extended-gray:0.19200,1.00000",
                                    "extended-gray:0.07800,1.00000"]}

    doc = {
        "color-space-for-untagged-svg-colors": "display-p3",
        "fill": fill,
        "groups": groups,
        "supported-platforms": {"squares": "shared"},
    }
    return doc, inferences, assets


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path, help="diretorio de saida do car_extract.py")
    ap.add_argument("--out", type=Path, required=True, help="bundle .icon a criar")
    ap.add_argument("--background", default="gradient",
                    choices=["gradient", "system-dark"],
                    help="gradient = as duas paradas medidas no ARGG (padrao); "
                         "system-dark = o nome, como o documento original o diz")
    ap.add_argument("--hoist-gradients", action="store_true",
                    help="move todo <linearGradient> para um <defs> no topo -- "
                         "reescrita neutra que contorna o leitor de SVG deste "
                         "projeto, que so coleta gradiente filho de <defs>")
    args = ap.parse_args()

    doc, inferences, assets = build(args.src, args.background)
    (args.out / "Assets").mkdir(parents=True, exist_ok=True)
    moved = 0
    for src_name, dst_name in assets:
        if args.hoist_gradients:
            text = (args.src / src_name).read_text(encoding="utf-8")
            text, n = hoist_gradients(text)
            moved += n
            (args.out / "Assets" / dst_name).write_text(text, encoding="utf-8")
        else:
            shutil.copyfile(args.src / src_name, args.out / "Assets" / dst_name)
    inferences["gradientes-movidos-para-defs"] = moved
    (args.out / "icon.json").write_text(
        json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    (args.out / "inferences.json").write_text(
        json.dumps(inferences, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"{args.out}: {len(doc['groups'])} grupo(s), {len(assets)} asset(s)")
    for g in doc["groups"]:
        print(f"  {g['name']:<10} {[l['name'] for l in g['layers']]} "
              f"shadow={g['shadow']} transl={g['translucency']} "
              f"blur={g['blur-material']} spec={g['specular']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
