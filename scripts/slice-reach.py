#!/usr/bin/env python3
"""How far a flat-fill renderer reaches into the corpus.

WHY THIS RUNS BEFORE THE CODE
-----------------------------
Every piece of the flat-fill path passes its own gate -- path buffer, vertex,
coverage, resolve, flat composite, render target -- and nothing connects them.
Before writing the connector, this measures what that connector would actually
be able to draw, so the target after it is chosen by the corpus and not by a
guess.

WHAT COUNTS AS "IN THE SLICE"
-----------------------------
A layer is in the slice when it is drawable with what is gated today: a `solid`
fill, `normal` blend, no glass, and art the renderer can place -- a PNG, or
vector art the CoreSVG reader turns into filled paths, flat or gradient-filled,
with no stroke, filter, mask, clip path, pattern, `use` or embedded raster.

THREE THINGS HAVE LEFT THE BLOCKER LIST: RASTER and the SVG's `url(#id)`
GRADIENT paint, both on 2026-09-01, and on 2026-09-02 the GLASS LAYER. The
number this script prints is therefore not comparable across any of them --
said here rather than letting a rising number look like the corpus changed.

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
FLAT_FILLS = {"solid", "none"}

# What the CoreSVG reader turns into filled paths today, and what it does not.
SVG_BLOCKERS = {
    "traço pintado": re.compile(r'stroke\s*[=:]\s*["\']?\s*(?!none)[#a-z0-9(]', re.I),
    "filtro": re.compile(r"<\s*filter[\s>]|filter\s*[=:]", re.I),
    "máscara": re.compile(r"<\s*mask[\s>]|mask\s*[=:]", re.I),
    "clip-path": re.compile(r"clip-path\s*[=:]", re.I),
    "pattern": re.compile(r"<\s*pattern[\s>]", re.I),
    "use": re.compile(r"<\s*use[\s>]", re.I),
    "raster embutido": re.compile(r"<\s*image[\s>]", re.I),
}


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
            if k not in FLAT_FILLS:
                bad.add("fill de camada: %s" % k)
    for v in values_for(node, "blend-mode"):
        if isinstance(v, str) and v != "normal":
            bad.add("mescla: %s" % v)

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
            for lay in (g.get("layers") or []):
                bad, _ = blockers_of(lay, assets, svg_cache, blocked_art)
                layers_total += 1
                if bad:
                    layer_blockers.update(bad)
                else:
                    layers_flat += 1
                    if any(v is True for v in values_for(lay, "glass")):
                        if unconsumed_material(g):
                            glass_incomplete += 1

        why, d = blockers_of(groups, assets, svg_cache, blocked_art)
        dangling += d
        if why:
            doc_blockers.update(why)
        else:
            docs_ok += 1
            in_slice.append(b.name)
        if not (why | (bgf - FLAT_FILLS)):
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
