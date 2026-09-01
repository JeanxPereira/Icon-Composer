#!/usr/bin/env python3
"""Collects real `.icon` documents from public repositories into References/corpus.

Why this exists: the `.icon` KEYS are settled -- they were read out of
`IconComposerFoundation`'s Swift reflection metadata (doc 01). What is NOT
settled is the grammar of the VALUES: `fill` takes the string `"none"` and the
object `{"solid": "srgb:…"}` in the same file, and one artifact cannot say what
the full set of shapes is. A schema read from a binary plus a single example is
a hypothesis; a schema plus a few dozen independent examples, written by
different people through the real app, is evidence.

There is no public corpus of `.icon` files, but there is an incidental one:
applications that adopted Icon Composer commit the document to their own
repository, next to their source. This finds them.

Provenance. Every file lands beside the repository, path, commit SHA and license
it came from, in `corpus.json`. The corpus is third-party material and follows
the same rule as the rest of References/: it never enters git. Nothing here is
redistributed -- the manifest is what makes the collection reproducible.

A `.icon` is a FOLDER, measured over 45 bundles: `icon.json` beside a flat
`Assets/` holding the SVGs and PNGs the layers name. `--with-assets N` pulls the
whole bundle for the first N documents, which is what lets a gate check the
reference the other way -- that every `image-name` names a file that is there.

Usage:
    python scripts/fetch-icon-corpus.py                  # collect, skip what exists
    python scripts/fetch-icon-corpus.py --limit 40
    python scripts/fetch-icon-corpus.py --with-assets 40 # whole bundles
    python scripts/fetch-icon-corpus.py --list           # what the manifest holds
    python scripts/fetch-icon-corpus.py --out DIR
"""
from __future__ import annotations

import argparse
import base64
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_OUT = Path(__file__).resolve().parent.parent / "References" / "corpus"

# Several shapes, because GitHub's code search returns a different slice for
# each and the union is wider than any one of them. Keys that only a real `.icon`
# carries -- a false positive would have to be a hand-written imitation.
QUERIES = [
    '"fill-specializations"',
    '"image-name" "blend-mode"',
    '"supported-platforms" "squares"',
    '"translation-in-points"',
    '"hidden-specializations"',
    '"specular" "translucency" "refractivity"',
]


def gh(args: list[str], quiet: bool = False) -> str:
    r = subprocess.run(["gh"] + args, capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        if not quiet:
            print(f"  gh {' '.join(args[:2])}: {r.stderr.strip()[:160]}", file=sys.stderr)
        return ""
    return r.stdout


def search(limit_per_query: int) -> dict[tuple[str, str], None]:
    """{(repo, path)} of candidate icon.json files."""
    found: dict[tuple[str, str], None] = {}
    for q in QUERIES:
        out = gh(["search", "code", "--limit", str(limit_per_query),
                  "--filename", "icon.json", q])
        n = 0
        for line in out.splitlines():
            # "owner/repo:path/to/icon.json: <matched line>"
            m = re.match(r"^([^:]+/[^:]+):([^:]+icon\.json):", line)
            if not m:
                continue
            repo, path = m.group(1), m.group(2)
            if ".icon/" not in path:
                continue          # the document always lives inside a `*.icon` bundle
            if (repo, path) not in found:
                found[(repo, path)] = None
                n += 1
        print(f"  {q:<52} +{n}")
    return found


def license_of(repo: str, cache: dict) -> str:
    if repo in cache:
        return cache[repo]
    out = gh(["api", f"repos/{repo}", "--jq", ".license.spdx_id // \"UNKNOWN\""], quiet=True)
    cache[repo] = out.strip() or "UNKNOWN"
    return cache[repo]


def slug(repo: str, path: str) -> str:
    owner, name = repo.split("/", 1)
    bundle = next((p for p in path.split("/") if p.endswith(".icon")), "icon")
    return re.sub(r"[^A-Za-z0-9._-]", "_", f"{owner}__{name}__{bundle[:-5]}")


def fetch_assets(out: Path, manifest: dict, limit: int) -> int:
    """Pull `Assets/` for documents the manifest already holds.

    Ordered by what EXERCISES the reader, not alphabetically. A first run took
    the first forty by name and got forty bundles in which no layer specializes
    its `image-name` -- so the gate could not tell a collector that walks every
    (appearance, idiom) context from one that only ever looks at the base. The
    documents that do specialize an image come first now.
    """
    def interesting(item):
        key, _rec = item
        doc = out / key / "icon.json"
        try:
            return (0 if "image-name-specializations" in doc.read_text(encoding="utf-8") else 1, key)
        except OSError:
            return (1, key)

    done = added = skipped = 0
    for key, rec in sorted(manifest.items(), key=interesting):
        if done >= limit:
            break
        done += 1
        dest = out / key / "Assets"
        listing = gh(["api", f"repos/{rec['repo']}/contents/{Path(rec['path']).parent.as_posix()}/Assets",
                      "--jq", r'.[] | select(.type=="file") | "\(.name)\t\(.download_url)"'], quiet=True)
        if not listing.strip():
            continue
        dest.mkdir(parents=True, exist_ok=True)
        n = 0
        for line in listing.strip().splitlines():
            name, url = line.split("\t", 1)
            target = dest / name
            if target.exists():
                skipped += 1
                continue
            blob = subprocess.run(["curl", "-sSL", url], capture_output=True)
            if blob.returncode != 0 or not blob.stdout:
                continue
            target.write_bytes(blob.stdout)
            n += 1
            added += 1
        rec["assets"] = sorted(p.name for p in dest.iterdir() if p.is_file())
        if n:
            print(f"  + {key:<44} {n} asset(s)")
    (out / "corpus.json").write_text(json.dumps(
        {"documents": dict(sorted(manifest.items()))}, indent=2) + "\n", encoding="utf-8")
    print(f"\nassets: +{added} new, {skipped} already there, over {done} bundle(s)")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--limit", type=int, default=60,
                    help="max hits per query shape (default 60)")
    ap.add_argument("--list", action="store_true", help="print the manifest and stop")
    ap.add_argument("--with-assets", type=int, default=0, metavar="N",
                    help="also fetch Assets/ for the first N documents already collected")
    args = ap.parse_args(argv)

    manifest_path = args.out / "corpus.json"
    manifest = {}
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8")).get("documents", {})

    if args.list:
        print(f"{args.out}: {len(manifest)} documents")
        for k, v in sorted(manifest.items()):
            print(f"  {v['bytes']:>7,}  {k:<44} {v['repo']}  [{v['license']}]")
        return 0

    if args.with_assets:
        return fetch_assets(args.out, manifest, args.with_assets)

    if not gh(["--version"], quiet=True):
        print("error: the GitHub CLI (`gh`) is required and must be authenticated",
              file=sys.stderr)
        return 2

    print("searching:")
    hits = search(args.limit)
    print(f"  {len(hits)} distinct (repo, path)\n")

    args.out.mkdir(parents=True, exist_ok=True)
    licenses: dict = {}
    added = skipped = failed = 0
    for (repo, path) in sorted(hits):
        key = slug(repo, path)
        dest = args.out / key / "icon.json"
        if dest.exists() and key in manifest:
            skipped += 1
            continue
        raw = gh(["api", f"repos/{repo}/contents/{path}", "--jq", ".content"], quiet=True)
        if not raw.strip():
            failed += 1
            continue
        try:
            data = base64.b64decode(raw)
            json.loads(data)          # a corpus entry that is not JSON is not a corpus entry
        except Exception as e:
            print(f"  reject {repo}:{path} -- {e}", file=sys.stderr)
            failed += 1
            continue
        sha = gh(["api", f"repos/{repo}/contents/{path}", "--jq", ".sha"], quiet=True).strip()
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        manifest[key] = {
            "repo": repo,
            "path": path,
            "sha": sha,
            "bytes": len(data),
            "license": license_of(repo, licenses),
            "fetched": datetime.datetime.now(datetime.timezone.utc)
                       .replace(microsecond=0).isoformat(),
        }
        added += 1
        print(f"  + {key:<44} {len(data):>7,} bytes")

    manifest_path.write_text(json.dumps(
        {"documents": dict(sorted(manifest.items()))}, indent=2) + "\n", encoding="utf-8")
    print(f"\n{len(manifest)} documents in {args.out}"
          f"  (+{added} new, {skipped} already there, {failed} failed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
