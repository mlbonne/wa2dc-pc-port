#!/usr/bin/env python3
"""Fix the nibble order of 16-bit alpha textures in Unity 3.x data files.

The Android (GLES) build stores some textures declared as ARGB4444 (format 2)
in RGBA4444 nibble order (alpha in the low nibble). The Windows player reads
format 2 as true ARGB4444, so transparent pixels become opaque yellow. This
script rotates each 16-bit pixel's nibbles in place, keeping the format, size
and object layout unchanged:

    v' = ((v & 0x000F) << 12) | (v >> 4)

It also checks the inverse case for format 13 (RGBA4444 declared but stored
ARGB) and reports every 16-bit alpha texture it finds.

Usage:
    python -I fix_argb4444_order.py <file.assets> [<file.assets> ...]
        [--dry-run] [--dump-dir DIR]

The files are rewritten in place unless --dry-run is given. With --dump-dir,
before/after PNGs of each affected texture are written there for comparison.
"""
import argparse
import os
import sys

import UnityPy

ARGB4444 = 2
RGBA4444 = 13


def nibble_stats(raw):
    total = len(raw) // 2
    low_zero = 0
    high_zero = 0
    for index in range(0, total * 2, 2):
        value = raw[index] | (raw[index + 1] << 8)
        if (value & 0x000F) == 0:
            low_zero += 1
        if (value & 0xF000) == 0:
            high_zero += 1
    return low_zero / total, high_zero / total


def rotate_nibbles(raw):
    out = bytearray(len(raw))
    for index in range(0, len(raw), 2):
        value = raw[index] | (raw[index + 1] << 8)
        value = ((value & 0x000F) << 12) | (value >> 4)
        out[index] = value & 0xFF
        out[index + 1] = (value >> 8) & 0xFF
    return bytes(out)


def process(path, dry_run, dump_dir):
    env = UnityPy.load(path)
    changed = []
    for obj in env.objects:
        if obj.type.name != "Texture2D":
            continue
        data = obj.read()
        fmt = int(data.m_TextureFormat)
        if fmt not in (ARGB4444, RGBA4444):
            continue
        raw = data.image_data
        if not raw:
            print("  {0}: no raw image data".format(data.m_Name))
            continue
        low_zero, high_zero = nibble_stats(raw)
        # Stored RGBA4444 (alpha low) is wrong for format 2; stored ARGB4444
        # (alpha high) is wrong for format 13.
        rotate = (
            fmt == ARGB4444 and low_zero > high_zero
        ) or (
            fmt == RGBA4444 and high_zero > low_zero
        )
        print(
            "  {0}: format={1} {2}x{3} low0={4:.3f} high0={5:.3f} -> {6}".format(
                data.m_Name,
                fmt,
                data.m_Width,
                data.m_Height,
                low_zero,
                high_zero,
                "rotate" if rotate else "keep",
            )
        )
        if not rotate:
            continue
        if dump_dir:
            os.makedirs(dump_dir, exist_ok=True)
            before = data.image
            before.save(os.path.join(dump_dir, data.m_Name + "-before.png"))
        data.image_data = rotate_nibbles(raw)
        data.save()
        if dump_dir:
            data.image.save(os.path.join(dump_dir, data.m_Name + "-after.png"))
        changed.append(data.m_Name)
    if not changed:
        print("no 16-bit alpha texture needed a channel-order fix")
        return []
    if dry_run:
        print("dry run: would rewrite {0} texture(s)".format(len(changed)))
        return changed
    payload = env.file.save()
    with open(path, "wb") as handle:
        handle.write(payload)
    print("wrote {0} ({1} bytes), changed {2} texture(s)".format(path, len(payload), len(changed)))
    return changed


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--dump-dir", default=None)
    args = parser.parse_args(argv)
    total = 0
    for path in args.files:
        print(path)
        total += len(process(path, args.dry_run, args.dump_dir))
    print("total changed: {0}".format(total))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
