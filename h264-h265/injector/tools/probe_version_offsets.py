#!/usr/bin/env python3
"""Probe whether Chrome 146 HEVC hotpatch offsets can be relocated.

This helper is read-only.  It uses stable string xrefs as landmarks, applies
the resulting deltas to known Chrome 146 patch points, and checks whether the
expected bytes exist at the relocated locations.
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
from dataclasses import dataclass


BASE_VEA_CLASS_XREF = 0x8C62314
BASE_PROVIDER_XREF = 0x78779CF
BASE_TEXT_BIAS = 0x1000


@dataclass(frozen=True)
class Section:
    name: str
    vaddr: int
    offset: int
    size: int


@dataclass(frozen=True)
class Probe:
    name: str
    base_vma: int
    expected: bytes
    group: str


PROBES = [
    Probe("VEA constructor", 0x8C5F6D0, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53"), "vea"),
    Probe("VEA initialize", 0x8C5FD10, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53"), "vea"),
    Probe("VEA initialize task", 0x8C607A0, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53"), "vea"),
    Probe("HEVC initialize codec-mask", 0x8C5FF19, bytes.fromhex("b8 c2 04 00 00"), "vea"),
    Probe("HEVC initialize VBR restriction", 0x8C5FF28, bytes.fromhex("83 7b 10 01 75 1e"), "vea"),
    Probe("HEVC mode-selection cmp", 0x8C6085C, bytes.fromhex("83 f9 02"), "vea"),
    Probe("force mode-selection accept", 0x8C60866, bytes.fromhex("83 f8 01 0f 85 be 0a 00 00"), "vea"),
    Probe("H264 delegate constructor", 0x8C54B80, bytes.fromhex("55 48 89 e5 41 57 41 56 41 54 53 48 83 ec 10"), "vea"),
    Probe("CreateEncodeJob", 0x8C64C90, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53 48 83 ec 78"), "vea"),
    Probe("VEA GetSupportedProfiles", 0x8C5F660, bytes.fromhex("55 48 89 e5 53 50 48 89 f0 48 89 fb 48 8b b6 28 03 00 00"), "vea"),
    Probe("VaapiWrapper CreateForVideoCodec", 0x8C73150, bytes.fromhex("55 48 89 e5 41 57 41 56 53 50 4c 89 c3 41 89 f6 49 89 ff"), "vea"),
    Probe("VaapiWrapper Create", 0x8C72B90, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53"), "vea"),
    Probe("ReadOnlyRegionPool::MaybeAllocateBuffer", 0x77CFEB0, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 98 00 00 00"), "adapter"),
    Probe("SetUpVeaConfig storage-type", 0x77CC3E9, bytes.fromhex("41 c6 86 6d 04 00 00 01"), "adapter"),
    Probe("SetUpVeaConfig input-format", 0x77CC3A3, bytes.fromhex("41 c7 86 68 04 00 00"), "adapter"),
    Probe("PrepareGpuFrame entry", 0x77CCDB0, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53"), "adapter"),
    Probe("PrepareCpuFrame", 0x77CD310, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53"), "adapter"),
    Probe("RequireBitstreamBuffers", 0x77CE460, bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 78 01 00 00"), "adapter"),
]

ENV_OFFSETS_VEA_DELTA = {
    "CHROME_HEVC_OFF_VEA_CTOR": 0x8C5F6D0,
    "CHROME_HEVC_OFF_VEA_INITIALIZE": 0x8C5FD10,
    "CHROME_HEVC_OFF_VEA_INITIALIZE_TASK": 0x8C607A0,
    "CHROME_HEVC_OFF_VEA_ACCEPTED_CODEC_MASK": 0x8C5FF19,
    "CHROME_HEVC_OFF_VEA_BYPASS_VBR_RESTRICTION": 0x8C5FF28,
    "CHROME_HEVC_OFF_VEA_ACCEPT_VP8_VP9_RANGE_CMP": 0x8C6085C,
    "CHROME_HEVC_OFF_VEA_FORCE_MODE_ACCEPT": 0x8C60866,
    "CHROME_HEVC_OFF_VEA_DEFAULT_ENCODE_MODE": 0x8C60853,
    "CHROME_HEVC_OFF_H264_DELEGATE_CTOR": 0x8C54B80,
    "CHROME_HEVC_OFF_H264_ARM_STORE_ENCODER": 0x8C60B73,
    "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_A": 0x8C6099E,
    "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_B": 0x8C60A4E,
    "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_C": 0x8C60B94,
    "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_D": 0x8C60D56,
    "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_INIT": 0x8C60E5E,
    "CHROME_HEVC_OFF_DELEGATE_INITIALIZE_CFI_SLOWPATH_CALL": 0x8C5FF80,
    "CHROME_HEVC_OFF_DELEGATE_INITIALIZE_CFI_RUNTIME_CALL": 0x8C60F8B,
    "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_GET_FRAMES": 0x8C60FD8,
    "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_GET_BITSTREAM": 0x8C61037,
    "CHROME_HEVC_OFF_DELEGATE_ENCODE_CFI_CHECK": 0x8C624A5,
    "CHROME_HEVC_OFF_DELEGATE_ENCODE_JOB_CFI_CHECK": 0x8C6428C,
    "CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK": 0x8C69FCF,
    "CHROME_HEVC_OFF_DELEGATE_GET_METADATA_CFI_CHECK": 0x8C6A35E,
    "CHROME_HEVC_OFF_DELEGATE_METADATA_CLEANUP_CFI_CHECK": 0x8C6A385,
    "CHROME_HEVC_OFF_DELEGATE_CLEANUP_SHORT_CFI_BRANCH": 0x8C65C13,
    "CHROME_HEVC_OFF_INITIALIZE_TASK_POST_DELEGATE_UD2": 0x8C611FE,
    "CHROME_HEVC_OFF_CREATE_ENCODE_JOB": 0x8C64C90,
    "CHROME_HEVC_OFF_VEA_GET_SUPPORTED_PROFILES": 0x8C5F660,
}

UNIQUE_PATTERNS = {
    "VEA GetSupportedProfiles": bytes.fromhex(
        "55 48 89 e5 53 50 48 89 f0 48 89 fb 48 8b b6 28 03 00 00"
    ),
    "VaapiWrapper CreateForVideoCodec": bytes.fromhex(
        "55 48 89 e5 41 57 41 56 53 50 4c 89 c3 41 89 f6 49 89 ff"
    ),
}

ENV_UNIQUE_PATTERNS = {
    "CHROME_HEVC_OFF_VEA_GET_SUPPORTED_PROFILES": UNIQUE_PATTERNS[
        "VEA GetSupportedProfiles"
    ],
    "CHROME_HEVC_OFF_VAAPI_CREATE_FOR_VIDEO_CODEC": UNIQUE_PATTERNS[
        "VaapiWrapper CreateForVideoCodec"
    ],
}


def parse_sections(binary: str) -> dict[str, Section]:
    out = subprocess.check_output(["readelf", "-SW", binary], text=True)
    sections: dict[str, Section] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 7 or not parts[0].startswith("["):
            continue
        try:
            name = parts[1]
            vaddr = int(parts[3], 16)
            off = int(parts[4], 16)
            size = int(parts[5], 16)
        except Exception:
            continue
        sections[name] = Section(name, vaddr, off, size)
    return sections


def build_id(binary: str) -> str:
    out = subprocess.check_output(["readelf", "-n", binary], text=True)
    for line in out.splitlines():
        if "Build ID:" in line:
            return line.split("Build ID:", 1)[1].strip()
    return "unknown"


def find_string_vmas(data: bytes, sections: dict[str, Section], needle: str) -> list[int]:
    out: list[int] = []
    needle_bytes = needle.encode() + b"\0"
    for name in (".rodata", ".data.rel.ro"):
        sec = sections.get(name)
        if not sec:
            continue
        blob = data[sec.offset : sec.offset + sec.size]
        start = 0
        while True:
            idx = blob.find(needle_bytes, start)
            if idx < 0:
                break
            out.append(sec.vaddr + idx)
            start = idx + 1
    return out


def find_lea_xrefs(data: bytes, text: Section, target: int) -> list[int]:
    blob = data[text.offset : text.offset + text.size]
    refs: list[int] = []
    start = 0
    while True:
        idx = blob.find(b"\x8d", start)
        if idx < 0 or idx + 6 > len(blob):
            break
        if idx > 0 and 0x40 <= blob[idx - 1] <= 0x4F:
            modrm = blob[idx + 1]
            if (modrm & 0xC7) == 0x05:
                insn = idx - 1
                disp = struct.unpack_from("<i", blob, insn + 3)[0]
                ref_target = text.vaddr + insn + 7 + disp
                if ref_target == target:
                    refs.append(text.vaddr + insn)
        start = idx + 1
    return refs


def first_xref(data: bytes, sections: dict[str, Section], needle: str) -> int:
    text = sections[".text"]
    refs: list[int] = []
    for string_vma in find_string_vmas(data, sections, needle):
        refs.extend(find_lea_xrefs(data, text, string_vma))
    if not refs:
        raise RuntimeError(f"xref not found for {needle!r}")
    return sorted(refs)[0]


def find_all(data: bytes, pattern: bytes) -> list[int]:
    hits: list[int] = []
    start = 0
    while True:
        idx = data.find(pattern, start)
        if idx < 0:
            return hits
        hits.append(idx)
        start = idx + 1


def find_delegate_switch_table_offset(data: bytes, initialize_task_offset: int) -> int | None:
    window_start = initialize_task_offset
    window = data[window_start : window_start + 0x1800]
    marker = b"\x48\x63\x04\x81\x48\x01\xc8\xff\xe0"
    pos = window.find(marker)
    if pos < 7:
        return None
    lea = window_start + pos - 7
    if data[lea : lea + 3] != b"\x48\x8d\x0d":
        return None
    disp = struct.unpack_from("<i", data, lea + 3)[0]
    lea_vma = lea + BASE_TEXT_BIAS
    table_vma = lea_vma + 7 + disp
    return table_vma - BASE_TEXT_BIAS


def maybe_add_vaapi_create_offsets(env_offsets: dict[str, int], data: bytes) -> None:
    """Resolve VaapiWrapper::Create from CreateForVideoCodec when layout matches.

    Chrome 146 and 147 keep Create exactly 0x5c0 bytes before
    CreateForVideoCodec, and the profile-table miss branch is 0x52 bytes into
    Create.  Keep this guarded by byte checks so future layouts fail closed.
    """
    create_for = env_offsets.get("CHROME_HEVC_OFF_VAAPI_CREATE_FOR_VIDEO_CODEC")
    if create_for is None:
        return

    create = create_for - 0x5C0
    prologue = bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53")
    miss_branch = create + 0x52
    miss_pattern = bytes.fromhex("0f 84")
    if data[create : create + len(prologue)] != prologue:
        return
    if data[miss_branch : miss_branch + len(miss_pattern)] != miss_pattern:
        return

    env_offsets["CHROME_HEVC_OFF_VAAPI_CREATE"] = create
    env_offsets[
        "CHROME_HEVC_OFF_VAAPI_CREATE_BYPASS_PROFILE_TABLE_MISS"
    ] = miss_branch


def maybe_add_adapter_offsets(env_offsets: dict[str, int], data: bytes) -> None:
    """Resolve WebCodecs VEA adapter CPU-input patch points.

    The adapter block is not relocatable from the VEA string xref.  Instead use
    the NV12 fast-path compare in PrepareCpuFrame as a compact landmark, then
    validate the neighboring PrepareGpuFrame and SetUpVeaConfig instructions.
    """
    fast_pattern = bytes.fromhex("83 fa 06 74 09")
    prepare_prologue = bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53")
    require_prologue = bytes.fromhex(
        "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 78 01 00 00"
    )
    readonly_prologue = bytes.fromhex(
        "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 98 00 00 00"
    )

    for fast in find_all(data, fast_pattern):
        prepare_cpu = fast - 0x5D
        coded_size = fast - 0x2D
        alloc_format = fast + 0xA2
        wrap_format = fast + 0x1B6
        require = fast + 0x10F3
        if data[prepare_cpu : prepare_cpu + len(prepare_prologue)] != prepare_prologue:
            continue
        if data[coded_size : coded_size + 7] != bytes.fromhex("49 8b 85 b0 01 00 00"):
            continue
        if data[alloc_format : alloc_format + 5] != bytes.fromhex("bf 06 00 00 00"):
            continue
        if data[wrap_format : wrap_format + 5] != bytes.fromhex("bf 06 00 00 00"):
            continue
        if data[require : require + len(require_prologue)] != require_prologue:
            continue

        gpu_candidates: list[int] = []
        start = max(0, fast - 0x900)
        end = max(0, fast - 0x300)
        pos = start
        while True:
            hit = data.find(prepare_prologue, pos, end)
            if hit < 0:
                break
            if (
                data[hit - 0x1160 : hit - 0x1160 + 4]
                == bytes.fromhex("41 8a 45 18")
                and data[hit - 0xA0D : hit - 0xA0D + 5]
                == bytes.fromhex("be 06 00 00 00")
                and data[hit - 0x9C7 : hit - 0x9C7 + 8]
                == bytes.fromhex("41 c7 46 20 01 00 00 00")
            ):
                gpu_candidates.append(hit)
            pos = hit + 1
        if len(gpu_candidates) != 1:
            continue
        prepare_gpu = gpu_candidates[0]

        readonly = None
        pos = fast + 0x2900
        end = fast + 0x2E00
        while True:
            hit = data.find(readonly_prologue, pos, end)
            if hit < 0:
                break
            readonly = hit
            break

        env_offsets["CHROME_HEVC_OFF_ADAPTER_COPY_SUPPORTS_GPU_SHARED_IMAGES"] = (
            prepare_gpu - 0x1160
        )
        env_offsets["CHROME_HEVC_OFF_SETUP_VEA_CONFIG_INPUT_FORMAT"] = (
            prepare_gpu - 0xA0D
        )
        env_offsets["CHROME_HEVC_OFF_SETUP_VEA_CONFIG_STORAGE_TYPE"] = (
            prepare_gpu - 0x9C7
        )
        env_offsets["CHROME_HEVC_OFF_PREPARE_GPU_FRAME"] = prepare_gpu
        env_offsets["CHROME_HEVC_OFF_PREPARE_CPU_FRAME"] = prepare_cpu
        env_offsets["CHROME_HEVC_OFF_PREPARE_CPU_FRAME_DEST_CODED_SIZE_LOAD"] = (
            coded_size
        )
        env_offsets["CHROME_HEVC_OFF_PREPARE_CPU_FRAME_NV12_FAST_PATH_CMP"] = fast
        env_offsets["CHROME_HEVC_OFF_PREPARE_CPU_FRAME_ALLOCATION_FORMAT"] = (
            alloc_format
        )
        env_offsets["CHROME_HEVC_OFF_PREPARE_CPU_FRAME_WRAP_FORMAT"] = wrap_format
        env_offsets["CHROME_HEVC_OFF_REQUIRE_BITSTREAM_BUFFERS"] = require
        if readonly is not None:
            env_offsets["CHROME_HEVC_OFF_READONLY_POOL_MAYBE_ALLOCATE"] = readonly
        return


def last_prologue_before(data: bytes, center: int, before: int, after: int = 0x40) -> int | None:
    prologue = bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53")
    start = max(0, center - before)
    end = min(len(data), center + after)
    hits = [hit for hit in find_all(data[start:end], prologue)]
    if not hits:
        return None
    return start + hits[-1]


def unique_offset(data: bytes, pattern: bytes) -> int | None:
    hits = find_all(data, pattern)
    return hits[0] if len(hits) == 1 else None


def branch_at_or_near(data: bytes, hint: int, before: int = 0x80, after: int = 0x100) -> int:
    if data[hint : hint + 2] == b"\x0f\x87":
        return hint
    start = max(0, hint - before)
    end = min(len(data), hint + after)
    hits = []
    pos = start
    while True:
        hit = data.find(b"\x0f\x87", pos, end)
        if hit < 0:
            break
        hits.append(hit)
        pos = hit + 1
    if not hits:
        return hint
    return min(hits, key=lambda item: abs(item - hint))


def branch_at_or_after(data: bytes, hint: int, after: int = 0x120) -> int:
    if data[hint : hint + 2] == b"\x0f\x87":
        return hint
    end = min(len(data), hint + after)
    hit = data.find(b"\x0f\x87", hint, end)
    return hit if hit >= 0 else hint


def branch_after_distinct_from(data: bytes, hint: int, other: int, after: int = 0x140) -> int:
    if data[hint : hint + 2] == b"\x0f\x87" and hint != other:
        return hint
    end = min(len(data), hint + after)
    pos = hint
    while True:
        hit = data.find(b"\x0f\x87", pos, end)
        if hit < 0:
            return hint
        if hit != other:
            return hit
        pos = hit + 1


def short_cfi_branch_at_or_near(data: bytes, hint: int, before: int = 0x80, after: int = 0x80) -> int:
    if data[hint] == 0x77:
        return hint
    start = max(0, hint - before)
    end = min(len(data), hint + after)
    hits = []
    for pos in range(start, end - 1):
        if data[pos] != 0x77:
            continue
        disp = int.from_bytes(data[pos + 1 : pos + 2], "little", signed=True)
        target = pos + 2 + disp
        # CFI trap targets in these builds point at ud/illegal padding.
        if data[target : target + 2] == b"\x0f\x0b" or data[
            target : target + 5
        ] == bytes.fromhex("67 0f b9 40 02"):
            hits.append(pos)
    if not hits:
        return hint
    return min(hits, key=lambda item: abs(item - hint))


def call_at_or_near_before_store(data: bytes, hint: int, before: int = 0x40, after: int = 0x100) -> int:
    if data[hint] == 0xE8:
        return hint
    start = max(0, hint - before)
    end = min(len(data), hint + after)
    marker = bytes.fromhex("48 89 83 30 01 00 00")
    candidates = []
    pos = start
    while True:
        hit = data.find(b"\xE8", pos, end)
        if hit < 0:
            break
        if marker in data[hit + 5 : hit + 32]:
            candidates.append(hit)
        pos = hit + 1
    if candidates:
        return min(candidates, key=lambda item: abs(item - hint))
    return hint


def maybe_refine_vea_offsets(
    env_offsets: dict[str, int], data: bytes, vea_xref: int, delegate_error_xref: int | None
) -> None:
    """Refine VEA offsets for builds whose VEA cluster is not a pure delta."""
    prologue = bytes.fromhex("55 48 89 e5 41 57 41 56 41 55 41 54 53")
    h264_ctor_pattern = bytes.fromhex(
        "55 48 89 e5 41 57 41 56 41 54 53 48 83 ec 10 "
        "49 89 f7 49 89 fe 48 8d 5d d0"
    )

    def near_old(base_offset: int, before: int = 0x180, after: int = 0x180) -> int | None:
        candidate = vea_xref + (base_offset - BASE_VEA_CLASS_XREF)
        return last_prologue_before(data, candidate + after, before + after, 0)

    ctor = near_old(0x8C5F6D0)
    initialize = near_old(0x8C5FD10)
    if ctor is not None:
        env_offsets["CHROME_HEVC_OFF_VEA_CTOR"] = ctor
    if initialize is not None:
        env_offsets["CHROME_HEVC_OFF_VEA_INITIALIZE"] = initialize

    # Find the dispatch switch first, then anchor InitializeTask and its CFI
    # patch points from the switch marker.  Chrome 148/149 keep this structure
    # even when the class-xref delta no longer lands on the right instructions.
    switch_marker = bytes.fromhex("48 63 04 81 48 01 c8 ff e0")
    search_start = max(0, vea_xref - 0x2500)
    search_end = min(len(data), vea_xref + 0x1000)
    switch_hits = []
    pos = search_start
    while True:
        hit = data.find(switch_marker, pos, search_end)
        if hit < 0:
            break
        switch_hits.append(hit)
        pos = hit + 1
    if len(switch_hits) == 1:
        switch = switch_hits[0]
        task = last_prologue_before(data, switch, 0x500)
        if task is not None:
            env_offsets["CHROME_HEVC_OFF_VEA_INITIALIZE_TASK"] = task
        table = find_delegate_switch_table_offset(data, task if task is not None else switch - 0x300)
        if table is not None:
            env_offsets["CHROME_HEVC_OFF_VEA_DELEGATE_SWITCH_TABLE"] = table

        env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_A"] = switch - 0x0D
        env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_B"] = switch + 0xA3
        env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_C"] = switch + 0x1E9
        env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_D"] = switch + 0x3AB
        # Chrome 148 shifted this branch by one byte; validate before choosing.
        init_cfi = switch + 0x4B3
        if data[init_cfi : init_cfi + 2] != b"\x0f\x87":
            init_cfi -= 1
        env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_INIT"] = init_cfi
        init_extra = switch + 0x4DB
        if data[init_extra : init_extra + 2] == b"\x0f\x87":
            env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_INIT_EXTRA"] = (
                init_extra
            )
        env_offsets["CHROME_HEVC_OFF_DELEGATE_INITIALIZE_CFI_RUNTIME_CALL"] = (
            call_at_or_near_before_store(data, switch + 0x5E0)
        )
        env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_GET_FRAMES"] = (
            branch_at_or_near(data, switch + 0x62D)
        )
        env_offsets["CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_GET_BITSTREAM"] = (
            branch_at_or_near(data, switch + 0x68C)
        )
        env_offsets["CHROME_HEVC_OFF_H264_ARM_STORE_ENCODER"] = switch + 0x1C8

        mode_hits = [
            hit
            for hit in find_all(data[search_start:search_end], bytes.fromhex("83 f9 02"))
            if abs((search_start + hit) - switch) < 0x400
        ]
        if mode_hits:
            mode = search_start + mode_hits[-1]
            env_offsets["CHROME_HEVC_OFF_VEA_ACCEPT_VP8_VP9_RANGE_CMP"] = mode
            force = data.find(bytes.fromhex("83 f8 01 0f 85"), mode, mode + 0x40)
            if force >= 0:
                env_offsets["CHROME_HEVC_OFF_VEA_FORCE_MODE_ACCEPT"] = force

    if initialize is not None:
        mask = data.find(bytes.fromhex("b8 c2 04 00 00"), initialize, initialize + 0x900)
        if mask >= 0:
            env_offsets["CHROME_HEVC_OFF_VEA_ACCEPTED_CODEC_MASK"] = mask
        vbr = data.find(bytes.fromhex("83 7b 10 01 75"), initialize, initialize + 0x900)
        if vbr >= 0:
            env_offsets["CHROME_HEVC_OFF_VEA_BYPASS_VBR_RESTRICTION"] = vbr

    h264_hits = find_all(data, h264_ctor_pattern)
    if len(h264_hits) >= 2:
        # The H264 delegate constructor is the second identical ctor-shaped
        # helper in the VEA delegate cluster for 146-149.
        env_offsets["CHROME_HEVC_OFF_H264_DELEGATE_CTOR"] = h264_hits[1]

    if delegate_error_xref is not None:
        create_candidate = delegate_error_xref - 0x2CDF
        create = last_prologue_before(data, create_candidate + 0x80, 0x180, 0)
        if create is not None and data[create : create + len(prologue)] == prologue:
            env_offsets["CHROME_HEVC_OFF_CREATE_ENCODE_JOB"] = create
            env_offsets["CHROME_HEVC_OFF_DELEGATE_ENCODE_CFI_CHECK"] = (
                branch_at_or_near(data, create + (0x8C624A5 - 0x8C64C90))
            )
            env_offsets["CHROME_HEVC_OFF_DELEGATE_ENCODE_JOB_CFI_CHECK"] = (
                branch_at_or_near(data, create + (0x8C6428C - 0x8C64C90))
            )
            env_offsets[
                "CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK"
            ] = branch_at_or_near(data, create + (0x8C69FCF - 0x8C64C90))
            prepare_extra_pattern = bytes.fromhex(
                "48 29 c1 48 c1 c9 07 48 83 f9 04 0f 87"
            )
            prepare_extra = data.rfind(
                prepare_extra_pattern,
                env_offsets["CHROME_HEVC_OFF_CREATE_ENCODE_JOB"],
                env_offsets["CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK"],
            )
            if prepare_extra >= 0:
                env_offsets[
                    "CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK_EXTRA"
                ] = prepare_extra + len(prepare_extra_pattern) - 2
            prepare_extra2_pattern = bytes.fromhex(
                "49 29 c5 49 c1 cd 07 49 83 fd 04 0f 87"
            )
            prepare_extra2 = data.find(
                prepare_extra2_pattern,
                env_offsets["CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK"],
                env_offsets["CHROME_HEVC_OFF_DELEGATE_GET_METADATA_CFI_CHECK"],
            )
            if prepare_extra2 >= 0:
                env_offsets[
                    "CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK_EXTRA2"
                ] = prepare_extra2 + len(prepare_extra2_pattern) - 2
            env_offsets["CHROME_HEVC_OFF_DELEGATE_GET_METADATA_CFI_CHECK"] = (
                branch_at_or_near(data, create + (0x8C6A35E - 0x8C64C90))
            )
            env_offsets[
                "CHROME_HEVC_OFF_DELEGATE_METADATA_CLEANUP_CFI_CHECK"
            ] = branch_after_distinct_from(
                data,
                create + (0x8C6A385 - 0x8C64C90),
                env_offsets["CHROME_HEVC_OFF_DELEGATE_GET_METADATA_CFI_CHECK"],
            )
            env_offsets["CHROME_HEVC_OFF_DELEGATE_CLEANUP_SHORT_CFI_BRANCH"] = (
                short_cfi_branch_at_or_near(
                    data, create + (0x8C65C13 - 0x8C64C90)
                )
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("chrome")
    parser.add_argument(
        "--env",
        action="store_true",
        help="Print shell export lines for offsets that can be resolved.",
    )
    args = parser.parse_args()

    binary = os.path.abspath(args.chrome)
    with open(binary, "rb") as f:
        data = f.read()
    sections = parse_sections(binary)
    vea_xref = first_xref(data, sections, "media::VaapiVideoEncodeAccelerator")
    delegate_error_xref = None
    try:
        delegate_error_xref = first_xref(data, sections, "VaapiVideoEncodeAcceleratorDelegate error")
    except RuntimeError:
        pass
    provider_xref = first_xref(
        data, sections, "VideoEncodeAcceleratorProvider::GetVideoEncodeAcceleratorSupportedProfiles"
    )
    deltas = {
        "vea": vea_xref - BASE_VEA_CLASS_XREF,
        "adapter": provider_xref - BASE_PROVIDER_XREF,
    }

    env_offsets: dict[str, int] = {
        name: offset + deltas["vea"] for name, offset in ENV_OFFSETS_VEA_DELTA.items()
    }
    for name, pattern in ENV_UNIQUE_PATTERNS.items():
        hits = find_all(data, pattern)
        if len(hits) == 1:
            env_offsets[name] = hits[0]
    table_offset = find_delegate_switch_table_offset(
        data, env_offsets["CHROME_HEVC_OFF_VEA_INITIALIZE_TASK"]
    )
    if table_offset is not None:
        env_offsets["CHROME_HEVC_OFF_VEA_DELEGATE_SWITCH_TABLE"] = table_offset
    maybe_add_vaapi_create_offsets(env_offsets, data)
    maybe_add_adapter_offsets(env_offsets, data)
    maybe_refine_vea_offsets(env_offsets, data, vea_xref, delegate_error_xref)

    if args.env:
        print(f"# chrome={binary}")
        print(f"# build_id={build_id(binary)}")
        for name in sorted(env_offsets):
            print(f"export {name}=0x{env_offsets[name]:x}")
        return 0

    print(f"binary\t{binary}")
    print(f"build_id\t{build_id(binary)}")
    print(f"vea_xref\t0x{vea_xref:x}\tdelta\t0x{deltas['vea']:x}")
    print(f"provider_xref\t0x{provider_xref:x}\tdelta\t0x{deltas['adapter']:x}")
    print("status\tgroup\tname\tvma\tactual")
    ok = 0
    for probe in PROBES:
        # The trampoline constants are file-backed executable mapping offsets,
        # not objdump VMAs.  In official Chrome builds .text VMA is file offset
        # + 0x1000, so byte probing should read directly at the relocated
        # mapping/file offset.
        offset = probe.base_vma + deltas[probe.group]
        actual = data[offset : offset + len(probe.expected)]
        status = "OK" if actual == probe.expected else "MISS"
        if status == "OK":
            ok += 1
        print(f"{status}\t{probe.group}\t{probe.name}\t0x{offset:x}\t{actual[:20].hex(' ')}")
    print(f"matched\t{ok}/{len(PROBES)}")
    print("unique_pattern\tname\tcount\toffsets")
    for name, pattern in UNIQUE_PATTERNS.items():
        hits: list[int] = []
        start = 0
        while True:
            idx = data.find(pattern, start)
            if idx < 0:
                break
            hits.append(idx)
            start = idx + 1
        offsets = ",".join(f"0x{x:x}" for x in hits[:8])
        print(f"unique_pattern\t{name}\t{len(hits)}\t{offsets}")
    return 0 if ok == len(PROBES) else 1


if __name__ == "__main__":
    raise SystemExit(main())
