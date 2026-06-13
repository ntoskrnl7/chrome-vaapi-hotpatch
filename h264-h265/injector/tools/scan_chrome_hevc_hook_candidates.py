#!/usr/bin/env python3
"""Scan a stripped Chrome binary for HEVC encoder hook landmarks.

This is a read-only helper.  It does not patch Chrome.  It finds strings that
survive in official Chrome builds, then scans .text for x86-64 RIP-relative
references to those strings.  The resulting addresses are starting points for
manual disassembly around VaapiVideoEncodeAccelerator paths.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
from dataclasses import dataclass


DEFAULT_NEEDLES = [
    "VideoEncodeAcceleratorProvider::GetVideoEncodeAcceleratorSupportedProfiles",
    "media::VaapiVideoEncodeAccelerator",
    "~VaapiVideoEncodeAccelerator",
    "VaapiVideoEncodeAcceleratorDelegate error",
    "Media.VaapiVideoEncodeAccelerator.VAAPIError",
    "Unsupported output profile ",
    "Failed initializing VAAPI for profile ",
    "Failed initializing encoder. config: ",
    "Unsupported codec: ",
    "HEVC",
    "hvc1",
]


@dataclass(frozen=True)
class Section:
    name: str
    vaddr: int
    offset: int
    size: int


def parse_sections(binary: str) -> dict[str, Section]:
    out = subprocess.check_output(["readelf", "-SW", binary], text=True)
    sections: dict[str, Section] = {}
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
    )
    for line in out.splitlines():
        m = pattern.match(line)
        if not m:
            continue
        name, addr, off, size = m.groups()
        sections[name] = Section(name, int(addr, 16), int(off, 16), int(size, 16))
    return sections


def build_id(binary: str) -> str:
    out = subprocess.check_output(["readelf", "-n", binary], text=True)
    for line in out.splitlines():
        if "Build ID:" in line:
            return line.split("Build ID:", 1)[1].strip()
    return "unknown"


def find_strings(data: bytes, sections: dict[str, Section], needle: str) -> list[int]:
    needle_bytes = needle.encode() + b"\0"
    candidates: list[int] = []
    search_sections = [s for s in (sections.get(".rodata"), sections.get(".data.rel.ro")) if s]
    for sec in search_sections:
        blob = data[sec.offset : sec.offset + sec.size]
        start = 0
        while True:
            idx = blob.find(needle_bytes, start)
            if idx < 0:
                break
            candidates.append(sec.vaddr + idx)
            start = idx + 1
    return candidates


def find_all_rip_xrefs(
    data: bytes, text: Section, target_vaddrs: set[int]
) -> dict[int, list[int]]:
    blob = data[text.offset : text.offset + text.size]
    hits: dict[int, list[int]] = {addr: [] for addr in target_vaddrs}
    # Most surviving string references in optimized x86-64 Chromium are LEA
    # instructions like `48 8d 0d <disp32>` or `4c 8d 35 <disp32>`.  Scan those
    # opcode shapes first; it is fast and precise enough for hook landmarks.
    lea_positions: list[int] = []
    start = 0
    while True:
        idx = blob.find(b"\x8d", start)
        if idx < 0 or idx + 6 > len(blob):
            break
        if idx > 0 and 0x40 <= blob[idx - 1] <= 0x4F:
            modrm = blob[idx + 1]
            if (modrm & 0xC7) == 0x05:
                lea_positions.append(idx - 1)
        start = idx + 1

    for insn_start in lea_positions:
        disp_offset = insn_start + 3
        disp = struct.unpack_from("<i", blob, disp_offset)[0]
        target = text.vaddr + insn_start + 7 + disp
        if target in hits:
            hits[target].append(text.vaddr + insn_start)
    return hits


def objdump_window(binary: str, address: int, radius: int) -> str:
    start = max(address - radius, 0)
    stop = address + radius
    return subprocess.check_output(
        [
            "objdump",
            "-d",
            "--no-show-raw-insn",
            f"--start-address=0x{start:x}",
            f"--stop-address=0x{stop:x}",
            binary,
        ],
        text=True,
        errors="replace",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "chrome",
        nargs="?",
        default="/opt/google/chrome/chrome",
        help="Path to the real Chrome ELF, not the shell wrapper.",
    )
    parser.add_argument(
        "--needle",
        action="append",
        default=[],
        help="Additional exact string to search. May be passed multiple times.",
    )
    parser.add_argument(
        "--disassemble",
        action="store_true",
        help="Print objdump windows around each xref.",
    )
    parser.add_argument("--radius", type=lambda x: int(x, 0), default=0x80)
    args = parser.parse_args()

    binary = os.path.abspath(args.chrome)
    with open(binary, "rb") as f:
        data = f.read()

    sections = parse_sections(binary)
    text = sections.get(".text")
    if not text:
        print("ERROR: .text section not found", file=sys.stderr)
        return 1

    print(f"binary: {binary}")
    print(f"build_id: {build_id(binary)}")
    print(f"text: vaddr=0x{text.vaddr:x} offset=0x{text.offset:x} size=0x{text.size:x}")
    print()

    needle_addrs: list[tuple[str, int]] = []
    for needle in DEFAULT_NEEDLES + args.needle:
        addrs = find_strings(data, sections, needle)
        if not addrs:
            needle_addrs.append((needle, -1))
            continue
        for addr in addrs:
            needle_addrs.append((needle, addr))

    targets = {addr for _, addr in needle_addrs if addr >= 0}
    xrefs_by_target = find_all_rip_xrefs(data, text, targets) if targets else {}

    for needle, addr in needle_addrs:
        if addr < 0:
            print(f"[missing] {needle}")
            continue
        xrefs = xrefs_by_target.get(addr, [])
        print(f"[string] 0x{addr:x} refs={len(xrefs):02d} {needle}")
        for xref in xrefs[:20]:
            print(f"  xref 0x{xref:x}")
            if args.disassemble:
                print(objdump_window(binary, xref, args.radius).rstrip())
        if len(xrefs) > 20:
            print(f"  ... {len(xrefs) - 20} more")
        print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
