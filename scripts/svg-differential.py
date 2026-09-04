#!/usr/bin/env python3
"""Every corpus SVG, ours against the independent oracle.

`svg-oracle.py` settles ONE file. This runs it over the corpus and reports three
numbers: how many agreed, how many the oracle REFUSED (its own gaps, named), and
how many DIFFERED -- which is the only column that says anything about us.

The refusals are the honest part. An oracle that filled a stroked path, or
guessed at an arc, would report a difference that is its own blind spot; this
counts those separately and never mixes them into the verdict.

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
    worst = []

    for n, svg in enumerate(svgs, 1):
        png = tmp / ('%04d.png' % n)
        r = subprocess.run([str(ICRENDER), str(svg), '--out', str(png),
                            '--size', str(args.size)],
                           capture_output=True, text=True)
        if not png.is_file():
            refused['o nosso nao desenhou'] += 1
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
    if worst:
        print('\n  os que diferem, por quantidade de pixels:')
        for k, name in sorted(worst, reverse=True)[:15]:
            print('      %6d  %s' % (k, name))
    return 1 if differed else 0


if __name__ == '__main__':
    sys.exit(main())
