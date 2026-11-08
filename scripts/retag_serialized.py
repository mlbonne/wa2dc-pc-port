#!/usr/bin/env python3
"""Retag Unity 3.x SerializedFile build-target bytes (player-swap helper).

Usage:
    python -I retag_serialized.py <directory> --from 13 --to 5 [--dry-run]

Only files whose header looks like a Unity 3.x serialized file are touched:
  offset 8  : int32 serialized-file version (we accept 7..12)
  offset 20 : NUL-terminated Unity version string (e.g. 3.5.6f4)
  then 4-byte-aligned int32 build target.

The source tree is never modified unless it is the directory passed in; make a
copy first if you need a backup.
"""
import argparse
import os
import struct


def retag(path, source, target, dry_run):
    with open(path, "rb") as handle:
        data = bytearray(handle.read())
    if len(data) < 40:
        return "too-small"
    file_version = struct.unpack_from(">i", data, 8)[0]
    if not 7 <= file_version <= 12:
        return "not-serialized"
    nul = data.find(b"\x00", 20, 64)
    if nul < 0:
        return "no-version"
    version = bytes(data[20:nul]).decode("ascii", "replace")
    if not version.startswith("3."):
        return "not-unity3"
    target_offset = (nul + 1 + 3) & ~3
    if target_offset + 4 > len(data):
        return "short"
    current = struct.unpack_from("<i", data, target_offset)[0]
    if current != source:
        return f"target-{current}"
    if dry_run:
        return f"would-retag-{version}-at-{target_offset}"
    struct.pack_into("<i", data, target_offset, target)
    with open(path, "wb") as handle:
        handle.write(data)
    return f"retagged-{version}-at-{target_offset}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("--from", dest="source", type=int, default=13)
    parser.add_argument("--to", dest="target", type=int, default=5)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip", action="append", default=[])
    args = parser.parse_args()
    counts = {}
    for root, dirs, files in os.walk(args.directory):
        dirs[:] = [d for d in dirs if d not in args.skip]
        for name in files:
            path = os.path.join(root, name)
            try:
                result = retag(path, args.source, args.target, args.dry_run)
            except Exception as exc:
                result = f"error-{type(exc).__name__}"
            counts[result] = counts.get(result, 0) + 1
            if result.startswith(("retagged", "would-retag", "target-")):
                print(f"{result}\t{os.path.relpath(path, args.directory)}")
    print("summary:", dict(sorted(counts.items())))


if __name__ == "__main__":
    main()
