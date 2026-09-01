#!/usr/bin/env python3
"""Carves the arm64 slice out of a FAT Mach-O.

The frameworks in Icon Composer's bundle are universal -- x86_64 and arm64 in one
file -- and every tool that reads load commands wants a THIN one. The arm64 slice
is the one the seals cite, because it is the one macOS 27 runs on the hardware the
target was built for.
"""
import struct
import sys
from pathlib import Path

CPU_ARM64 = 0x0100000C
CPU_X86_64 = 0x01000007
NAMES = {CPU_ARM64: "arm64", CPU_X86_64: "x86_64"}


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: slice_arm64.py <fat mach-o> <out>", file=sys.stderr)
        return 2
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    data = src.read_bytes()
    magic = struct.unpack(">I", data[:4])[0]
    if magic not in (0xCAFEBABE, 0xCAFEBABF):
        print("not a FAT binary (magic %08x); copying as-is" % magic)
        dst.write_bytes(data)
        return 0
    count = struct.unpack(">I", data[4:8])[0]
    print("%d architecture(s)" % count)
    for i in range(count):
        off = 8 + i * 20
        cpu, sub, o, size, align = struct.unpack(">iiIII", data[off:off + 20])
        print("  %-8s offset %-10d size %d" % (NAMES.get(cpu & 0xFFFFFFFF, "?"), o, size))
        if (cpu & 0xFFFFFFFF) == CPU_ARM64:
            dst.write_bytes(data[o:o + size])
            print("  -> wrote %s (%d bytes)" % (dst, size))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
