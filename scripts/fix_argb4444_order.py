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


def should_rotate(fmt, low_zero, high_zero):
    # The Android RGBA4444 order leaves alpha in the low nibble: almost no
    # pixel has a zero high nibble and many have a zero low nibble. Stock
    # Unity UI textures are true ARGB4444 and have a mixed high nibble, so
    # require high_zero to be essentially zero before rotating.
    if fmt == ARGB4444:
        return high_zero < 0.001 and low_zero > 0.05
    if fmt == RGBA4444:
        return low_zero < 0.001 and high_zero > 0.05
    return False


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
        rotate = should_rotate(fmt, low_zero, high_zero)
        if rotate:
            print(
                "  {0}: format={1} {2}x{3} low0={4:.3f} high0={5:.3f} -> rotate".format(
                    data.m_Name, fmt, data.m_Width, data.m_Height, low_zero, high_zero))
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
        return []
    if dry_run:
        print("dry run: would rewrite {0} texture(s)".format(len(changed)))
        return changed
    payload = env.file.save()
    with open(path, "wb") as handle:
        handle.write(payload)
    print("wrote {0} ({1} bytes), changed {2} texture(s)".format(path, len(payload), len(changed)))
    return changed


def candidates(root):
    for base, dirs, names in os.walk(root):
        for name in names:
            path = os.path.join(base, name)
            if name.endswith((".assets", ".unity3d")) or name in ("mainData",):
                yield path
            elif "." not in name and os.path.getsize(path) > 1000:
                yield path


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*")
    parser.add_argument("--dir", default=None)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--dump-dir", default=None)
    args = parser.parse_args(argv)
    paths = list(args.files)
    if args.dir:
        paths.extend(candidates(args.dir))
    total = 0
    for path in paths:
        try:
            changed = process(path, args.dry_run, args.dump_dir)
        except Exception as exception:
            # Every candidate is a Unity serialized file or bundle. A failure
            # here means the fixer could not process it; never ship silently.
            print("ERROR {0}: {1}".format(path, exception))
            raise
        total += len(changed)
    print("total changed: {0}".format(total))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
