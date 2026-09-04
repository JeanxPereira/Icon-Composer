#!/usr/bin/env python3
"""Every corpus SVG, ours against the independent oracle.

`svg-oracle.py` settles ONE file. This runs it over the corpus and reports four
columns: agreed, differed, what the ORACLE refused, and what WE refused.

The refusals are the honest part, and there are TWO kinds. An oracle that
filled a stroked path, or guessed at an arc, would report a difference that is
its own blind spot. And OUR renderer names its own gaps on stderr -- a gradient
whose stops arrive through an `xlink:href` it does not follow, say -- and a file
where we deliberately drew nothing is not a file where we disagree.

Both are counted apart and neither is mixed into the verdict. Only the DIFFER
column says anything about either implementation.

    python scripts/svg-differential.py [--size 128] [--limit N]
"""
import argparse
import io
import os
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORPUS = Path(os.environ.get('IC_CORPUS_DIR', ROOT / 'References' / 'corpus'))
ICRENDER = ROOT / 'build' / 'mingw' / 'Source' / 'cli' / 'icrender.exe'
ORACLE = ROOT / 'scripts' / 'svg-oracle.py'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--size', type=int, default=128)
    ap.add_argument('--limit', type=int, default=0)
    args = ap.parse_args()

    if not ICRENDER.is_file():
        raise SystemExit('build icrender first: %s' % ICRENDER)

    svgs = sorted(CORPUS.glob('*/Assets/*.svg'))
    if args.limit:
        svgs = svgs[:args.limit]

    tmp = Path(tempfile.mkdtemp(prefix='svgdiff-'))
    agreed = differed = 0
    refused = Counter()
    ours_refused = Counter()
    worst = []

    for n, svg in enumerate(svgs, 1):
        png = tmp / ('%04d.png' % n)
        r = subprocess.run([str(ICRENDER), str(svg), '--out', str(png),
                            '--size', str(args.size)],
                           capture_output=True, text=True)
        if not png.is_file():
            ours_refused['nao produziu imagem'] += 1
            continue
        # A shape WE skipped, with a reason, is our named gap -- not a
        # disagreement. Comparing against a picture we deliberately left
        # incomplete would blame the oracle for our own honesty.
        #
        # A NOTE IS NOT A REFUSAL. `display-p3 desenhado SEM conversao` says the
        # shape WAS drawn and its colour space was not converted -- and this
        # compares ALPHA, which a colour space cannot touch. Counting it as a
        # refusal took 16 files out of the comparison for nothing, which is the
        # same over-refusal the oracle had to be cured of.
        lines = [ln.strip() for ln in (r.stderr or '').strip().splitlines() if ln.strip()]
        undrawn = [ln for ln in lines
                   if 'nao desenhado' in ln
                   or ('shape ' in ln and 'desenhado SEM conversao' not in ln)]
        if undrawn:
            ours_refused[undrawn[0][:58]] += 1
            continue
        o = subprocess.run([sys.executable, str(ORACLE), str(svg),
                            '--size', str(args.size), '--against', str(png)],
                           capture_output=True, text=True)
        out = (o.stdout or '') + (o.stderr or '')
        if 'REFUSED' in out:
            why = out.split('REFUSED:')[1].split('.')[0].strip()
            refused[why[:60]] += 1
            continue
        big = 0
        for line in out.splitlines():
            if 'diferenca > 0.5' in line:
                big = int(line.split(':')[1].split('de')[0].strip())
            if 'maior diferenca' in line:
                m = float(line.split(':')[1].split('em')[0].strip())
        if big:
            differed += 1
            worst.append((big, str(svg.relative_to(CORPUS))))
        else:
            agreed += 1
        if n % 20 == 0:
            print('  ... %d/%d' % (n, len(svgs)), flush=True)

    print('\n%d SVGs do corpus, a %d px' % (len(svgs), args.size))
    print('  CONCORDAM (nenhum pixel > 0,5 de diferenca): %d' % agreed)
    print('  DIFEREM:                                    %d' % differed)
    print('  o oraculo RECUSOU (lacuna dele, nomeada):   %d' % sum(refused.values()))
    for why, k in refused.most_common():
        print('      %-58s %d' % (why, k))
    print('  o NOSSO recusou, ja nomeando o motivo:       %d' % sum(ours_refused.values()))
    for why, k in ours_refused.most_common():
        print('      %-58s %d' % (why, k))
    if worst:
        print('\n  os que diferem, por quantidade de pixels:')
        for k, name in sorted(worst, reverse=True)[:15]:
            print('      %6d  %s' % (k, name))
    return 1 if differed else 0


if __name__ == '__main__':
    sys.exit(main())
