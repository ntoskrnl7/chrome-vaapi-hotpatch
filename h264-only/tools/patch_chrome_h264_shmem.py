#!/usr/bin/env python3
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
SRC = "/opt/google/chrome/chrome"
DST = str(ROOT / "chrome-h264-shmem")

# Chrome 146.0.7680.177 x86_64 PIE virtual addresses, identified from
# VideoEncodeAcceleratorAdapter::InitializeOnAcceleratorThread and
# SetUpVeaConfig. We patch only the experiment copy.
PATCHES = [
    {
        "name": "input_buffer_preference Any -> CpuMemBuf",
        "vaddr": 0x77CCDA2,
        "old": bytes.fromhex("41 c7 86 18 01 00 00 01 00 00 00"),
        "new": bytes.fromhex("41 c7 86 18 01 00 00 02 00 00 00"),
    },
    {
        "name": "VEA Config storage_type GpuMemoryBuffer -> Shmem",
        "vaddr": 0x77CD3E9,
        "old": bytes.fromhex("41 c7 46 20 01 00 00 00"),
        "new": bytes.fromhex("41 c7 46 20 00 00 00 00"),
    },
    {
        "name": "VEA Config input_format NV12 -> I420",
        "vaddr": 0x77CD3A3,
        "old": bytes.fromhex("be 06 00 00 00"),
        "new": bytes.fromhex("be 01 00 00 00"),
    },
    {
        "name": "PrepareCpuFrame allocation format NV12 -> I420 #1",
        "vaddr": 0x77CE40F,
        "old": bytes.fromhex("bf 06 00 00 00"),
        "new": bytes.fromhex("bf 01 00 00 00"),
    },
    {
        "name": "PrepareCpuFrame allocation format NV12 -> I420 #2",
        "vaddr": 0x77CE523,
        "old": bytes.fromhex("bf 06 00 00 00"),
        "new": bytes.fromhex("bf 01 00 00 00"),
    },
]


def section_info(binary):
    out = subprocess.run(
        ["readelf", "-SW", binary],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[1] == ".text":
            return int(parts[3], 16), int(parts[4], 16), int(parts[5], 16)
    raise SystemExit("failed to locate .text")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    dst = sys.argv[2] if len(sys.argv) > 2 else DST
    text_vaddr, text_off, text_size = section_info(src)

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)

    with open(dst, "r+b") as f:
        for patch in PATCHES:
            vaddr = patch["vaddr"]
            if not text_vaddr <= vaddr < text_vaddr + text_size:
                raise SystemExit(f"{patch['name']}: vaddr outside .text")
            off = text_off + (vaddr - text_vaddr)
            f.seek(off)
            got = f.read(len(patch["old"]))
            if got != patch["old"]:
                raise SystemExit(
                    f"{patch['name']}: bytes mismatch at file offset 0x{off:x}\n"
                    f"  expected {patch['old'].hex(' ')}\n"
                    f"  got      {got.hex(' ')}"
                )
            f.seek(off)
            f.write(patch["new"])
            print(f"patched {patch['name']} at vaddr=0x{vaddr:x} off=0x{off:x}")

    mode = os.stat(dst).st_mode
    os.chmod(dst, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    print(dst)


if __name__ == "__main__":
    main()
