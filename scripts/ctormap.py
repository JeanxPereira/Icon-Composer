#!/usr/bin/env python3
"""The field map of a constructor that whole-module optimisation inlined.

WHY THIS IS AN INSTRUMENT AND NOT A HAND READING
------------------------------------------------
`thinlit.py --initializers` finds ZERO per-property initializer functions in
IconRendering, over a __text decoded at 100%. That is real absence: WMO
dissolved them. What survives is one large constructor that writes every
default into the indirect return buffer (x8).

Reading that by hand means decoding `movk` chains by hand -- the exact hazard
`litref.py` records: *"a hastily written decoder does not fail, it lies
quietly."* So the reading is done by machine, and anything the model cannot
resolve is printed as `?` rather than guessed. That is `render_state.py`'s rule
too: a tool that cannot attribute a value says so.

WHAT IT MODELS
--------------
    adrp / add / mov / movk                     integer registers
    fmov dN,xM | fmov dN,#f | fmov vN.2d,#f     scalar and pair floats
    dup vN.2d,xM                                a doubled scalar
    ldr qN|dN|xN,[xM,#off]                      const-pool loads
    str / stur / stp / strb / strh to [base,#o] the writes

Anything else CLOBBERS its destination register, so an unmodelled instruction
degrades a field to `?` instead of letting a stale value be reported as fresh.

A `bl` is a BOUNDARY: it clobbers the caller-saved registers, and when x8 was
pointed into the buffer first it marks a nested sub-object this sweep does not
enter. The offsets it covers are named, not filled.

USAGE
    python ctormap.py <thin mach-o> 0x<entry> [--limit 0x1000]
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from thinlit import MachO, _imm

RETURNS = ("ret", "retaa", "retab")
CALLER_SAVED = ["x%d" % i for i in range(0, 18)]


def _xname(r: str) -> str:
    r = r.strip()
    return "x" + r[1:] if r[:1] == "w" else r


def ctor_map(mo: MachO, entry: int, limit: int = 0x1000):
    """[(offset, size, value, how)] written to the indirect-return buffer."""
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    data = mo.read(entry, limit)
    if data is None:
        raise SystemExit("0x%x is in no section" % entry)

    xr: dict[str, int] = {}
    vr: dict[str, bytes] = {}
    page: dict[str, int] = {}
    base: str | None = None
    sret: int | None = None
    out = []

    def kill(r: str):
        r = r.strip()
        for name in (r, _xname(r)):
            xr.pop(name, None)
            page.pop(name, None)
            vr.pop(name, None)
        if r and r[0] in "qdsv":
            for pre in "qdsv":
                vr.pop(pre + r[1:].split(".")[0], None)

    def setv(r: str, blob: bytes):
        stem = r.split(".")[0][1:]
        for pre in "qdsv":
            vr[pre + stem] = blob

    def getv(r: str):
        return vr.get("q" + r.split(".")[0][1:])

    for ins in md.disasm(data, entry):
        m, ops = ins.mnemonic, ins.op_str
        parts = [p.strip() for p in ops.split(",")]
        if m in RETURNS:
            break

        if m == "mov" and len(parts) == 2 and parts[1] == "x8" and base is None:
            base = parts[0]
            continue

        if m == "adrp":
            v = _imm(parts[1])
            if v is None:
                try:
                    v = int(parts[1], 0)
                except ValueError:
                    v = None
            kill(parts[0])
            if v is not None:
                page[parts[0]] = v
            continue

        if m in ("bl", "blr", "blraa", "blrab"):
            for r in CALLER_SAVED:
                kill(r)
            for i in range(32):
                for pre in "qdsv":
                    vr.pop("%s%d" % (pre, i), None)
            out.append((sret, 0, None, "bl"))
            sret = None
            continue

        if m == "add" and len(parts) == 3:
            v = _imm(parts[2])
            if v is not None and parts[1] == base:
                kill(parts[0])
                if parts[0] == "x8":
                    sret = v
                continue
            if v is not None and parts[1] in page:
                p = page[parts[1]]
                kill(parts[0])
                xr[parts[0]] = p + v
                continue
            kill(parts[0])
            continue

        if m == "mov" and len(parts) == 2:
            v = _imm(parts[1])
            src = xr.get(_xname(parts[1]))
            kill(parts[0])
            if v is not None:
                xr[_xname(parts[0])] = v & 0xFFFFFFFFFFFFFFFF
            elif src is not None:
                xr[_xname(parts[0])] = src
            continue

        if m == "movk" and len(parts) >= 2:
            r = _xname(parts[0])
            v = _imm(parts[1])
            shift = 0
            if len(parts) == 3 and "lsl" in parts[2]:
                shift = int(parts[2].split("#")[1], 0)
            if r in xr and v is not None:
                xr[r] = (xr[r] & ~(0xFFFF << shift)) | (v << shift)
            else:
                xr.pop(r, None)
            continue

        if m == "fmov" and len(parts) == 2:
            d, s = parts
            if s.startswith("#"):
                try:
                    f = float(s[1:])
                except ValueError:
                    kill(d)
                    continue
                b = struct.pack("<d", f)
                kill(d)
                setv(d, b + b if "." in d else b + b"\0" * 8)
            elif _xname(s) in xr:
                v = xr[_xname(s)]
                kill(d)
                setv(d, struct.pack("<Q", v) + b"\0" * 8)
            else:
                kill(d)
            continue

        if m == "dup" and len(parts) == 2 and _xname(parts[1]) in xr:
            b = struct.pack("<Q", xr[_xname(parts[1])])
            kill(parts[0])
            setv(parts[0], b + b)
            continue

        if m in ("ldr", "ldur") and "[" in ops:
            d = parts[0]
            inside = ops[ops.index("[") + 1:ops.rindex("]")]
            ip = [p.strip() for p in inside.split(",")]
            off = _imm(ip[1]) if len(ip) > 1 else 0
            pg = page.get(ip[0])
            kill(d)
            if pg is not None and off is not None:
                n = 16 if d[0] == "q" else 8
                blob = mo.read(pg + off, n)
                if blob is None:
                    continue
                if d[0] in "qds":
                    setv(d, blob if n == 16 else blob + b"\0" * 8)
                else:
                    xr[_xname(d)] = struct.unpack("<Q", blob)[0]
            continue

        if m in ("str", "stur", "stp", "strb", "strh") and "[" in ops:
            inside = ops[ops.index("[") + 1:ops.rindex("]")]
            ip = [p.strip() for p in inside.split(",")]
            if base is None or ip[0] != base:
                continue
            off = _imm(ip[1]) if len(ip) > 1 else 0
            if off is None:
                continue
            regs = [p.strip() for p in ops[:ops.index("[")].split(",") if p.strip()]
            width = {"strb": 1, "strh": 2}.get(m)
            cursor = off
            for r in regs:
                if r in ("xzr", "wzr"):
                    n = width or 8
                    out.append((cursor, n, 0, "zr"))
                    cursor += n
                elif r[0] == "q":
                    b = getv(r)
                    for k in (0, 8):
                        out.append((cursor + k, 8,
                                    struct.unpack("<d", b[k:k + 8])[0] if b else None,
                                    "q" if b else "?"))
                    cursor += 16
                elif r[0] in "ds":
                    b = getv(r)
                    out.append((cursor, 8,
                                struct.unpack("<d", b[:8])[0] if b else None,
                                "d" if b else "?"))
                    cursor += 8
                else:
                    n = width or 8
                    v = xr.get(_xname(r))
                    out.append((cursor, n, v, "int" if v is not None else "?"))
                    cursor += n
            continue

        if parts and parts[0]:
            kill(parts[0])
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary")
    ap.add_argument("entry")
    ap.add_argument("--limit", default="0x1000")
    a = ap.parse_args()
    mo = MachO(Path(a.binary))
    rows = ctor_map(mo, int(a.entry, 0), int(a.limit, 0))
    print("field map of the buffer built at %s\n" % a.entry)
    unresolved = 0
    for off, size, val, how in rows:
        if how == "bl":
            print("  %-30s <- nested constructor at +0x%s"
                  % ("", ("%x" % off) if off is not None else "?"))
            continue
        if val is None:
            unresolved += 1
            print("  +0x%03x  %d  %-20s %-22s %s" % (off, size, "?", "", how))
            continue
        # The tool does not know the FIELD's type -- the struct says Double
        # here and Int there, and nothing in the instruction stream settles it.
        # So both readings of the same bits are printed and the choice is left
        # to whoever holds the field list. Reporting one of them alone would be
        # attributing a type this sweep never measured.
        bits = (struct.pack("<d", val) if how in ("q", "d")
                else struct.pack("<Q", val & 0xFFFFFFFFFFFFFFFF))
        as_d = struct.unpack("<d", bits)[0]
        as_i = struct.unpack("<q", bits)[0]
        print("  +0x%03x  %d  %-20s %-22s %s"
              % (off, size, repr(as_d), "int %d" % as_i, how))
    print("\n%d write(s), %d unresolved" % (len(rows), unresolved))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
