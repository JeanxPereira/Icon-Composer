#!/usr/bin/env python3
"""Four readings of a THIN arm64 Mach-O, for the slices under `References`.

WHY THIS EXISTS
---------------
`IconRendering.arm64` HAS NO SWIFT SYMBOL NAMES. Its `LC_SYMTAB` holds 1,793
entries and not one of them is a `$s`-mangled function with an address -- only
ObjC selector stubs, class references and a few local helpers. So the ordinary
move (find the symbol, read the function) does not work there, and every reading
in doc 03 §34 had to be reached another way:

    syms   what names DO survive -- selector stubs are the vocabulary of a
           binary that talks to ObjC, and they say what an unnamed function does
    fn     LC_FUNCTION_STARTS, which is complete even when the symbol table is
           not: it is how a bare address becomes "the function at 0x1CB90"
    xref   who calls an address. A selector stub with two callers is a question
           already half answered
    dis    a window, with call targets named from the symbol table

The seals in doc 03 §34 are reproducible with these four and nothing else.

USAGE
    python scripts/macho.py syms  <slice> [substring ...]
    python scripts/macho.py fn    <slice> [0xADDR]
    python scripts/macho.py xref  <slice> 0xTARGET [0xTARGET ...]
    python scripts/macho.py dis   <slice> 0xADDR [count]
"""
import struct
import sys
from pathlib import Path

FAT = (0xCAFEBABE, 0xBEBAFECA, 0xCAFEBABF, 0xBFBAFECA)


def _load(path):
    d = Path(path).read_bytes()
    magic, = struct.unpack_from('<I', d, 0)
    if magic in FAT:
        raise SystemExit('%s is a FAT Mach-O -- carve the slice first with '
                         'slice_arm64.py' % path)
    if magic != 0xFEEDFACF:
        raise SystemExit('%s is not a 64-bit Mach-O (magic %#x)' % (path, magic))
    return d


def _commands(d):
    ncmds, = struct.unpack_from('<I', d, 16)
    off = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', d, off)
        yield cmd, off, size
        off += size


def sections(d):
    out = []
    for cmd, off, _ in _commands(d):
        if cmd != 0x19:  # LC_SEGMENT_64
            continue
        nsects, = struct.unpack_from('<I', d, off + 64)
        p = off + 72
        for _ in range(nsects):
            name = d[p:p + 16].rstrip(b'\0').decode()
            addr, size, foff = struct.unpack_from('<QQI', d, p + 32)
            out.append((name, addr, size, foff))
            p += 80
    return out


def symbols(d):
    """(value, name) for every named entry of LC_SYMTAB."""
    out = []
    for cmd, off, _ in _commands(d):
        if cmd != 0x2:  # LC_SYMTAB
            continue
        symoff, nsyms, stroff, strsize = struct.unpack_from('<IIII', d, off + 8)
        for i in range(nsyms):
            strx, _ntype, _sect, _desc, value = struct.unpack_from(
                '<IBBHQ', d, symoff + i * 16)
            if not strx or strx >= strsize:
                continue
            end = d.index(b'\0', stroff + strx)
            out.append((value, d[stroff + strx:end].decode('utf-8', 'replace')))
    return out


def function_starts(d):
    """Every function's address, from the linker's ULEB128 delta chain.

    Complete where the symbol table is not -- that is the whole point.
    """
    base = None
    span = None
    for cmd, off, _ in _commands(d):
        if cmd == 0x19 and d[off + 8:off + 24].rstrip(b'\0') == b'__TEXT':
            base, = struct.unpack_from('<Q', d, off + 24)
        elif cmd == 0x26:  # LC_FUNCTION_STARTS
            span = struct.unpack_from('<II', d, off + 8)
    if base is None or span is None:
        raise SystemExit('no LC_FUNCTION_STARTS in this slice')
    dataoff, datasize = span
    addr, i, end, out = base, dataoff, dataoff + datasize, []
    while i < end:
        val = shift = 0
        while True:
            b = d[i]
            i += 1
            val |= (b & 0x7F) << shift
            if not b & 0x80:
                break
            shift += 7
        if val == 0:
            break
        addr += val
        out.append(addr)
    return out


def cmd_syms(argv):
    d = _load(argv[0])
    pats = [p.lower() for p in argv[1:]]
    s = symbols(d)
    sys.stderr.write('%d simbolos\n' % len(s))
    for value, name in s:
        if pats and not any(p in name.lower() for p in pats):
            continue
        print('0x%08X  %s' % (value, name))


def cmd_fn(argv):
    d = _load(argv[0])
    fs = function_starts(d)
    if len(argv) < 2:
        sys.stderr.write('%d funcoes\n' % len(fs))
        for a in fs:
            print('0x%08X' % a)
        return
    want = int(argv[1], 16)
    prev = None
    for a in fs:
        if a > want:
            break
        prev = a
    if prev is None:
        raise SystemExit('0x%08X is before the first function' % want)
    nxt = next((a for a in fs if a > want), None)
    if nxt is None:
        print('0x%08X: inicio 0x%08X (ultima funcao)' % (want, prev))
    else:
        print('0x%08X: inicio 0x%08X  fim 0x%08X  (%d bytes)'
              % (want, prev, nxt, nxt - prev))


def cmd_xref(argv):
    """Who BLs (or tail-call Bs) one of these addresses.

    `BL` is `0x94000000 | imm26` and `B` is `0x14000000 | imm26`, the immediate
    a signed word offset from the instruction. A tail call is still a call, so
    both count -- and they are printed apart, because a `B` means the caller's
    frame is gone and the reading upward stops there.
    """
    d = _load(argv[0])
    targets = {int(a, 16) for a in argv[1:]}
    if not targets:
        raise SystemExit('give at least one target address')
    text = next((s for s in sections(d) if s[0] == '__text'), None)
    if text is None:
        raise SystemExit('no __text')
    _, addr, size, foff = text
    hits = 0
    for i in range(0, size - 3, 4):
        w, = struct.unpack_from('<I', d, foff + i)
        op = w & 0xFC000000
        if op not in (0x94000000, 0x14000000):
            continue
        imm = w & 0x03FFFFFF
        if imm & 0x02000000:
            imm -= 0x04000000
        if addr + i + imm * 4 in targets:
            print('0x%08X  %s' % (addr + i, 'BL' if op == 0x94000000 else 'B '))
            hits += 1
    sys.stderr.write('%d call site(s)\n' % hits)


def cmd_dis(argv):
    try:
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
    except ImportError:
        raise SystemExit('needs capstone: python -m pip install capstone')
    d = _load(argv[0])
    at = int(argv[1], 16)
    n = int(argv[2]) if len(argv) > 2 else 40
    text = next((s for s in sections(d) if s[0] == '__text'), None)
    _, addr, size, foff = text
    if not addr <= at < addr + size:
        raise SystemExit('0x%08X is outside __text' % at)
    names = {}
    for value, name in symbols(d):
        if value:
            names.setdefault(value, name)
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    code = d[foff + (at - addr): foff + (at - addr) + n * 4]
    for ins in md.disasm(code, at):
        tag = ''
        for tok in ins.op_str.replace(',', ' ').split():
            tok = tok.strip('[]')
            if not tok.startswith('#0x'):
                continue
            try:
                t = int(tok[1:], 16)
            except ValueError:
                continue
            if t in names:
                tag = '   ; ' + names[t]
        print('0x%08X  %-8s %s%s' % (ins.address, ins.mnemonic, ins.op_str, tag))


COMMANDS = {'syms': cmd_syms, 'fn': cmd_fn, 'xref': cmd_xref, 'dis': cmd_dis}


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in COMMANDS:
        raise SystemExit(__doc__.strip())
    COMMANDS[sys.argv[1]](sys.argv[2:])


if __name__ == '__main__':
    main()
