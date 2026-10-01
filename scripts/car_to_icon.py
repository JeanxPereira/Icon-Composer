#!/usr/bin/env python3
"""Reconstroi um bundle `.icon` a partir de um `Assets.car` compilado -- o
documento em camadas que o Icon Composer gravou la dentro, de volta para
`icon.json` + `Assets/`.

Recebe o `.car` direto (e chama `car_extract.py` por baixo) ou um diretorio que
`car_extract.py` ja tenha gravado.

O QUE E MEDIDO E O QUE E SUPOSTO
--------------------------------
Este script existe para manter essa fronteira visivel. Tudo que ele escreve sai
de uma destas duas gavetas, e a segunda vai inteira para `inferences.json` ao
lado do bundle:

  [BIN] medido no catalogo, com o nome que o CoreUI da a cada campo (o mapa
        esta no cabecalho de `car_extract.py`)
    - a arte: cada `image.svg`, byte a byte como saiu do RAWD/LZFSE;
    - o agrupamento: quais folhas em qual IconGroup, e quais IconGroup no
      iconstack, resolvido por chave de rendition contra FACETKEYS;
    - por filho: `opacity`, `blendMode` e a origem do quadro;
    - por folha: `hasLightingEffects` -> `glass`, e `gradientOrColorName`, que
      aponta para uma rendition COLR (cor, em `double`) ou ARGG (gradiente:
      inicio, fim e as paradas, cada uma nomeando uma COLR) -> `fill`;
    - por grupo: `hasSpecular` e `specularPlacement` -> `specular`,
      `shadowStyle` e `shadowOpacity` -> `shadow`, `translucency`, `blurStrength` ->
      `blur-material`, `refractionStrength` e `refractionHeight` ->
      `refractivity`, `gathersSpecularByElement` -> `lighting`;
    - o fundo: o filho do iconstack que nao e um IconGroup, e sim um ARGG;
    - as tres aparencias (Aqua, DarkAqua, ISAppearanceTintable): o que difere
      entre elas vira `<chave>-specializations`.

  [INF] suposto por este script -- cada item tem uma chave em `inferences.json`
    - ordem: o array compilado e tratado como TRAS->FRENTE e invertido, porque
      `groups[0]` e o da FRENTE no `icon.json`. Apoio: no icone do Icon Composer
      as opacidades do recorte Tintable caem 1,0 / 0,9 / 0,7 na direcao do
      fundo; no do SF Symbols os facets se chamam `*.back` e `*.front` e caem
      nessa ordem.
    - `specular`: `specularPlacement` 1 e escrito `"inside"`, que e o que o
      motor recebe do catalogo; se o documento de origem dizia `true` e foi o
      compilador que escreveu 1, isso nao se le daqui.
    - os `enabled`: o compilado so guarda o numero. `translucency.enabled` e
      `translucency != 0` e `refractivity.enabled` e `refractionStrength != 0`.
    - `gathersSpecularByElement` -> `lighting: individual`, e sem ele `combined`.
    - o espaco de cor: `colorSpaceID` 2 e 6 vem com 2 componentes e 3 com 4, o
      que casa com a tabela publica do CoreUI deslocada de um (1 srgb, 2 gray,
      3 display-p3, 4 extended-srgb, 5 extended-linear-srgb, 6 extended-gray).
    - uma cor nomeada vira `solid` -- o compilado nao distingue `solid` de
      `automatic-gradient`.
    - aparencia base = Aqua; DarkAqua -> `dark`, ISAppearanceTintable -> `tinted`.
    - `position` identidade -- a origem do quadro e (0, 0) em todo filho dos dois
      catalogos medidos; se vier diferente, vai crua para `inferences.json` e o
      bundle sai com a identidade.
    - `color-space-for-untagged-svg-colors` NAO e escrita: o compilado nao a
      guarda, e sem ela o hex dos SVG e lido como sRGB. No icone do SF Symbols e
      isso que a Apple desenhou: onde a arte aparece sozinha o pixel dela e o hex
      cru (coracao: arte 115,174,232, Apple 115,175,233; lido como Display P3
      daria 95,176,237), e onde a `Color-7` (Display P3, alfa 0,78) cobre a arte
      o pixel e 36,191,130 -- a conta com a cor convertida para sRGB da
      34,189,129, e sem converter da 84,187,133.
    - o nome de arquivo de cada asset -- o catalogo guarda o nome do FACET
      (`AppIcon_Assets/10.blue1`), nao o nome do arquivo que o autor importou.

Uso:
    python scripts/car_to_icon.py <Assets.car> --out <Nome.icon>
    python scripts/car_to_icon.py <Assets.car> --out <Nome.icon> --keep <dir>
    python scripts/car_to_icon.py <dir-do-car_extract> --out <Nome.icon>
    python scripts/car_to_icon.py <Assets.car> --list
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import sys
import tempfile
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


# AS TRES TABELAS ABAIXO SAO A PONTE DO ALVO, lida em IconRendering.arm64: e ele
# quem recebe estes numeros do CoreUI e os traduz para o modelo dele.
#
# [BIN] `shadowStyle` e TRADUZIDO, nao copiado: o conversor de `0xBCD0` faz
# `lsr(0x03020001, 8 * n)`, e a constante da 0 -> none, 1 -> automatic,
# 2 -> vibrant, 3 -> neutral; a tabela inversa esta em `0x94B08`. `vibrant` e o
# `layer-color` do documento. Um valor >= 4 e recusado pelo alvo com log.
SHADOW_STYLE = {0: "none", 1: "automatic", 2: "layer-color", 3: "neutral"}

# [BIN] `specularPlacement` passa cru com teste de faixa (`0xBE5C`): 0
# automatic, 1 inside, 2 outside. `hasSpecular` vai cru para o material.
SPECULAR_PLACEMENT = {0: True, 1: "inside", 2: "outside"}

# [INF] A tabela publica do CoreUI (0 srgb, 1 gray, 2 display-p3, ...) deslocada
# de um: e a unica leitura em que a contagem de componentes fecha nos tres ids
# vistos (2 e 6 com 2 componentes, 3 com 4).
COLOR_SPACE = {1: "srgb", 2: "gray", 3: "display-p3", 4: "extended-srgb",
               5: "extended-linear-srgb", 6: "extended-gray"}

# [BIN] `blendMode` e um CGBlendMode: o conversor de `0xB484` aceita 0..15, 26 e
# 27 (mascara `0x0C00FFFF`) e traduz pela tabela de bytes de `0x93D12`, a
# inversa exata da de `0x94AC0`; 1 e `multiply`. A tabela aqui so tem os dez
# modos que o `icon.json` sabe escrever: color-dodge (6), color-burn (7) e os
# de 10 a 15 existem no motor e nao no vocabulario do documento.
BLEND_MODE = {0: "normal", 1: "multiply", 2: "screen", 3: "overlay", 4: "darken",
              5: "lighten", 8: "soft-light", 9: "hard-light",
              26: "plus-darker", 27: "plus-lighter"}


def shadow(fx: dict, where: str) -> dict:
    kind = SHADOW_STYLE.get(fx["shadowStyle"])
    if kind is None:
        raise SystemExit(f"error: {where}: shadowStyle {fx['shadowStyle']} sem nome")
    return {"kind": kind, "opacity": fx["shadowOpacity"]}


def specular(fx: dict, where: str):
    if not fx["hasSpecular"]:
        return False
    placement = SPECULAR_PLACEMENT.get(fx["specularPlacement"])
    if placement is None:
        raise SystemExit(f"error: {where}: specularPlacement {fx['specularPlacement']} sem nome")
    return placement


def blend_mode(child: dict) -> str:
    mode = BLEND_MODE.get(child["blend"])
    if mode is None:
        raise SystemExit(f"error: {child['facet']}: modo de mescla {child['blend']} sem nome")
    return mode


# A aparencia base nao leva nome no `icon.json`; as outras duas sao as chaves de
# `<chave>-specializations`.
APPEARANCES = [("Aqua", None), ("DarkAqua", "dark"), ("ISAppearanceTintable", "tinted")]

LAYOUT_STACK, LAYOUT_GROUP, LAYOUT_COLOR = 1019, 1020, 1009
SYSTEM_FILLS = ("system-dark", "system-light")


def f32(v: float) -> float:
    """O `float` de 32 bits com o menor numero de casas que ainda o reproduz --
    0.27339 em vez de 0.27339008450508118."""
    for digits in range(1, 10):
        r = round(v, digits)
        if struct.pack("<f", r) == struct.pack("<f", v):
            return r
    return v


class Catalog:
    """O manifesto do `car_extract.py`, indexado como este script o consulta."""

    def __init__(self, manifest: list[dict], background: str):
        self.manifest = manifest
        self.background = background
        self.by_facet: dict[str, list[dict]] = {}
        self.nodes: dict[tuple[str, str], dict] = {}
        for r in manifest:
            self.by_facet.setdefault(r["facet"], []).append(r)
            if "tlv" in r and r["layout"] in (LAYOUT_STACK, LAYOUT_GROUP):
                self.nodes[(r["facet"], r["appearance"])] = r
        self.used_colors: set[str] = set()
        self.notes: dict = {}

    def stacks(self) -> list[str]:
        return sorted({r["facet"] for r in self.manifest if r["layout"] == LAYOUT_STACK})

    def node(self, facet: str, appearance: str) -> dict:
        """O no naquela aparencia; um catalogo que so gravou a base responde com
        ela."""
        return self.nodes.get((facet, appearance)) or self.nodes[(facet, "Aqua")]

    def color(self, facet: str) -> str:
        rec = next((r for r in self.by_facet.get(facet, []) if "color" in r), None)
        if rec is None:
            raise SystemExit(f"error: cor {facet!r} nao esta no manifesto (um diretorio "
                             "extraido antes de o car_extract.py ler COLR/ARGG "
                             "precisa ser extraido de novo)")
        self.used_colors.add(facet)
        c = rec["color"]
        space = COLOR_SPACE.get(c["colorSpaceID"])
        if space is None:
            raise SystemExit(f"error: {facet}: colorSpaceID {c['colorSpaceID']} sem nome")
        return space + ":" + ",".join(f"{v:.5f}" for v in c["components"])

    def fill(self, facet: str):
        """O valor de `fill` que o asset nomeado descreve: cor ou gradiente."""
        short = facet.split("/")[-1]
        rec = next((r for r in self.by_facet.get(facet, []) if "gradient" in r), None)
        if rec is None:
            return {"solid": self.color(facet)}
        g = rec["gradient"]
        stops = g["stops"]
        if len(stops) != 2 or [s["offset"] for s in stops] != [0.0, 1.0]:
            # `linear-gradient` no icon.json e um par de cores sem offsets.
            self.notes.setdefault("gradiente-fora-do-par-0-1", {})[facet] = stops
        value: dict = {"linear-gradient": [self.color(s["color"]) for s in stops]}
        if short in SYSTEM_FILLS and self.background == "auto":
            return short
        if short not in SYSTEM_FILLS:
            (sx, sy), (ex, ey) = g["start"], g["end"]
            value["orientation"] = {"start": {"x": f32(sx), "y": f32(sy)},
                                    "stop": {"x": f32(ex), "y": f32(ey)}}
        return value

    def asset(self, facet: str) -> tuple[str, str]:
        """(arquivo extraido, nome dentro de `Assets/`) da arte de uma folha."""
        short = facet.split("/")[-1]
        recs = self.by_facet.get(facet, [])
        for r in recs:
            if r["file"].endswith(".svg"):
                return r["file"], short + ".svg"
        for r in recs:
            if "png" in r:
                return r["png"], short + ".png"
        raise SystemExit(f"error: a folha {facet!r} nao tem arte extraida")


def specialize(key: str, values: list, neutral=None, absent=None) -> dict:
    """`values` na ordem de APPEARANCES. Tudo igual: a chave simples (ou nada, se
    for o valor neutro). Diferente: `<key>-specializations`, com a base sem
    `appearance` e so as aparencias que saem dela."""
    base = values[0]
    if all(v == base for v in values):
        return {} if base == neutral else {key: base}
    entries = [] if base == neutral else [{"value": base}]
    for (_, name), v in zip(APPEARANCES[1:], values[1:]):
        if v != base:
            entries.append({"appearance": name, "value": absent if v is None else v})
    return {key + "-specializations": entries}


def build(cat: Catalog, stack_facet: str) -> tuple[dict, dict, list[tuple[str, str]]]:
    stacks = [cat.node(stack_facet, app) for app, _ in APPEARANCES]
    base = stacks[0]
    if "nodes" not in base["tlv"]:
        raise SystemExit("error: manifesto de um car_extract.py antigo; extraia de novo")
    children = base["tlv"]["children"]
    for s in stacks[1:]:
        if [c["facet"] for c in s["tlv"]["children"]] != [c["facet"] for c in children]:
            raise SystemExit("error: o iconstack muda de filhos entre aparencias; "
                             "este script so especializa valores, nao estrutura")

    positions: dict = {}
    assets: dict[str, str] = {}
    groups = []
    doc: dict = {}

    for i in reversed(range(len(children))):            # -> frente->tras
        facet = children[i]["facet"]
        if (facet, "Aqua") not in cat.nodes:            # nao e IconGroup: o fundo
            doc.update(specialize("fill", [cat.fill(s["tlv"]["children"][i]["facet"])
                                           for s in stacks]))
            continue

        name = facet.split("/")[-1]
        nodes = [cat.node(facet, app) for app, _ in APPEARANCES]
        leaves = nodes[0]["tlv"]["children"]
        for n in nodes[1:]:
            if [c["facet"] for c in n["tlv"]["children"]] != [c["facet"] for c in leaves]:
                raise SystemExit(f"error: {facet!r} muda de camadas entre aparencias; "
                                 "este script so especializa valores, nao estrutura")

        layers = []
        for j in reversed(range(len(leaves))):          # -> frente->tras
            leaf = leaves[j]["facet"]                    # AppIcon_Assets/10.blue1
            src, fname = cat.asset(leaf)
            assets[fname] = src
            props = [n["tlv"]["nodes"][j] for n in nodes]
            layer: dict = {"name": leaf.split("/")[-1], "image-name": fname}
            layer.update(specialize("glass", [p["hasLightingEffects"] for p in props]))
            layer["hidden"] = False
            fills = [p["gradientOrColorName"] for p in props]
            layer.update(specialize("fill", [cat.fill(f) if f else None for f in fills],
                                    absent="none"))
            layer.update(specialize("opacity", [n["tlv"]["children"][j]["opacity"]
                                                for n in nodes], neutral=1.0))
            layer.update(specialize("blend-mode", [blend_mode(n["tlv"]["children"][j])
                                                   for n in nodes], neutral="normal"))
            if any(leaves[j]["origin"]):
                positions[f"{name}/{layer['name']}"] = leaves[j]["origin"]
            layers.append(layer)

        props = [s["tlv"]["nodes"][i] for s in stacks]
        fx = [s["tlv"]["effects"][i] for s in stacks]
        refr = [s["tlv"]["refraction"][i] for s in stacks]
        g: dict = {"name": name, "layers": layers}
        g.update(specialize("specular", [specular(e, name) for e in fx]))
        g.update(specialize("lighting", ["individual" if p["gathersSpecularByElement"]
                                         else "combined" for p in props]))
        g.update(specialize("shadow", [shadow(e, name) for e in fx]))
        g.update(specialize("translucency", [
            {"enabled": e["translucency"] != 0.0, "value": e["translucency"]} for e in fx]))
        g.update(specialize("blur-material", [p["blurStrength"] for p in props]))
        g.update(specialize("refractivity", [
            {"enabled": r["refractionStrength"] != 0.0,
             "strength": r["refractionStrength"], "depth": r["refractionHeight"]}
            if r["refractionStrength"] or r["refractionHeight"] else None for r in refr],
            absent={"enabled": False, "strength": 0.0, "depth": 0.0}))
        g["hidden"] = False
        g.update(specialize("opacity", [s["tlv"]["children"][i]["opacity"]
                                        for s in stacks], neutral=1.0))
        g.update(specialize("blend-mode", [blend_mode(s["tlv"]["children"][i])
                                           for s in stacks], neutral="normal"))
        if any(children[i]["origin"]):
            positions[name] = children[i]["origin"]
        groups.append(g)

    doc["groups"] = groups
    doc["supported-platforms"] = {"squares": "shared"}

    all_colors = sorted(r["facet"] for r in cat.manifest if r["layout"] == LAYOUT_COLOR)
    inferences: dict = {
        "ordem-do-array": "compilado tratado como TRAS->FRENTE e invertido para icon.json",
        "specular": "specularPlacement 1 escrito como 'inside'; o documento de origem "
                    "pode ter dito true",
        "enabled": "translucency.enabled = (translucency != 0); "
                   "refractivity.enabled = (refractionStrength != 0)",
        "lighting": "gathersSpecularByElement -> individual; sem ele -> combined",
        "espaco-de-cor": "colorSpaceID lido pela tabela do CoreUI deslocada de um: "
                         + ", ".join(f"{k}={v}" for k, v in COLOR_SPACE.items()),
        "cor-nomeada": "escrita como 'solid'; o compilado nao distingue de 'automatic-gradient'",
        "cores-sem-referencia": [c for c in all_colors if c not in cat.used_colors],
        "aparencias": "base = Aqua; DarkAqua -> dark; ISAppearanceTintable -> tinted",
        "position": positions or "identidade; a origem do quadro e (0, 0) em todo filho",
        "color-space-for-untagged-svg-colors": "omitida; o hex dos SVG fica em sRGB",
        "nomes-de-arquivo-dos-assets": "derivados do nome do FACET, nao do nome importado",
        "background": cat.background,
    }
    inferences.update(cat.notes)
    return doc, inferences, sorted((src, dst) for dst, src in assets.items())


def write_bundle(src: Path, out: Path, doc: dict, inferences: dict,
                 assets: list[tuple[str, str]], hoist: bool) -> None:
    (out / "Assets").mkdir(parents=True, exist_ok=True)
    moved = 0
    for src_name, dst_name in assets:
        if hoist and dst_name.endswith(".svg"):
            text = (src / src_name).read_text(encoding="utf-8")
            text, n = hoist_gradients(text)
            moved += n
            (out / "Assets" / dst_name).write_text(text, encoding="utf-8")
        else:
            shutil.copyfile(src / src_name, out / "Assets" / dst_name)
    inferences["gradientes-movidos-para-defs"] = moved
    (out / "icon.json").write_text(
        json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    (out / "inferences.json").write_text(
        json.dumps(inferences, indent=2, ensure_ascii=False), encoding="utf-8")


def run(args: argparse.Namespace, src: Path) -> int:
    if args.src.is_file():
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import car_extract
        manifest = car_extract.extract(args.src, src)
    else:
        manifest = json.loads((src / "manifest.json").read_text(encoding="utf-8"))

    cat = Catalog(manifest, args.background)
    stacks = cat.stacks()
    if args.list or not stacks:
        print("\n".join(stacks) if stacks else "nenhum iconstack (layout 1019) neste catalogo")
        return 0 if stacks else 1
    if args.out is None:
        raise SystemExit("error: falta --out")
    if args.name is None and len(stacks) > 1:
        raise SystemExit("error: mais de um icone; escolha com --name: " + ", ".join(stacks))
    name = args.name or stacks[0]
    if name not in stacks:
        raise SystemExit(f"error: {name!r} nao e um iconstack daqui: " + ", ".join(stacks))

    doc, inferences, assets = build(cat, name)
    write_bundle(src, args.out, doc, inferences, assets, args.hoist_gradients)

    print(f"{args.out}: {len(doc['groups'])} grupo(s), {len(assets)} asset(s)")
    for g in doc["groups"]:
        print(f"  {g['name']:<10} {[l['name'] for l in g['layers']]}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path,
                    help="um Assets.car, ou o diretorio de saida do car_extract.py")
    ap.add_argument("--out", type=Path, help="bundle .icon a criar")
    ap.add_argument("--name", help="qual icone, quando o catalogo tem mais de um")
    ap.add_argument("--list", action="store_true", help="so lista os icones do catalogo")
    ap.add_argument("--keep", type=Path,
                    help="com um .car: guarda aqui o que o car_extract.py tirou "
                         "(inclusive os PNG que a Apple renderizou)")
    ap.add_argument("--background", default="auto", choices=["auto", "gradient"],
                    help="auto = `system-dark`/`system-light` pelo nome, como o "
                         "documento original o diz (padrao); gradient = as duas "
                         "paradas medidas no ARGG")
    ap.add_argument("--hoist-gradients", action="store_true",
                    help="move todo <linearGradient> para um <defs> no topo -- "
                         "reescrita neutra que contorna o leitor de SVG deste "
                         "projeto, que so coleta gradiente filho de <defs>")
    args = ap.parse_args()

    if args.src.is_file() and args.keep is None:
        with tempfile.TemporaryDirectory() as tmp:
            return run(args, Path(tmp))
    return run(args, args.keep if args.src.is_file() else args.src)


if __name__ == "__main__":
    raise SystemExit(main())
