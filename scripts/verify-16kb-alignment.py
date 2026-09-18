#!/usr/bin/env python3
"""Verify PT_LOAD alignment of 64-bit ELF libraries in AAR/APK/AAB/ZIP or .so files.

Targets Android devices with 16KB pages; OS version alone does not imply page
size. This checks ELF headers, not APK ZIP offsets: also run
`zipalign -v -c -P 16 4 app.apk` on the final APK.

Usage: python3 verify-16kb-alignment.py artifact [artifact ...] [--abi arm64-v8a]
Exit 0: every selected library passed; 1: invalid, missing, or under-aligned
libraries; 2: argument error. No third-party dependencies.

This file exists byte-identically in two repositories: mikevocalz/virocore and
mikevocalz/expo-pico, both at scripts/verify-16kb-alignment.py, with its test
alongside it. There is no shared source and no package — a change to one is only
in the other if someone copies it. The alignment check gates release artifacts
in both, so the copies drifting apart means one repo silently stops enforcing
what the other does. Change both, or neither. test_verify_16kb_alignment.py
asserts they still match.
"""
import argparse
import struct
import sys
import zipfile
from pathlib import Path


def min_pt_load_align(data: bytes, machine: int = 183) -> int:
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise ValueError("missing or truncated ELF header")
    if data[4:7] != bytes((2, 1, 1)):
        raise ValueError("expected ELF64 little-endian version 1")
    if struct.unpack_from("<H", data, 18)[0] != machine:
        raise ValueError("ELF machine does not match selected ABI")
    offset = struct.unpack_from("<Q", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 54)
    if size < 56 or not count or count == 0xffff or offset < 64 or offset + size * count > len(data):
        raise ValueError("invalid or truncated program header table")
    alignments = []
    for i in range(count):
        header = offset + i * size
        if struct.unpack_from("<I", data, header)[0] != 1:
            continue
        file_offset, address = struct.unpack_from("<QQ", data, header + 8)
        align = struct.unpack_from("<Q", data, header + 48)[0]
        if align < 1 or align & (align - 1) or file_offset % align != address % align:
            raise ValueError("invalid PT_LOAD alignment or offset/address congruence")
        alignments.append(align)
    if not alignments:
        raise ValueError("no PT_LOAD segments")
    return min(alignments)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+")
    parser.add_argument("--abi", choices=["arm64-v8a", "x86_64"], default="arm64-v8a")
    parser.add_argument("--min-align", type=lambda x: int(x, 0), default=0x4000)
    parser.add_argument(
        "--allow", action="append", default=[], metavar="BASENAME",
        help="Library basename permitted to be under-aligned, repeatable. Reported "
             "as ALLOW and excluded from the exit code. For third-party binaries "
             "you cannot rebuild; every entry needs a reason at the call site. An "
             "--allow that nothing matches is an error, so the list cannot rot.")
    args = parser.parse_args(argv)
    if args.min_align < 1 or args.min_align & (args.min_align - 1):
        parser.error("--min-align must be a positive power of two")
    machine = 183 if args.abi == "arm64-v8a" else 62
    checked = 0
    failures = 0
    allowed = set()

    def check(name, data):
        nonlocal checked, failures
        checked += 1
        try:
            align = min_pt_load_align(data, machine)
            if align < args.min_align:
                raise ValueError(f"PT_LOAD alignment {hex(align)} < {hex(args.min_align)}")
            print(f"OK    {name}: {hex(align)}")
        except (ValueError, struct.error) as error:
            basename = name.rsplit("/", 1)[-1]
            if basename in args.allow:
                allowed.add(basename)
                print(f"ALLOW {name}: {error}")
                return
            failures += 1
            print(f"FAIL  {name}: {error}")

    for artifact in args.artifacts:
        try:
            if Path(artifact).suffix == ".so":
                check(artifact, Path(artifact).read_bytes())
            else:
                with zipfile.ZipFile(artifact) as archive:
                    # infolist(), not namelist(): a zip may carry several members
                    # under one name, and read(name) resolves through NameToInfo,
                    # which keeps only the last. Reading each ZipInfo checks every
                    # member, so a stale .so shadowed by a good one cannot pass.
                    entries = [i for i in archive.infolist() if i.filename.endswith(".so") and
                               any(part in (args.abi, f"android.{args.abi}")
                                   for part in i.filename.split("/"))]
                    if not entries:
                        raise ValueError(f"no {args.abi} shared libraries")
                    for info in sorted(entries, key=lambda i: i.filename):
                        check(f"{artifact}!{info.filename}", archive.read(info))
        except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
            failures += 1
            print(f"FAIL  {artifact}: {error}")
    summary = f"Checked {checked} libraries; {failures} failure(s)"
    if allowed:
        summary += f"; {len(allowed)} allowed ({', '.join(sorted(allowed))})"
    print(summary + ".")

    # An --allow that matched nothing is itself a failure. Either the library was
    # rebuilt and the entry should go, or its name changed and the allowance is
    # now silently covering a different file. Both need a human.
    stale = [name for name in dict.fromkeys(args.allow) if name not in allowed]
    if stale:
        print(f"FAIL  unused --allow: {', '.join(stale)} matched no under-aligned "
              f"library. Remove the entry, or check whether the file was renamed.")
        failures += len(stale)

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
