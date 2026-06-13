#!/usr/bin/env python3
import argparse
import os
import struct
import subprocess


TEXT_VADDR = 0
TEXT_OFF = 0
TEXT_SIZE = 0

RODATA_VADDR = 0
RODATA_OFF = 0


TARGETS = [
    b"Media.MojoVideoEncodeAccelerator.InputStorageType",
    b"VideoEncodeAcceleratorAdapter::PrepareGpuFrame",
    b"VideoEncodeAcceleratorAdapter::PrepareCpuFrame",
    b"VideoEncodeAcceleratorAdapter::EncodeOnAcceleratorThread",
    b"Initializing VAVEA, ",
    b", storage_type: ",
    b"format:%s storage_type:%s coded_size:%s visible_rect:%s natural_size:%s",
]


def off_to_vaddr(off: int) -> int:
    if RODATA_OFF <= off:
        return off - RODATA_OFF + RODATA_VADDR
    return off


def vaddr_to_off(vaddr: int) -> int:
    if TEXT_VADDR <= vaddr < TEXT_VADDR + TEXT_SIZE:
        return vaddr - TEXT_VADDR + TEXT_OFF
    if RODATA_VADDR <= vaddr:
        return vaddr - RODATA_VADDR + RODATA_OFF
    return vaddr


def load_sections(binary: str):
    global TEXT_VADDR, TEXT_OFF, TEXT_SIZE, RODATA_VADDR, RODATA_OFF
    out = subprocess.run(
        ["readelf", "-SW", binary],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 7:
            continue
        if parts[1] == ".text":
            TEXT_VADDR = int(parts[3], 16)
            TEXT_OFF = int(parts[4], 16)
            TEXT_SIZE = int(parts[5], 16)
        elif parts[1] == ".rodata":
            RODATA_VADDR = int(parts[3], 16)
            RODATA_OFF = int(parts[4], 16)
    if not TEXT_SIZE or not RODATA_VADDR:
        raise SystemExit("failed to parse .text/.rodata from readelf")


def find_string(data: bytes, needle: bytes) -> int:
    off = data.find(needle)
    if off < 0:
        raise SystemExit(f"missing string: {needle!r}")
    return off


def find_lea_xrefs(data: bytes, text: bytes, target_vaddr: int):
    hits = []
    base = TEXT_VADDR
    # x86-64 RIP-relative LEA: 48 8d ?? disp32 or 4c 8d ?? disp32
    for prefix in (b"\x48\x8d", b"\x4c\x8d"):
        start = 0
        while True:
            i = text.find(prefix, start)
            if i < 0 or i + 7 > len(text):
                break
            disp = struct.unpack_from("<i", text, i + 3)[0]
            insn_vaddr = base + i
            ref = insn_vaddr + 7 + disp
            if ref == target_vaddr:
                hits.append(insn_vaddr)
            start = i + 2
    return hits


def find_movabs_xrefs(text: bytes, target_vaddr: int):
    hits = []
    imm = struct.pack("<Q", target_vaddr)
    start = 0
    while True:
        i = text.find(imm, start)
        if i < 0:
            break
        hits.append(TEXT_VADDR + i)
        start = i + 1
    return hits


def function_bounds(text: bytes, vaddr: int):
    idx = vaddr - TEXT_VADDR
    start = idx
    while start > 0:
        # Chrome text is commonly int3-padded between functions.
        if text[start - 1] == 0xCC and text[start:start + 4] in (
            b"\x55\x48\x89\xe5",
            b"\x41\x57\x41\x56",
            b"\x53\x48",
            b"\x48\x89",
        ):
            break
        if start > 16 and text[start - 16:start].count(0xCC) >= 8:
            while start < len(text) and text[start] == 0xCC:
                start += 1
            break
        start -= 1
    end = idx
    while end < len(text) - 16:
        if text[end:end + 8] == b"\xCC" * 8:
            break
        end += 1
    return TEXT_VADDR + start, TEXT_VADDR + end


def disassemble(binary: str, start: int, stop: int):
    return subprocess.run(
        [
            "objdump",
            "-d",
            f"--start-address=0x{start:x}",
            f"--stop-address=0x{stop:x}",
            "-Mintel",
            binary,
        ],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    ).stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary", nargs="?", default="/opt/google/chrome/chrome")
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()

    with open(args.binary, "rb") as f:
        data = f.read()
    load_sections(args.binary)
    text = data[TEXT_OFF:TEXT_OFF + TEXT_SIZE]

    print(f"binary={args.binary}", flush=True)
    print(f"text=[0x{TEXT_VADDR:x}, 0x{TEXT_VADDR + TEXT_SIZE:x})", flush=True)

    all_funcs = {}
    for needle in TARGETS:
        off = find_string(data, needle)
        vaddr = off_to_vaddr(off)
        lea_hits = find_lea_xrefs(data, text, vaddr)
        mov_hits = find_movabs_xrefs(text, vaddr)
        hits = sorted(set(lea_hits + mov_hits))
        print(f"\nstring 0x{off:x}/0x{vaddr:x}: {needle.decode(errors='replace')}", flush=True)
        for hit in hits:
            lo, hi = function_bounds(text, hit)
            all_funcs.setdefault((lo, hi), []).append((needle, hit))
            print(f"  xref 0x{hit:x} func 0x{lo:x}..0x{hi:x} size=0x{hi-lo:x}", flush=True)

    if args.dump:
        for (lo, hi), refs in sorted(all_funcs.items()):
            print("\n" + "=" * 100)
            print(f"func 0x{lo:x}..0x{hi:x}")
            for needle, hit in refs:
                print(f"  ref 0x{hit:x}: {needle.decode(errors='replace')}")
            # Keep dumps bounded; enough for patch-site hunting.
            dump_lo = max(lo, min(hit for _, hit in refs) - 0x180)
            dump_hi = min(hi, max(hit for _, hit in refs) + 0x260)
            print(disassemble(args.binary, dump_lo, dump_hi))


if __name__ == "__main__":
    main()
