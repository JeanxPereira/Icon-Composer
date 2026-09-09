#!/usr/bin/env python3
"""How far this renderer reaches into the corpus.

WHY THIS RUNS BEFORE THE CODE
-----------------------------
Every piece of the flat-fill path passes its own gate -- path buffer, vertex,
coverage, resolve, flat composite, render target -- and nothing connects them.
Before writing the connector, this measures what that connector would actually
be able to draw, so the target after it is chosen by the corpus and not by a
guess.

AND ON 2026-09-04 THE TWO BIGGEST BLOCKERS LEFT TOGETHER
---------------------------------------------------------
The painted STROKE and the BLEND both stopped blocking, and they had to go
together: measured by removing each family and re-judging, the stroke alone
freed 4 documents and the blend alone freed 4, while the two together freed
14. Six documents were held by both and by nothing else, so neither front
delivered them alone.

  stroke   `StrokeRender` flattens, emits the point stream the target's CPU
           emits (doc 03 §31), and evaluates the fragment's own coverage
           (§10, §33). Only a stroke painted with a GRADIENT still blocks,
           and no corpus document has one.
  blend    the ten modes the format can spell are transcribed and the
           compositor mixes per layer; a blended GROUP gets a target of its
           own.

THAT REFUSAL WAS READ ON 2026-09-04 AND NARROWED ON 2026-09-05. It used to
cover every group that blends AND carries glass, and it cost exactly the
difference between the spec's promise (178 layers, 47 documents) and the
number here (169 and 40). Apple's glass does not sample the destination at all
-- `GlassDisplacementStyle::draw` filters the ITEM it is applied to (doc 03
§34.3) -- but OUR `glassOver` displaces the accumulation buffer in place, so a
group's fresh target really does starve it. What closed the gap was narrowing
rather than lifting: the coupling only bites when the refraction MOVES
something, and no blocked document had a refracting glass. The refusal that
survives is that one, and it is a difference in our model rather than an
unread question.

WHAT COUNTS AS "IN THE SLICE"
-----------------------------
A layer is in the slice when it is drawable with what is gated today: any of the
seven `fill` kinds, a `normal` blend, and art the renderer can place -- a PNG,
or vector art the CoreSVG reader turns into filled paths, flat or
gradient-filled, with no stroke, filter, mask, clip path, pattern, `use` or
embedded raster.

THREE THINGS HAVE LEFT THE BLOCKER LIST: RASTER and the SVG's `url(#id)`
GRADIENT paint, both on 2026-09-01, and on 2026-09-02 the GLASS LAYER. The
number this script prints is therefore not comparable across any of them --
said here rather than letting a rising number look like the corpus changed.

AND ON 2026-09-03, THE `fill` LEFT IT ENTIRELY
----------------------------------------------
`Source/RenderBox/FillResolve.h` reads the document's seven `Fill.Kind` cases
through the target's TWO converters, and `IconRenderer` now paints all seven --
on the background as well as on a layer, which is new: until today the root
`fill` was not read at all and the layers were composited over nothing.

Three retirements arrived together, and they are one work item and not three:

  `automatic`           on a layer it is not a colour, it is what that same
                        layer resolves at the `light` slot; on the background it
                        is a system chiclet ramp, or `IconColor.clear` under
                        `tinted`.
  `automatic-gradient`  `2026-09-01-gradiente.md` §4.4 refused it because THE
                        AXIS HAD NEVER BEEN READ. It has been: every converter
                        site passes `placement: nil`, and nil means
                        `GradientPlacement.default`, `(0,0)->(0,1)`.
  `linear-gradient`     the same nil closed its last hole. A layer ramp that
                        names no `orientation` was refused before; it now draws
                        on that default axis, and `[ART]` that is 26 of the
                        corpus's 48 layer `linear-gradient` fills -- the common
                        case, not an edge.

STAGED, so the spec's promise stays checkable: with `automatic` alone the layer
ruler reads 125 and the document ruler 29; with `automatic-gradient` too, 140
and 32; with the `linear-gradient` hole closed as well, 145 and 33.

And on 2026-09-04, with the stroke and the blend: **169 and 40**.

WHAT THIS SCRIPT STILL DOES NOT CONSULT, and it is why its layer count runs
ahead of the renderer's own: `hidden`, and whether a layer names art in the
context being drawn. It counts EVERY REACHABLE VALUE by design (see the
specialization note below), so a layer hidden in one context and drawn in
another counts as drawable here and is not drawn there. `[ART]` Over this corpus
that difference is 5 layers: this script says 145 and `renderIcon` at the base
appearance draws 140.

AND WHAT IS DRAWN WITHOUT HAVING BEEN READ. A `fill` that no longer blocks is
not a `fill` that is finished. `[OBS]` The background is painted over the whole
canvas square because the chiclet's corner geometry was never read; `[OBS]` a
`.system` fill is placed on the shape's bounding rect because
`supportsChicletAlignmentForSystemFills` is true by default and the `CGSize` it
substitutes was not read; and `[OBS]` the direction of the default axis is
undetermined, which is nearly invisible on the 255->245 ramp and is not on
31->15. The renderer names all three in `RenderedIcon::notes` on every render
that provokes them. Drawable is not the same as the target's pixel -- the same
distinction this script already draws for the glass material.

WHAT "GLASS IS NO LONGER A BLOCKER" MEANS, EXACTLY
-------------------------------------------------
The compositor draws a glass layer now (`Source/RenderBox/GlassLayer.h`). The
layer's `glass` bit is a PARTICIPATION flag; the refraction PARAMETERS live on
the group, in `refractivity`, and `[ART]` only 5 of the corpus's 271 groups
carry that key at all -- 2 with a non-zero strength. Where the strength is zero
the shader's one argument is zero, every displacement offset is exactly zero,
and the glass layer draws its own art over an untouched backdrop. That is a
DRAW, not a gap, so it is not counted as a blocker.

ONE GLASS BLOCKER REMAINS, and it is narrower than the old one: a glass layer
whose group DOES refract and whose art is a RASTER. `generateField` eats
polylines and a raster has no path to flatten, so there is no shape to build a
field from. `[ART]` In this corpus that is exactly one layer -- and it happens
to be one of the only two the corpus could have refracted at all.

TWO RULERS, AND THE SECOND IS THE ONE THAT DECIDES
--------------------------------------------------
Per DOCUMENT is all-or-nothing: nine flat layers and one glass layer score
zero. That is the right ruler for "can this icon be rendered" and the wrong one
for "is the connector worth building", because it hides how much of the drawing
work the flat path already covers. So the reach is reported per LAYER too, and
that is the number the decision hangs on.

THE SPECIALIZATION SUFFIX -- THE DEFECT THAT MADE THE FIRST THREE RUNS LIE
---------------------------------------------------------------------------
A property does not nest its specializations: it gets a SIBLING key. A layer
carries `image-name` when it is simple and `image-name-specializations` when it
is not (doc 01 §5), and the same for `fill`, `blend-mode` and `glass`.

The first version of this looked only for the bare keys. It therefore saw NO
assets at all in a document that specializes every one of them, found no
blockers, and reported it as fully drawable. Two of the two documents it called
"in the slice" were exactly that: not flat, just unread. It also reported ZERO
dangling references where doc 02 §3 had measured two -- and that disagreement
between two of my own measurements is what exposed it. A reader that skips a
key does not fail; it reports a smaller world and calls it clean.

THREE MORE THINGS THIS SEPARATES
--------------------------------
1. **Art the corpus does not carry.** Only 55 of the 145 bundles ship an
   `Assets/` folder; the rest were fetched document-only. A gap in the CORPUS is
   not a gap in the renderer, so the reach is computed over the 55 that can be
   judged and the others are reported apart.
2. **The background and the layers.** The top-level `fill` is the icon's
   background, and in the corpus it is usually a gradient. Counting it would
   report a reach near zero and hide the real question, so both are reported.
3. **A fill kind and a fill modifier.** `fill` is an object whose key names the
   kind, OR a bare string (`automatic`, `none`, `system-light`, `system-dark`).
   `orientation` sits as a SIBLING of `linear-gradient` in the same object -- a
   modifier, not a kind. An earlier run read the string form character by
   character and reported letters as fill kinds.

USAGE
    python scripts/slice-reach.py References/corpus
"""
from __future__ import annotations

import io
import json
import re
import sys
from collections import Counter
from pathlib import Path

SUFFIX = "-specializations"
FILL_MODIFIERS = {"orientation"}

# The seven `Fill.Kind` cases the renderer paints -- which, since 2026-09-03, is
# all seven of them. Kept as a set rather than deleted with the check it guards:
# the vocabulary is closed today, and an EIGHTH case appearing in a future
# document should show up as a named blocker rather than be drawn as whatever
# the reader made of it.
DRAWN_FILLS = {"none", "automatic", "solid", "automatic-gradient", "linear-gradient",
               "system-light", "system-dark"}

# What the CoreSVG reader turns into filled paths today, and what it does not.
# DOIS BLOQUEIOS NAO TEM CONSERTO EM CODIGO, e o teto real desta regua e 53 e
# nao 55. Medido em 2026-09-05:
#
#   chromium__chromium__AppIcon  pede `2 - Layer.svg` (travessao)  -> nao existe
#   mysk-research__loupe__Loupe  pede `loupe-icon-light 3.png`     -> nao existe
#
# Os dois nomes vem de ESPECIALIZACOES, e o Assets/ de cada bundle traz outros
# arquivos (`blue.svg`, `loupe-icon-light.png`): o autor renomeou a arte antes de
# publicar o repositorio. Nenhum leitor pode desenhar um arquivo que nao veio, e
# por isso "referencia pendurada" e categoria propria aqui em vez de virar mais
# uma lacuna que alguem vai tentar fechar.

SVG_BLOCKERS = {
    # O TRACO CHAPADO SAIU DAQUI EM 2026-09-04. `StrokeRender` o desenha:
    # ponto a ponto pelo fluxo que a CPU do alvo emite (doc 03 §31), cobertura
    # pelo fragment (§10, §33), e a largura escalada pelo mapa. O que
    # continua bloqueando e so o traco pintado com GRADIENTE, que nenhum
    # documento do corpus usa -- os 35 sao chapados, 31 hex e 4 `white`.
    "traço com url(#)": re.compile(r'stroke\s*[=:]\s*["\']?\s*url\(', re.I),
    # O FILTRO SAIU DAQUI EM 2026-09-09, e passou a `filter_blockers` -- uma
    # regex nao consegue mais responder por ele, porque a resposta depende de
    # QUAL cadeia a referencia resolve.
    #
    # `[BIN]` O alvo constroi SEIS primitivas (`SVGFilter::filterPrimitive`,
    # CoreSVG.arm64 0x2A230, tabela de seis em `__const:0x327F0`) e derruba o
    # resto na CONSTRUCAO. O buraco vira null, o null atravessa a cadeia, e
    # `SVGFilter::draw` (0x29A34) manda o resultado nulo para um caminho que so
    # limpa -- o elemento DESENHA NADA, e o unico chamador nao tem plano B.
    #
    # Reproduzir isso nao e lacuna nossa, e o alvo. Entao uma cadeia com
    # primitiva derrubada NAO bloqueia. O que bloqueia e o contrario: uma
    # cadeia inteiramente dentro das seis, que o alvo desenha e nos ainda nao.
    # A `<mask>` DESENHA desde 2026-09-05: os filhos sao renderizados como um
    # documento proprio e reduzidos a `luminancia * alfa`, que e o que uma
    # mascara vale. O que continua bloqueando e so o que fica recusado por nome:
    # `maskUnits` fora de `userSpaceOnUse` (o padrao da spec e
    # `objectBoundingBox`, geometria diferente por referenciador), e um valor de
    # `mask` que nao seja `url(#id)`.
    #
    # `maskUnits`/`maskContentUnits`/`mask-type` NAO casam aqui: o `\s*[=:]` logo
    # depois de `mask` exclui as tres, e cada uma esta nos casos testados.
    "máscara em bounding box": re.compile(
        r'maskUnits\s*=\s*["\']?\s*(?!userSpaceOnUse)\w', re.I),
    "máscara que nao e url(#)": re.compile(
        r'mask\s*[=:]\s*(?:"(?!url\(#|none")'
        r"|'(?!url\(#|none')"
        r'|(?!["\']|url\(#|none\b)\S)', re.I),
    # O `clip-path` SAIU DAQUI EM 2026-09-05, e so o que continua recusado
    # bloqueia. `url(#id)` com `clipPathUnits` no padrao e desenhado: a regiao
    # vira mascara de cobertura e a interseccao e por pixel, entao aplicar o clip
    # de um grupo em cada forma da exatamente o mesmo que clipar o grupo composto
    # -- ao contrario do `opacity`, aqui nao ha aproximacao.
    #
    # O que sobra: `objectBoundingBox`, que re-escala a regiao pela caixa de cada
    # referenciador (uma definicao, geometrias diferentes), e as formas basicas
    # de CSS (`inset()`, `circle()`, ...), que sao outra linguagem.
    "clip-path em bounding box": re.compile(
        r'clipPathUnits\s*=\s*["\']?\s*objectBoundingBox', re.I),
    # A ASPA E TRATADA EXPLICITAMENTE, e a primeira versao desta linha nao
    # tratava: com `["\']?` opcional o motor RETROCEDE, deixa de consumir a
    # aspa, e o lookahead passa a olhar `"` em vez de `url(#`. Ela acusava
    # entao exatamente o unico clip que este leitor desenha.
    "clip-path que nao e url(#)": re.compile(
        r'clip-path\s*[=:]\s*(?:"(?!url\(#|none")'
        r"|'(?!url\(#|none')"
        r'|(?!["\']|url\(#|none\b)\S)', re.I),
    # O PATTERN, O `<use>` E O RASTER EMBUTIDO SAIRAM DAQUI EM 2026-09-09, e os
    # tres juntos porque sao UM construto: o idioma com que o Figma exporta
    # "esta forma e preenchida por uma imagem" -- `fill="url(#p)"`, o pattern
    # com um `<use>`, e o `<use>` apontando para uma `<image>` com um
    # `data:image/png;base64`. Contar como tres era contar um problema tres
    # vezes.
    #
    # `[BIN]` E o alvo desenha os tres: `SVGPattern::draw` (0x12184),
    # `drawCells` (0x11D50), `ConvertUseElementCoordinates` (0x22AC), chegando a
    # `CGPatternCreate`. Aqui a string nao enganou, ao contrario do filtro.
    #
    # O que continua bloqueando esta em `pattern_blockers`, porque depende de
    # QUAL pattern a referencia resolve -- uma regex nao responde por isso.
}


# `[BIN]` Os dez modos que o formato sabe soletrar (doc 01 §6), e os dez estao
# transcritos. Uma grafia fora desta lista nao pode vir de um `.icon` valido, e
# se vier e lacuna e nao `normal`.
KNOWN_BLENDS = {
    "normal", "plus-lighter", "plus-darker", "overlay", "multiply",
    "soft-light", "hard-light", "darken", "lighten", "screen",
}


# `[BIN]` As seis primitivas que o alvo constroi, na ordem da tabela
# `__const:0x327F0` de CoreSVG.arm64: {0x67, 0x5f, 0x5b, 0x72, 0x65, 0x78}.
# A ordem e mantida porque ela e a prova -- ordenar tornaria a transcricao
# infalsificavel contra os bytes de onde veio.
TARGET_FILTER_PRIMITIVES = (
    "feGaussianBlur", "feOffset", "feFlood", "feComposite", "feBlend",
    "feConvolveMatrix",
)

FILTER_DEF = re.compile(r"<filter\b([^>]*)>(.*?)</filter>", re.S | re.I)
FILTER_REF = re.compile(r"""filter\s*[=:]\s*["']?\s*url\(\s*#([^)\s"']+)""", re.I)
FILTER_PRIM = re.compile(r"<\s*(fe[A-Za-z]+)", re.I)
ELEMENT_ID = re.compile(r"""\bid\s*=\s*["']([^"']+)""", re.I)


PATTERN_DEF = re.compile(r"<pattern\b([^>]*)>(.*?)</pattern>", re.S | re.I)
PATTERN_REF = re.compile(r"""(?:fill|stroke)\s*[=:]\s*["']?\s*url\(\s*#([^)\s"']+)""", re.I)
USE_IN = re.compile(r"<\s*use\b([^>]*)", re.I)
HREF = re.compile(r"""(?:xlink:)?href\s*=\s*["']#([^"']+)""", re.I)
IMAGE_DEF = re.compile(r"<image\b([^>]*)", re.I)
DATA_PNG = re.compile(r"""(?:xlink:)?href\s*=\s*["']data:image/png;base64,""", re.I)
BARE_USE = re.compile(r"<\s*use\b", re.I)


def pattern_blockers(text: str) -> set:
    """O que as referencias de `<pattern>` deste SVG bloqueiam.

    O IDIOMA DO FIGMA DESENHA DESDE 2026-09-09: um `<pattern>` em unidades de
    caixa cujo conteudo e UM `<use>` apontando para uma `<image>` com um
    `data:image/png;base64`. O que sobra bloqueando e o que o leitor recusa por
    nome, e cada recusa e NOSSA -- o alvo desenha todas elas.

      patternTransform / patternUnits=userSpaceOnUse   geometria que o corpus
                                                       nao exercita e que este
                                                       leitor nao inventa
      conteudo que nao e um unico `<use>`
      `<image>` que nao e um `data:image/png;base64`
      referencia que nao resolve para pattern nem gradiente

    E um `<use>` FORA de um pattern continua bloqueando: o `<use>` que este
    leitor le e so o que mora dentro de um, e um solto na tela desenha algo que
    ninguem coloca.
    """
    defs = {}
    for m in PATTERN_DEF.finditer(text):
        ident = ELEMENT_ID.search(m.group(1))
        if not ident:
            continue
        defs[ident.group(1)] = (m.group(1), m.group(2))

    images = {}
    for m in IMAGE_DEF.finditer(text):
        ident = ELEMENT_ID.search(m.group(1))
        if ident:
            images[ident.group(1)] = bool(DATA_PNG.search(m.group(1)))

    bad = set()
    used_in_patterns = 0
    for ref in PATTERN_REF.findall(text):
        head_body = defs.get(ref)
        if head_body is None:
            continue  # gradiente, ou uma referencia que outra regra ja julga
        head, body = head_body
        if re.search(r"patternTransform\s*=", head, re.I):
            bad.add("pattern")
            continue
        if re.search(r"""patternUnits\s*=\s*["']?\s*userSpaceOnUse""", head, re.I):
            bad.add("pattern")
            continue
        uses = USE_IN.findall(body)
        if len(uses) != 1:
            bad.add("pattern")
            continue
        used_in_patterns += 1
        href = HREF.search(uses[0])
        if not href:
            bad.add("pattern")
            continue
        readable = images.get(href.group(1))
        if readable is None:
            bad.add("pattern")
        elif not readable:
            bad.add("raster embutido")

    # Um `<use>` que nao esta dentro de um pattern nao e lido por ninguem aqui.
    if len(BARE_USE.findall(text)) > used_in_patterns:
        bad.add("use")
    return bad


def filter_blockers(text: str) -> set:
    """O que as referencias de `<filter>` deste SVG bloqueiam.

    TRES RESPOSTAS, E SO A DO MEIO E UM BLOQUEIO:

      cadeia com primitiva fora das seis   o alvo derruba a primitiva na
                                           construcao e o elemento acaba
                                           desenhando NADA. Nos fazemos o mesmo
                                           desde 2026-09-09, entao e uma
                                           REPRODUCAO -- nao bloqueia.
      cadeia inteiramente dentro das seis  o alvo desenha de verdade, e ai sim
                                           falta pixel nosso. BLOQUEIA.
      referencia que nao resolve           ninguem mediu o que o alvo faz com
                                           ela, e o corpus nao tem nenhuma.
                                           Nomeada, nao adivinhada.
    """
    defs = {}
    for m in FILTER_DEF.finditer(text):
        ident = ELEMENT_ID.search(m.group(1))
        if not ident:
            continue
        defs[ident.group(1)] = [x.group(1) for x in FILTER_PRIM.finditer(m.group(2))]
    bad = set()
    for ref in FILTER_REF.findall(text):
        prims = defs.get(ref)
        if prims is None:
            bad.add("filtro sem definicao")
            continue
        # `feFlood`, `feBlend` e `feGaussianBlur` estao transcritos desde
        # 2026-09-09 (`Source/RenderBox/SvgFilter.cpp`). As outras tres das seis
        # -- `feOffset`, `feComposite`, `feConvolveMatrix` -- o alvo desenha e
        # nos nao, entao uma cadeia que use qualquer uma DELAS ainda bloqueia.
        TRANSCRITAS = ("feFlood", "feBlend", "feGaussianBlur")
        if all(p in TARGET_FILTER_PRIMITIVES for p in prims):
            if not all(p in TRANSCRITAS for p in prims):
                bad.add("filtro")
    return bad


def group_blend_over_glass(groups) -> set:
    """Grupos que mesclam E cujo vidro REFRATA -- e so esses.

    A regua contava aqui todo grupo que mesclasse e tivesse uma camada de vidro,
    qualquer que fosse. Contava demais: o acoplamento que bloqueia e o do nosso
    `glassOver`, que desloca o buffer de acumulacao NO LUGAR e portanto chega
    vazio no alvo proprio de um grupo -- e ele so roda quando a refracao move
    alguma coisa. Vidro com refracao identidade desenha como arte comum e nao
    prende o grupo a nada.

    `[ART]` No corpus, 5 dos 271 grupos carregam `refractivity` e 2 tem forca
    nao-zero, entao a diferenca entre as duas contas nao e pequena.
    """
    bad = set()
    for g in groups or []:
        modes = set()
        for k, v in g.items():
            if k == "blend-mode" and isinstance(v, str):
                modes.add(v)
            if k == "blend-mode-specializations":
                for e in (v or []):
                    if isinstance(e, dict) and isinstance(e.get("value"), str):
                        modes.add(e["value"])
        if not (modes - {"normal"}):
            continue
        if not any(x is True for lay in (g.get("layers") or [])
                   for k, x in lay.items() if k == "glass"):
            continue
        # A refracao so e nao-identidade com `enabled` e forca diferente de zero;
        # o `strength` sozinho nao basta, e `enabled` sozinho tambem nao.
        for r in values_for(g, "refractivity"):
            if not isinstance(r, dict):
                continue
            if r.get("enabled") is True and r.get("strength") not in (None, 0, 0.0):
                bad.add("mescla de grupo sobre vidro que refrata")
                break
    return bad


def values_for(node, key: str):
    """Every value a property takes ANYWHERE under `node`.

    Both shapes, at every depth: the bare `key`, and each entry's `value` under
    `key + "-specializations"`. Every reachable value counts, because a slice
    that cannot draw a gradient cannot draw a document that asks for one in any
    context.
    """
    out = []

    def walk(n):
        if isinstance(n, dict):
            for k, v in n.items():
                if k == key:
                    out.append(v)
                elif k == key + SUFFIX and isinstance(v, list):
                    for entry in v:
                        if isinstance(entry, dict) and "value" in entry:
                            out.append(entry["value"])
                else:
                    walk(v)
        elif isinstance(n, list):
            for v in n:
                walk(v)

    walk(node)
    return out


def fill_kinds(value) -> list[str]:
    """The kinds a `fill` value names, in either of its two shapes."""
    if isinstance(value, str):
        return [value]
    if isinstance(value, dict):
        return [k for k in value if k not in FILL_MODIFIERS]
    return []


def group_refraction_strength(group) -> float:
    """The largest |strength| any resolvable `refractivity` on this GROUP names.

    Group-level only, and deliberately: `[ART]` all 280 occurrences of `glass`
    are inside layers and none of them carries a number, while `refractivity`
    occurs only on groups. `[OBS]` The `enabled` bit is NOT consulted, which is
    what `glassMaterialFrom` does too -- where that bit collapses was never read,
    and 42 corpus groups are disabled with a non-zero value, so honouring it and
    ignoring it are different behaviours and nothing says which the target does.
    """
    values = []
    if "refractivity" in group:
        values.append(group["refractivity"])
    for entry in (group.get("refractivity" + SUFFIX) or []):
        if isinstance(entry, dict) and "value" in entry:
            values.append(entry["value"])
    worst = 0.0
    for v in values:
        if isinstance(v, dict) and isinstance(v.get("strength"), (int, float)):
            worst = max(worst, abs(float(v["strength"])))
    return worst


def glass_raster_names(groups) -> set[str]:
    """Art named by a glass layer whose group actually refracts, and is a raster.

    This is the one glass blocker left. It is computed per DOCUMENT rather than
    inside `blockers_of` because the parameter is on the group and the bit is on
    the layer, and `blockers_of` is handed one or the other -- pairing them here
    keeps the layer ruler and the document ruler answering the same question.
    """
    blocked: set[str] = set()
    for g in groups:
        if group_refraction_strength(g) == 0.0:
            continue
        for lay in (g.get("layers") or []):
            if not any(v is True for v in values_for(lay, "glass")):
                continue
            for name in values_for(lay, "image-name"):
                if isinstance(name, str) and Path(name).suffix.lower() != ".svg":
                    blocked.add(name)
    return blocked


def unconsumed_material(group) -> bool:
    """Does this group ask for glass material this renderer does not apply?

    `[OBS]` The three below have no consumer read from the binary, so the
    renderer carries them and applies none. Anything else about the layer may be
    perfect and it still will not be the target's pixel.
    """
    for v in values_for(group, "translucency"):
        if isinstance(v, dict) and v.get("enabled") is True:
            return True
    for v in values_for(group, "shadow"):
        if isinstance(v, dict) and v.get("kind") not in (None, "none"):
            return True
    for v in values_for(group, "specular"):
        if v is True or isinstance(v, str):
            return True
    return False


def blockers_of(node, assets: Path, svg_cache: dict,
                glass_raster: set[str] | None = None) -> tuple[set[str], int]:
    """What stops `node` from being drawn, and how many names do not resolve."""
    bad: set[str] = set()
    dangling = 0
    glass_raster = glass_raster or set()

    for v in values_for(node, "fill"):
        for k in fill_kinds(v):
            if k not in DRAWN_FILLS:
                bad.add("fill de camada: %s" % k)
    # A MESCLA SAIU DAQUI EM 2026-09-04, com uma excecao medida. Os dez modos
    # que o formato sabe soletrar estao todos transcritos (doc 03 §32), o
    # compositor mescla por camada, e um grupo mesclado ganha alvo proprio.
    #
    # A excecao e o grupo que mescla E carrega vidro: o vidro refrata o fundo, e
    # com alvo proprio esse fundo esta vazio. Qual dos dois o alvo faz nao foi
    # lido. Isso NAO se decide olhando uma chave: precisa do grupo inteiro, e
    # por isso e calculado em `group_blend_over_glass` e unido aqui pelo
    # chamador em vez de sair deste laco.
    for v in values_for(node, "blend-mode"):
        if isinstance(v, str) and v != "normal" and v not in KNOWN_BLENDS:
            bad.add("mescla nao soletravel: %s" % v)

    for name in dict.fromkeys(v for v in values_for(node, "image-name")
                              if isinstance(v, str)):
        if name in glass_raster:
            # The glass draws; what is missing is a field generator that reads a
            # raster's ALPHA instead of a contour. Its own sentence, because
            # folding it into the retired "glass is not transcribed" would hide
            # the fact that the glass now draws.
            bad.add("vidro sobre raster")
        f = assets / name
        if not f.is_file():
            dangling += 1
            bad.add("referência pendurada")
            continue
        # Raster is no longer a blocker: the compositor decodes PNG and places
        # it (doc 03 §21, §22). Any OTHER extension still is.
        if f.suffix.lower() == ".png":
            continue
        if f.suffix.lower() != ".svg":
            bad.add("arte com extensao nao lida (%s)" % f.suffix.lower())
            continue
        if f not in svg_cache:
            text = f.read_text(encoding="utf-8", errors="replace")
            svg_cache[f] = [n for n, rx in SVG_BLOCKERS.items() if rx.search(text)]
            svg_cache[f] += sorted(filter_blockers(text))
            svg_cache[f] += sorted(pattern_blockers(text))
        bad.update(svg_cache[f])
    return bad, dangling


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "References/corpus")
    bundles = sorted(p for p in root.iterdir() if (p / "icon.json").is_file())
    if not bundles:
        raise SystemExit("no bundle with an icon.json under %s" % root)

    judged = full_ok = docs_ok = art_absent = dangling = 0
    layers_total = layers_flat = 0
    # A layer can be DRAWABLE and still not be the target's pixel. `glass` on a
    # layer makes its group's material apply to it, and three of that material's
    # fields have NO KNOWN CONSUMER in the binary -- `translucency`,
    # `shadowOpacity`/`shadowStyle` and `hasSpecular`/`specularPlacement` cross
    # Swift/ObjC verbatim with no arithmetic and no Max/Power partner (doc 03
    # §29). This renderer therefore draws such a layer without them.
    #
    # That is NOT a blocker: nothing is drawn wrong, and the refraction that IS
    # decoded runs correctly. But "no named blocker" and "the target's pixel" are
    # different claims, and a ruler that reports only the first oversells. So the
    # second number is counted and printed beside it.
    glass_incomplete = 0
    doc_blockers = Counter()
    layer_blockers = Counter()
    asset_kinds = Counter()
    bg_fill = Counter()
    all_blends = Counter()
    svg_cache: dict[Path, list[str]] = {}
    in_slice: list[str] = []

    for b in bundles:
        doc = json.loads(io.open(b / "icon.json", encoding="utf-8").read())
        groups = doc.get("groups") or []

        bgf: set[str] = set()
        for v in values_for({"fill": doc.get("fill")}, "fill"):
            bgf.update(fill_kinds(v))
        for v in (doc.get("fill" + SUFFIX) or []):
            if isinstance(v, dict):
                bgf.update(fill_kinds(v.get("value")))
        bg_fill.update(bgf)

        for v in values_for(groups, "blend-mode"):
            if isinstance(v, str):
                all_blends[v] += 1

        if not (b / "Assets").is_dir():
            art_absent += 1
            continue
        judged += 1
        assets = b / "Assets"

        for name in dict.fromkeys(v for v in values_for(groups, "image-name")
                                  if isinstance(v, str)):
            f = assets / name
            if f.is_file():
                asset_kinds[f.suffix.lower() or "(sem extensão)"] += 1

        blocked_art = glass_raster_names(groups)

        for g in groups:
            glassy = group_blend_over_glass([g])
            for lay in (g.get("layers") or []):
                bad, _ = blockers_of(lay, assets, svg_cache, blocked_art)
                bad |= glassy
                layers_total += 1
                if bad:
                    layer_blockers.update(bad)
                else:
                    layers_flat += 1
                    if any(v is True for v in values_for(lay, "glass")):
                        if unconsumed_material(g):
                            glass_incomplete += 1

        why, d = blockers_of(groups, assets, svg_cache, blocked_art)
        why |= group_blend_over_glass(groups)
        dangling += d
        if why:
            doc_blockers.update(why)
        else:
            docs_ok += 1
            in_slice.append(b.name)
        # The BACKGROUND is drawn now too, so this is no longer "the layers
        # work and the fill is anyone's guess". `[OBS]` It is still not the
        # target's pixel: the chiclet's corner geometry and the rect a
        # chiclet-aligned system fill uses were not read -- see the header.
        if not (why | (bgf - DRAWN_FILLS)):
            full_ok += 1

    n = len(bundles)
    print("%d documentos, %d com arte no corpus (os outros %d vieram só com o "
          "icon.json)\n" % (n, judged, art_absent))

    print("ALCANCE POR CAMADA  — a régua que decide")
    print("  %d de %d camadas desenháveis hoje   %5.1f%%"
          % (layers_flat, layers_total, 100.0 * layers_flat / max(layers_total, 1)))
    if glass_incomplete:
        print("  destas, %d desenham SEM o material que o documento pede"
              % glass_incomplete)
        print("      translucency, sombra e especular do grupo não têm consumidor")
        print("      lido no binário (doc 03 §29), então nenhum é aplicado.")
        print("      Desenhável não é o mesmo que igual ao alvo.")

    print("\nALCANCE POR DOCUMENTO  (sobre os %d que dá para julgar)" % judged)
    print("  camadas  %3d/%d  %5.1f%%   o documento inteiro, fundo de fora"
          % (docs_ok, judged, 100.0 * docs_ok / max(judged, 1)))
    print("  completo %3d/%d  %5.1f%%   fundo e camadas"
          % (full_ok, judged, 100.0 * full_ok / max(judged, 1)))
    if in_slice:
        print("\n  dentro da fatia: %s" % ", ".join(in_slice))

    print("\nO QUE BLOQUEIA, POR CAMADA  (uma camada conta em várias)")
    for name, c in layer_blockers.most_common():
        print("  %-30s %4d  %5.1f%%" % (name, c, 100.0 * c / max(layers_total, 1)))

    print("\nO QUE BLOQUEIA, POR DOCUMENTO")
    for name, c in doc_blockers.most_common():
        print("  %-30s %4d  %5.1f%%" % (name, c, 100.0 * c / max(judged, 1)))

    print("\nO FUNDO (`fill` de topo), sobre os %d documentos" % n)
    for k, c in bg_fill.most_common():
        print("  %-30s %4d  %5.1f%%" % (k, c, 100.0 * c / n))

    print("\nOS ASSETS DOS %d JULGADOS, por extensão" % judged)
    for k, c in asset_kinds.most_common():
        print("  %-30s %4d" % (k, c))
    print("  %-30s %4d" % ("referências penduradas", dangling))

    print("\nAS MESCLAS DE CAMADA, sobre os %d documentos" % n)
    for k, c in all_blends.most_common():
        print("  %-30s %4d" % (k, c))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
