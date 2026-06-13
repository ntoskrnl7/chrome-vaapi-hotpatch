#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import stat
import struct
import subprocess
import sys


ENCODE_TRACE = b"VideoEncodeAcceleratorAdapter::EncodeOnAcceleratorThread"
PREPARE_CPU_TRACE = b"VideoEncodeAcceleratorAdapter::PrepareCpuFrame"

CPU_INPUT_KIND = 2
SHMEM_STORAGE_TYPE = 0
I420_PIXEL_FORMAT = 1


def fail(message):
    raise SystemExit(f"error: {message}")


def parse_sections(binary):
    out = subprocess.run(
        ["readelf", "-SW", binary],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    sections = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[0] == "[" and parts[1].endswith("]"):
            name, addr, off, size = parts[2], parts[4], parts[5], parts[6]
        elif len(parts) >= 7 and parts[0].startswith("[") and parts[0] != "[Nr]":
            name, addr, off, size = parts[1], parts[3], parts[4], parts[5]
        else:
            continue
        sections[name] = {
            "vaddr": int(addr, 16),
            "off": int(off, 16),
            "size": int(size, 16),
        }
    for name in (".text", ".rodata"):
        if name not in sections:
            fail(f"failed to locate {name} in {binary}")
    return sections


def vaddr_to_off(sections, vaddr):
    for sec in sections.values():
        start = sec["vaddr"]
        end = start + sec["size"]
        if start <= vaddr < end:
            return sec["off"] + (vaddr - start)
    fail(f"vaddr 0x{vaddr:x} is outside known sections")


def off_to_vaddr(sections, off):
    for sec in sections.values():
        start = sec["off"]
        end = start + sec["size"]
        if start <= off < end:
            return sec["vaddr"] + (off - start)
    fail(f"file offset 0x{off:x} is outside known sections")


def read_section(data, sec):
    return data[sec["off"]:sec["off"] + sec["size"]]


def find_one(data, needle, name):
    off = data.find(needle)
    if off < 0:
        fail(f"missing {name}: {needle!r}")
    return off


def find_lea_xrefs(text, text_vaddr, target_vaddr):
    hits = []
    for prefix in (b"\x48\x8d", b"\x4c\x8d"):
        start = 0
        while True:
            i = text.find(prefix, start)
            if i < 0:
                break
            if i + 7 <= len(text):
                disp = struct.unpack_from("<i", text, i + 3)[0]
                ref = text_vaddr + i + 7 + disp
                if ref == target_vaddr:
                    hits.append(text_vaddr + i)
            start = i + 2
    return sorted(set(hits))


def find_function_bounds(text, text_vaddr, hit_vaddr):
    idx = hit_vaddr - text_vaddr
    if idx < 0 or idx >= len(text):
        fail(f"xref 0x{hit_vaddr:x} is outside .text")

    start = idx
    while start > 0:
        if start > 16 and text[start - 16:start].count(0xCC) >= 8:
            while start < len(text) and text[start] == 0xCC:
                start += 1
            break
        if text[start - 1] == 0xCC and text[start:start + 4] in (
            b"\x55\x48\x89\xe5",
            b"\x41\x57\x41\x56",
            b"\x41\x57\x41\x55",
            b"\x53\x48",
            b"\x48\x89",
        ):
            break
        start -= 1

    end = idx
    while end < len(text) - 16:
        if text[end:end + 8] == b"\xCC" * 8:
            break
        end += 1

    return text_vaddr + start, text_vaddr + end


def patch_bytes(buf, off, old, new, name, report):
    got = bytes(buf[off:off + len(old)])
    if got != old:
        fail(
            f"{name}: byte mismatch at file offset 0x{off:x}; "
            f"expected {old.hex(' ')}, got {got.hex(' ')}"
        )
    buf[off:off + len(old)] = new
    report["patches"].append(
        {
            "name": name,
            "file_offset": f"0x{off:x}",
            "old": old.hex(" "),
            "new": new.hex(" "),
        }
    )


def find_input_preference_patch(text, text_vaddr, encode_start_vaddr):
    lo = max(0, encode_start_vaddr - text_vaddr - 0x6000)
    hi = encode_start_vaddr - text_vaddr
    window = text[lo:hi]
    mov = b"\x41\xc7\x86"
    hits = []
    start = 0
    while True:
        i = window.find(mov, start)
        if i < 0:
            break
        if i + 11 <= len(window):
            field_off = window[i + 3:i + 7]
            imm = window[i + 7:i + 11]
            cmp_pat = b"\x41\x83\xbe" + field_off + b"\x00"
            prev = window[max(0, i - 48):i]
            if imm == b"\x01\x00\x00\x00" and cmp_pat in prev:
                hits.append(lo + i)
        start = i + 1
    if len(hits) != 1:
        fail(f"expected one input_buffer_preference patch site, found {len(hits)}")
    off_in_text = hits[0]
    old = text[off_in_text:off_in_text + 11]
    new = old[:7] + struct.pack("<I", CPU_INPUT_KIND)
    return text_vaddr + off_in_text, old, new


def iter_call_targets(text, text_vaddr, lo_vaddr, hi_vaddr):
    lo = max(0, lo_vaddr - text_vaddr)
    hi = min(len(text), hi_vaddr - text_vaddr)
    i = lo
    while i + 5 <= hi:
        if text[i] == 0xE8:
            rel = struct.unpack_from("<i", text, i + 1)[0]
            target = text_vaddr + i + 5 + rel
            yield text_vaddr + i, target
        i += 1


def find_setup_config_patches(text, text_vaddr, encode_start_vaddr):
    search_lo = max(text_vaddr, encode_start_vaddr - 0x6000)
    search_hi = encode_start_vaddr
    storage_pat = b"\x41\xc7\x46\x20\x01\x00\x00\x00"
    fmt_pat = b"\xbe\x06\x00\x00\x00"
    candidates = []

    for call_vaddr, target_vaddr in iter_call_targets(text, text_vaddr, search_lo, search_hi):
        if not text_vaddr <= target_vaddr < text_vaddr + len(text):
            continue
        func_lo, func_hi = find_function_bounds(text, text_vaddr, target_vaddr)
        body = text[func_lo - text_vaddr:func_hi - text_vaddr]
        storage_idx = body.find(storage_pat)
        fmt_idx = body.find(fmt_pat)
        if storage_idx >= 0 and fmt_idx >= 0 and fmt_idx < storage_idx:
            candidates.append((call_vaddr, func_lo, func_hi, fmt_idx, storage_idx))

    uniq = {}
    for item in candidates:
        uniq[item[1]] = item
    candidates = list(uniq.values())
    if len(candidates) != 1:
        detail = ", ".join(f"0x{x[1]:x}" for x in candidates)
        fail(f"expected one SetUpVeaConfig-like function, found {len(candidates)} [{detail}]")

    _, func_lo, _, fmt_idx, storage_idx = candidates[0]
    fmt_vaddr = func_lo + fmt_idx
    storage_vaddr = func_lo + storage_idx
    fmt_old = fmt_pat
    fmt_new = b"\xbe" + struct.pack("<I", I420_PIXEL_FORMAT)
    storage_old = storage_pat
    storage_new = storage_pat[:4] + struct.pack("<I", SHMEM_STORAGE_TYPE)
    return [
        (fmt_vaddr, fmt_old, fmt_new, "VEA Config input_format NV12 -> I420"),
        (storage_vaddr, storage_old, storage_new, "VEA Config storage_type GpuMemoryBuffer -> Shmem"),
    ]


def find_prepare_cpu_patches(data, sections, text, text_vaddr):
    trace_off = find_one(data, PREPARE_CPU_TRACE, "PrepareCpuFrame trace string")
    trace_vaddr = off_to_vaddr(sections, trace_off)
    xrefs = find_lea_xrefs(text, text_vaddr, trace_vaddr)
    if len(xrefs) != 1:
        fail(f"expected one PrepareCpuFrame trace xref, found {len(xrefs)}")
    func_lo, func_hi = find_function_bounds(text, text_vaddr, xrefs[0])
    body = text[func_lo - text_vaddr:func_hi - text_vaddr]
    pat = b"\xbf\x06\x00\x00\x00"
    hits = []
    start = 0
    while True:
        i = body.find(pat, start)
        if i < 0:
            break
        hits.append(i)
        start = i + 1
    if len(hits) < 2:
        fail(f"expected at least two PrepareCpuFrame NV12 allocation sites, found {len(hits)}")
    return [
        (
            func_lo + hits[0],
            pat,
            b"\xbf" + struct.pack("<I", I420_PIXEL_FORMAT),
            "PrepareCpuFrame allocation format NV12 -> I420 #1",
        ),
        (
            func_lo + hits[1],
            pat,
            b"\xbf" + struct.pack("<I", I420_PIXEL_FORMAT),
            "PrepareCpuFrame allocation format NV12 -> I420 #2",
        ),
    ]


def discover_patches(data, sections):
    text_sec = sections[".text"]
    text_vaddr = text_sec["vaddr"]
    text = read_section(data, text_sec)

    encode_trace_off = find_one(data, ENCODE_TRACE, "EncodeOnAcceleratorThread trace string")
    encode_trace_vaddr = off_to_vaddr(sections, encode_trace_off)
    encode_xrefs = find_lea_xrefs(text, text_vaddr, encode_trace_vaddr)
    if len(encode_xrefs) != 1:
        fail(f"expected one EncodeOnAcceleratorThread trace xref, found {len(encode_xrefs)}")
    encode_lo, encode_hi = find_function_bounds(text, text_vaddr, encode_xrefs[0])

    patches = []
    vaddr, old, new = find_input_preference_patch(text, text_vaddr, encode_lo)
    patches.append((vaddr, old, new, "input_buffer_preference GpuMemBuf -> CpuMemBuf"))
    patches.extend(find_setup_config_patches(text, text_vaddr, encode_lo))
    patches.extend(find_prepare_cpu_patches(data, sections, text, text_vaddr))

    return {
        "encode_function": {"start": f"0x{encode_lo:x}", "end": f"0x{encode_hi:x}"},
        "raw_patches": patches,
    }


def main():
    ap = argparse.ArgumentParser(
        description="Patch a copied Chrome binary so Linux VAAPI H.264 WebCodecs encode uses shmem/I420 CPU input."
    )
    ap.add_argument("src", help="source Chrome ELF binary")
    ap.add_argument("dst", help="destination patched Chrome ELF binary")
    ap.add_argument("--report", help="write JSON patch report")
    ap.add_argument("--in-place", action="store_true", help="patch dst in place; src and dst must be the same")
    args = ap.parse_args()

    if args.in_place and os.path.abspath(args.src) != os.path.abspath(args.dst):
        fail("--in-place requires src and dst to be the same path")

    with open(args.src, "rb") as f:
        src_data = f.read()
    sections = parse_sections(args.src)
    discovered = discover_patches(src_data, sections)

    if not args.in_place:
        os.makedirs(os.path.dirname(args.dst), exist_ok=True)
        shutil.copy2(args.src, args.dst)

    with open(args.dst, "rb") as f:
        buf = bytearray(f.read())

    report = {
        "source": args.src,
        "destination": args.dst,
        "encode_function": discovered["encode_function"],
        "patches": [],
    }
    for vaddr, old, new, name in discovered["raw_patches"]:
        off = vaddr_to_off(sections, vaddr)
        patch_bytes(buf, off, old, new, name, report)
        report["patches"][-1]["vaddr"] = f"0x{vaddr:x}"
        print(f"patched {name} at vaddr=0x{vaddr:x} off=0x{off:x}")

    with open(args.dst, "wb") as f:
        f.write(buf)
    mode = os.stat(args.dst).st_mode
    os.chmod(args.dst, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    if args.report:
        os.makedirs(os.path.dirname(args.report), exist_ok=True)
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
            f.write("\n")

    print(args.dst)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(exc.stdout or "", file=sys.stderr)
        fail(f"command failed: {' '.join(exc.cmd)}")
