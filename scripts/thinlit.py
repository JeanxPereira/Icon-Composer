#!/usr/bin/env python3
"""Double constants in a THIN Mach-O: who holds them, and who reads them.

WHY THIS EXISTS
---------------
`AquaKit/References/scripts/litref.py` answers exactly this question, but it is
bound to the dyld shared cache (`SplitCache`). `IconRendering.framework` is not
in the cache: it ships inside Icon Composer's own bundle, as a loose universal
Mach-O. Same method, different input path.

The method is litref's, transcribed rather than reinvented, including the two
warnings it carries:

  * `capstone.disasm()` stops SILENTLY at the first word it cannot decode, so a
    truncated sweep looks exactly like a clean sweep that found nothing. This
    resumes 4 bytes ahead and prints COVERAGE alongside every result. Below
    100%, "found nothing" is not absence.
  * An authenticated return (`retab`, `braaz`, ...) is a function boundary. Miss
    it and an `adrp` survives into the NEXT function, naming a literal that
    function never reads.

THE THREE SWEEPS
----------------
  --initializers   Swift emits a *variable initialization expression* per
                   stored property that has a default, and for a constant
                   `Double` its whole body is `<load a double>; ret`. They are
                   emitted in DECLARATION order, so a run of N adjacent ones
                   names N defaults in field order. This finds them.
  --pools          literal pools ranked by how many places read them.
  --runs           contiguous runs of plausible doubles in the const sections,
                   for the case where the compiler laid a whole struct's
                   initial value out as one blob.

THE POSITIVE CONTROL (`--self-test`)
------------------------------------
litref's control is SystemBannerUI's 24-hit shadow pool, which does not exist
here. The control that DOES exist in any Mach-O, and is independent of this
decoder, is the C string table: `__TEXT,__cstring` is a sequence of
NUL-terminated strings whose START offsets come from the FILE, not from my
arithmetic. Every `adrp`+`add` this tool resolves into `__cstring` must land on
a string start. Landing mid-string is the exact signature of a botched
immediate -- the failure that once turned 24 hits into 2 -- so the fraction of
hits landing on a boundary is a real, falsifiable check on the address math.

USAGE
    python thinlit.py <thin mach-o> --self-test
    python thinlit.py <thin mach-o> --initializers
    python thinlit.py <thin mach-o> --runs --min-run 4
    python thinlit.py <thin mach-o> --dump 0x<lo> 0x<hi>
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

import capstone

LC_SEGMENT_64 = 0x19
BARRIERS = ("bl", "blr", "ret", "retaa", "retab", "br",
            "braa", "brab", "braaz", "brabz",
            "blraa", "blrab", "blraaz", "blrabz", "brk")
RETURNS = ("ret", "retaa", "retab")


class MachO:
    """Sections of a THIN 64-bit Mach-O, by 'SEG.sect' name."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        magic = struct.unpack("<I", self.data[:4])[0]
        if magic != 0xFEEDFACF:
            raise SystemExit("not a thin 64-bit Mach-O (magic %08x) -- "
                             "carve the slice first (slice_arm64.py)" % magic)
        ncmds = struct.unpack("<I", self.data[16:20])[0]
        self.sections: dict[str, tuple[int, int, int]] = {}   # name -> (va, size, file off)
        off = 32
        for _ in range(ncmds):
            cmd, size = struct.unpack("<II", self.data[off:off + 8])
            if cmd == LC_SEGMENT_64:
                seg = self.data[off + 8:off + 24].split(b"\0")[0].decode()
                nsects = struct.unpack("<I", self.data[off + 64:off + 68])[0]
                s = off + 72
                for _ in range(nsects):
                    name = self.data[s:s + 16].split(b"\0")[0].decode()
                    addr, ssize = struct.unpack("<QQ", self.data[s + 32:s + 48])
                    foff = struct.unpack("<I", self.data[s + 48:s + 52])[0]
                    self.sections["%s.%s" % (seg, name)] = (addr, ssize, foff)
                    s += 80
            off += size

    def read(self, va: int, n: int) -> bytes | None:
        """Bytes at a virtual address, or None when it is in no section."""
        for addr, size, foff in self.sections.values():
            if addr <= va and va + n <= addr + size:
                if foff == 0 and addr == 0:
                    return None
                return self.data[foff + (va - addr):foff + (va - addr) + n]
        return None

    def section_of(self, va: int) -> str | None:
        for name, (addr, size, _) in self.sections.items():
            if addr <= va < addr + size:
                return name
        return None


def decode(data: bytes, base: int):
    """Instructions + coverage. Resumes 4 bytes past each opaque word."""
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    insns, pos, ok = [], 0, 0
    while pos < len(data):
        found = False
        for ins in md.disasm(data[pos:], base + pos):
            insns.append(ins)
            pos += ins.size
            ok += ins.size
            found = True
        if not found:
            pos += 4
    return insns, (ok / len(data) if data else 1.0)


def _imm(txt: str):
    txt = txt.strip()
    if not txt.startswith("#"):
        return None
    try:
        return int(txt[1:], 0)
    except ValueError:
        return None


def readers(insns):
    """[(pc, resolved target, mnemonic)] -- each adrp+add/ldr pair."""
    page: dict[str, int] = {}
    out = []
    for ins in insns:
        m, ops = ins.mnemonic, ins.op_str
        if m == "adrp":
            reg, _, tgt = ops.partition(",")
            v = _imm(tgt)
            if v is None:
                try:
                    v = int(tgt.strip(), 0)
                except ValueError:
                    v = None
            if v is None:
                page.pop(reg.strip(), None)
            else:
                page[reg.strip()] = v
            continue
        if m in BARRIERS:
            page.clear()
            continue
        dst = ops.split(",")[0].strip() if ops else ""
        if m == "add" and ops.count(",") == 2:
            d, n, imm = [p.strip() for p in ops.split(",")]
            v = _imm(imm)
            if n in page and v is not None:
                out.append((ins.address, page[n] + v, "add"))
            page.pop(d, None)
            continue
        if m in ("ldr", "ldur") and "[" in ops and "]" in ops:
            inside = ops[ops.index("[") + 1:ops.rindex("]")]
            parts = [p.strip() for p in inside.split(",")]
            n = parts[0]
            v = _imm(parts[1]) if len(parts) > 1 else 0
            if n in page and v is not None:
                out.append((ins.address, page[n] + v, m))
        if dst in page:
            page.pop(dst, None)
    return out


def double_at(mo: MachO, va: int):
    b = mo.read(va, 8)
    if b is None:
        return None
    return struct.unpack("<d", b)[0]


# ---- sweep A: the per-property initializer micro-functions ----------------
#
# A stored `Double` property with a constant default compiles to a whole
# function of two or three instructions: materialize the constant into d0, then
# return. Nothing else is in the body.

def initializers(mo: MachO, insns):
    """[(entry pc, value, how)] for every 'load a double; ret' micro-function."""
    out = []
    for i, ins in enumerate(insns):
        if ins.mnemonic not in RETURNS:
            continue
        # walk back over instructions that only set up d0
        j = i - 1
        if j < 0:
            continue
        prev = insns[j]
        value = how = None
        if prev.mnemonic == "fmov" and prev.op_str.startswith("d0, #"):
            try:
                value = float(prev.op_str.split("#")[1])
                how = "fmov"
            except ValueError:
                value = None
        elif prev.mnemonic in ("ldr", "ldur") and prev.op_str.startswith("d0, ["):
            ops = prev.op_str
            inside = ops[ops.index("[") + 1:ops.rindex("]")]
            parts = [p.strip() for p in inside.split(",")]
            base = parts[0]
            off = _imm(parts[1]) if len(parts) > 1 else 0
            if j - 1 >= 0 and insns[j - 1].mnemonic == "adrp" and off is not None:
                areg, _, tgt = insns[j - 1].op_str.partition(",")
                if areg.strip() == base:
                    v = _imm(tgt)
                    if v is None:
                        try:
                            v = int(tgt.strip(), 0)
                        except ValueError:
                            v = None
                    if v is not None:
                        value = double_at(mo, v + off)
                        how = "const@0x%x" % (v + off)
        if value is None:
            continue
        # the entry point: 2 instructions for fmov, 3 for the adrp+ldr form
        entry = insns[j - 1].address if how and how.startswith("const") else prev.address
        out.append((entry, value, how))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--initializers", action="store_true")
    ap.add_argument("--pools", action="store_true")
    ap.add_argument("--runs", action="store_true")
    ap.add_argument("--min-run", type=int, default=4)
    ap.add_argument("--dump", nargs=2, metavar=("LO", "HI"))
    ap.add_argument("--target", nargs=2, metavar=("LO", "HI"))
    ap.add_argument("--min", type=int, default=3)
    a = ap.parse_args()

    mo = MachO(Path(a.binary))
    if "__TEXT.__text" not in mo.sections:
        raise SystemExit("no __TEXT.__text")
    tva, tsz, _ = mo.sections["__TEXT.__text"]
    insns, cov = decode(mo.read(tva, tsz), tva)
    print("__text 0x%x..0x%x  %d instructions  coverage %.4f%%"
          % (tva, tva + tsz, len(insns), cov * 100))
    if cov < 1.0:
        print("!! coverage below 100%: an empty result is NOT absence")

    if a.self_test:
        return self_test(mo, insns, cov)

    if a.dump:
        lo, hi = int(a.dump[0], 0), int(a.dump[1], 0)
        for va in range(lo, hi, 8):
            d = double_at(mo, va)
            print("0x%08x  %-24r  %s" % (va, d, mo.section_of(va)))
        return 0

    if a.initializers:
        found = initializers(mo, insns)
        print("\n%d double-returning micro-function(s)\n" % len(found))
        for pc, v, how in found:
            print("0x%08x  %-24r  %s" % (pc, v, how))
        return 0

    hits = readers(insns)
    if a.target:
        lo, hi = int(a.target[0], 0), int(a.target[1], 0)
        sel = [h for h in hits if lo <= h[1] < hi]
        print("\n%d reader(s) of [0x%x, 0x%x)\n" % (len(sel), lo, hi))
        for pc, tgt, m in sel:
            print("  0x%08x  %-5s -> 0x%08x  %r" % (pc, m, tgt, double_at(mo, tgt)))
        return 0

    if a.pools:
        by = {}
        for pc, tgt, m in hits:
            sec = mo.section_of(tgt)
            if sec and ("const" in sec or "literal" in sec):
                by.setdefault(tgt, []).append(pc)
        rank = sorted(by.items(), key=lambda kv: -len(kv[1]))
        print("\n%d const address(es) read; those with >= %d readers:\n"
              % (len(by), a.min))
        for tgt, pcs in rank:
            if len(pcs) < a.min:
                break
            print("  0x%08x  %2d reader(s)  %-22r %s"
                  % (tgt, len(pcs), double_at(mo, tgt), mo.section_of(tgt)))
        return 0

    if a.runs:
        for name in sorted(mo.sections):
            if "const" not in name and "literal" not in name:
                continue
            va, size, _ = mo.sections[name]
            blob = mo.read(va, size)
            if blob is None:
                continue
            run = []
            for k in range(0, size - 7, 8):
                d = struct.unpack("<d", blob[k:k + 8])[0]
                ok = (d == 0.0) or (math.isfinite(d) and 1e-4 <= abs(d) <= 1e4)
                if ok:
                    run.append((va + k, d))
                else:
                    if len(run) >= a.min_run:
                        print("\n%s  run of %d at 0x%08x" % (name, len(run), run[0][0]))
                        for addr, val in run:
                            print("    0x%08x  %r" % (addr, val))
                    run = []
            if len(run) >= a.min_run:
                print("\n%s  run of %d at 0x%08x" % (name, len(run), run[0][0]))
                for addr, val in run:
                    print("    0x%08x  %r" % (addr, val))
        return 0

    ap.print_help()
    return 2


def self_test(mo: MachO, insns, cov) -> int:
    """The C-string boundary control described in the module docstring."""
    bad = 0
    if "__TEXT.__cstring" not in mo.sections:
        print("FAIL no __TEXT.__cstring -- no independent control available")
        return 1
    cva, csz, _ = mo.sections["__TEXT.__cstring"]
    blob = mo.read(cva, csz)
    starts = {cva}
    for k, ch in enumerate(blob):
        if ch == 0 and k + 1 < csz:
            starts.add(cva + k + 1)

    hits = [h for h in readers(insns) if cva <= h[1] < cva + csz]
    on = sum(1 for _, tgt, _ in hits if tgt in starts)
    frac = on / len(hits) if hits else 0.0
    print("\ncontrol: %d resolved pointer(s) into __cstring, %d on a string start (%.2f%%)"
          % (len(hits), on, frac * 100))
    for label, ok in (("hits found", len(hits) >= 50),
                      ("all on a boundary", on == len(hits)),
                      ("coverage 100%%", cov == 1.0)):
        bad += not ok
        print("%-4s %s" % ("PASS" if ok else "FAIL", label))
    if bad:
        print("\nPARTIAL -- %d of 3 anchors wrong; do not trust an empty sweep" % bad)
        return 1
    print("\nFOUND -- 3 of 3 anchors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
