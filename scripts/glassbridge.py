#!/usr/bin/env python3
"""The glass uniform bridge: which CIFilter key writes which byte of the struct.

WHY THIS EXISTS
---------------
`RB::Shader::Glass::BackgroundUniforms` is 256 bytes and 75 fields. The Metal IR
in `default_mod98.ll` gives every field's OFFSET, WIDTH and ARITHMETIC -- and no
field's NAME: these are `!air.visible` functions, not entry points, so there is
no `!air.buffer` node and the only names AIR keeps are the seven arguments.

`RenderBox.arm64` carries a contiguous run of 83 CIFilter-style keys at file
offset 0x16E7D8..0x16EF66 (`inputInnerRefractionAmount`, `inputBlurOpacity0`,
`inputRingShadowMask`, ...). Nothing about a matching NAME proves a matching
SLOT -- that is the same trap the blend modes set in doc 03 §15.3, where "the
only candidate in a set of one" got recorded as a reading.

What settles it is not the names: it is the CODE that fills the struct. One
function reads 75 of the 83 keys, and for each it calls a `(object, key,
default) -> double` helper and stores the result at a fixed stack offset. That
store IS the binding, and this tool reads it.

THE METHOD
----------
Follow the VALUE, not the order. A first version paired each key with the next
store off a stack, and the compiler interleaves: two keys landed on one offset
and one offset collected two keys. A collision is the signature of a wrong
pairing, not of a binary that overwrites its own field. So here the call's
result is tracked out of `d0` through moves, `fcvt` and arithmetic until a `str`
binds it, and every register the ABI says a call clobbers is killed at the call.

That kill is not a detail: without it a tracked value survives a call inside a
register the callee owns, and reappears bound to a slot it never touched.

THE POSITIVE CONTROL
--------------------
Two independent artefacts have to agree, and they were produced by different
teams for different purposes:

  * the ARM64 packer in `RenderBox.arm64` -- what this tool reads, and
  * the field layout in `default_mod98.ll` -- what the shader READS.

Every store this tool resolves must land inside the struct and must not STRADDLE
a field boundary the IR declares. A 4-byte store beginning at a half's offset,
or an 8-byte store crossing out of a `half4`, means the address arithmetic is
wrong -- and it is the exact failure that would otherwise produce a plausible,
confident, wrong table.

WHAT IT DOES NOT ANSWER
-----------------------
The three colour matrices (bytes 80..151) are NOT bound here, and the tool says
so rather than guessing. They are not stored key by key: `White`, `Black`,
`Saturation` and `FillColor` go through the YCC composite assembly, one key
feeding several matrix entries. That routine is transcribed on the QuartzCore
side (AquaKit `MakeYCCCompositeMatrix`); binding it here is separate work.

USAGE
    python glassbridge.py <thin mach-o>              # the background struct
    python glassbridge.py <thin mach-o> --foreground
    python glassbridge.py <thin mach-o> --raw        # every store, unfiltered
"""
from __future__ import annotations

import argparse
import collections
import struct
import sys
from pathlib import Path

import capstone

# --- the two things read from the file, not assumed ------------------------
TEXT_FILEOFF = 0x1338
TEXT_VMADDR = 0x1338
CFSTRING_FILEOFF = 0x190660
CFSTRING_SIZE = 0x3BC0

# `[BIN]` The key run, read from __TEXT,__cstring. 83 strings, and the 83
# CFString objects that wrap them are CONTIGUOUS and in the same order.
KEY_LO, KEY_HI = 0x16E7D8, 0x16EF80

# `[BIN]` The two packers, found by counting how many of the 83 keys each
# function references: 75 in one, 12 in the other, 1 stray. Not hand-picked.
BACKGROUND = (0x0E6D40, 1100, 0xC0, 256)
FOREGROUND = (0x0E7D48, 600, 0x68, 72)

# `[BIN]` The field layout of BackgroundUniforms, read from the LLVM struct type
# in default_mod98.ll and cross-checked against its TBAA member list. This is
# the INDEPENDENT artefact the control checks against -- it comes from the
# shader that READS the struct, never from the code that writes it.
BG_FIELDS: list[tuple[int, int]] = (
    [(0, 8), (8, 8)]                                   # 2 x [2 x float]
    + [(16 + 4 * i, 4) for i in range(12)]             # 12 x float
    + [(64, 8)]                                        # [2 x float]
    + [(72, 4), (76, 4)]                               # 2 x float
    + [(80 + 8 * i, 8) for i in range(9)]              # 9 x [4 x half]
    + [(152, 4), (156, 4)]                             # 2 x float
    + [(160 + 2 * i, 2) for i in range(12)]            # 12 x half
    + [(184, 4)]                                       # [2 x half]
    + [(188 + 2 * i, 2) for i in range(34)]            # 34 x half
)

# ForegroundUniforms: 72 bytes, 2 x [2 x float] then 14 floats.
FG_FIELDS: list[tuple[int, int]] = [(0, 8), (8, 8)] + [(16 + 4 * i, 4) for i in range(14)]

WIDTH = {"h": 2, "s": 4, "d": 8, "q": 16}
ARITH = {"fdiv", "fcsel", "fneg", "fmul", "fadd", "fsub",
         "fmax", "fmin", "fsqrt", "frinta", "fmadd", "fnmadd", "fmsub"}
# `[BIN]` v0-v7 are caller-saved in the AAPCS64 vector ABI.
CLOBBERED_BY_CALL = "01234567"


def load(path: Path) -> bytes:
    d = path.read_bytes()
    if struct.unpack_from("<I", d, 0)[0] != 0xFEEDFACF:
        raise SystemExit("not a thin 64-bit Mach-O: %s" % path)
    return d


def key_names(d: bytes) -> dict[int, str]:
    """CFString address -> the C string it wraps."""
    out: dict[int, str] = {}
    for i in range(CFSTRING_FILEOFF, CFSTRING_FILEOFF + CFSTRING_SIZE, 32):
        ptr = struct.unpack_from("<Q", d, i + 16)[0] & 0xFFFFFFFFF
        if KEY_LO <= ptr <= KEY_HI:
            out[i] = d[ptr:d.index(b"\0", ptr)].decode("ascii")
    return out


def vreg(name: str) -> str:
    return name.split(".")[0][1:]


def read_packer(d: bytes, cf: dict[int, str], start: int, count: int, base: int):
    """Every (offset, width, key, default, ops) this packer binds."""
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    lo = TEXT_FILEOFF + (start - TEXT_VMADDR)
    code = d[lo:lo + count * 4]

    page: dict[str, int] = {}
    xreg: dict[str, int] = {}
    hold: dict[str, tuple] = {}
    pending_key: str | None = None
    pending_default: float | None = None
    rows = []
    seen_instructions = 0

    for ins in md.disasm(code, start):
        seen_instructions += 1
        m, o = ins.mnemonic, ins.op_str
        a = [x.strip() for x in o.split(",")]

        if m == "adrp":
            page[a[0]] = int(a[1].lstrip("#"), 0)
        elif m == "add" and len(a) == 3 and a[1] in page and a[2].startswith("#"):
            v = page[a[1]] + int(a[2][1:], 0)
            page[a[0]] = v
            if v in cf:
                pending_key = cf[v]
        elif m == "mov" and len(a) == 2 and a[1].startswith("#"):
            xreg[a[0]] = int(a[1][1:], 0) & 0xFFFFFFFFFFFFFFFF
        elif m == "fmov" and len(a) == 2:
            if a[1] in xreg:
                pending_default = struct.unpack("<d", struct.pack("<Q", xreg[a[1]]))[0]
            elif a[1].startswith("#"):
                pending_default = float(a[1][1:])
            elif a[1][0] in "sdhq" and vreg(a[1]) in hold:
                hold[vreg(a[0])] = hold[vreg(a[1])]
            else:
                hold.pop(vreg(a[0]), None)
        elif m == "ldr" and a[0][0] in "sdhq" and "[" in o:
            parts = [x.strip(" ]") for x in o.split("[")[1].split(",")]
            if parts[0] in page:
                addr = page[parts[0]] + (int(parts[1].lstrip("#"), 0) if len(parts) > 1 else 0)
                if addr + 8 <= len(d):
                    pending_default = struct.unpack_from("<d", d, addr)[0]
            hold.pop(vreg(a[0]), None)
        elif m in ("ldp", "movi", "dup", "ins", "ucvtf", "scvtf"):
            for x in a[:2 if m == "ldp" else 1]:
                if x and x[0] in "sdhqv":
                    hold.pop(vreg(x), None)
        elif m == "bl":
            # Killing these is what keeps a value from surviving a call inside a
            # register the callee owns and reappearing on someone else's slot.
            for r in CLOBBERED_BY_CALL:
                hold.pop(r, None)
            if pending_key is not None:
                hold["0"] = (pending_key, pending_default, [])
                pending_key = pending_default = None
        elif m == "fcvt" and a[0][0] in "sdhq":
            src = vreg(a[1])
            if src in hold:
                hold[vreg(a[0])] = hold[src]
            else:
                hold.pop(vreg(a[0]), None)
        elif m in ARITH and a[0][0] in "sdhq":
            srcs = [vreg(x) for x in a[1:] if x and x[0] in "sdhq"]
            src = next((s for s in srcs if s in hold), None)
            if src is not None:
                k, dv, tr = hold[src]
                hold[vreg(a[0])] = (k, dv, tr + [m])
            else:
                hold.pop(vreg(a[0]), None)
        elif m == "str" and a[0][0] in "sdhq" and "sp" in o and vreg(a[0]) in hold:
            try:
                off = int(o.split("#")[1].rstrip("]"), 0)
            except (IndexError, ValueError):
                continue
            k, dv, tr = hold.pop(vreg(a[0]))
            rows.append((off - base, WIDTH[a[0][0]], k, dv, tr))

    return rows, seen_instructions


def control(rows, fields, size) -> tuple[int, int, list[str]]:
    """Every store must sit inside the struct and inside ONE declared field."""
    ok = 0
    bad: list[str] = []
    for off, w, key, _dv, _tr in rows:
        if not (0 <= off and off + w <= size):
            continue                       # scratch, not a field; reported apart
        hit = [(fo, fw) for fo, fw in fields if fo <= off and off + w <= fo + fw]
        if hit:
            ok += 1
        else:
            bad.append("%s: %d bytes at +%d straddles a field boundary" % (key, w, off))
    return ok, len(bad), bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--foreground", action="store_true")
    ap.add_argument("--raw", action="store_true")
    args = ap.parse_args()

    d = load(Path(args.binary))
    cf = key_names(d)
    if not cf:
        print("FAILED: no glass key CFStrings found -- wrong binary, or the run moved")
        return 1
    print("%d CFString keys in the run, %d..%d" % (len(cf), KEY_LO, KEY_HI))

    start, count, base, size = FOREGROUND if args.foreground else BACKGROUND
    fields = FG_FIELDS if args.foreground else BG_FIELDS
    which = "ForegroundUniforms" if args.foreground else "BackgroundUniforms"

    rows, ninstr = read_packer(d, cf, start, count, base)
    print("packer at %#x, %d instructions decoded, %d bindings\n" % (start, ninstr, len(rows)))

    inside = [r for r in rows if 0 <= r[0] and r[0] + r[1] <= size]
    outside = [r for r in rows if r not in inside]

    ok, nbad, bad = control(rows, fields, size)
    print("CONTROL -- every store inside ONE field the IR declares")
    print("  %d of %d in-struct stores land inside a declared field" % (ok, len(inside)))
    for b in bad:
        print("  STRADDLES: %s" % b)
    if nbad:
        print("  FAILED -- the address arithmetic is wrong; the table below is NOT a reading")
        return 1
    print("  PASS\n")

    by_offset = collections.defaultdict(list)
    for off, w, k, dv, tr in inside:
        by_offset[off].append((w, k, dv, tr))

    print("%s -- %d bytes\n" % (which, size))
    print("%5s %3s  %-34s %14s  %s" % ("byte", "w", "key", "default", "transform"))
    for off in sorted(by_offset):
        for w, k, dv, tr in by_offset[off]:
            ds = "(runtime)" if dv is None else ("%.6g" % dv)
            print("%5d %3d  %-34s %14s  %s" % (off, w, k, ds, " ".join(tr)))

    covered = set()
    for off, w, _k, _dv, _tr in inside:
        covered.update(range(off, off + w))
    gaps = []
    run = None
    for b in range(size):
        if b not in covered:
            run = (b, b) if run is None else (run[0], b)
        elif run is not None:
            gaps.append(run)
            run = None
    if run is not None:
        gaps.append(run)

    print("\n%d of %d bytes bound. NOT bound:" % (len(covered), size))
    for a, b in gaps:
        note = ""
        # The annotation keys on OVERLAP with the matrix block, not containment:
        # the gap that actually covers the matrices starts at +64, below 80, and
        # a containment test silently put the note on the wrong line.
        if not args.foreground and a < 152 and b >= 80:
            note = "  <- covers the three colour matrices; assembled from White/Black/Saturation/FillColor by the YCC composite, not stored key by key"
        print("  +%d..+%d (%d bytes)%s" % (a, b, b - a + 1, note))

    if outside and args.raw:
        print("\n%d store(s) outside the struct (scratch, reported not dropped):" % len(outside))
        for off, w, k, dv, tr in sorted(outside):
            print("  %+6d %d  %s" % (off, w, k))

    print("\nFOUND -- %d keys bound to %d of %d bytes" % (len(inside), len(covered), size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
