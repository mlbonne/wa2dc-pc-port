#!/usr/bin/env python3
"""Audit 16-bit alpha textures in Unity 3.x serialized files.

Usage:
    python -I inspect_argb4444.py <path> [<path> ...]

For every Texture2D, print format, size and (for format 2/13) the fraction of
16-bit pixels whose low/high nibble is zero. A high zero-low fraction means the
data is stored in RGBA4444 nibble order (alpha in the low nibble).
"""
import os
import sys

import UnityPy

TEXTURE_FORMATS = {
    1: "Alpha8",
    2: "ARGB4444",
    3: "RGB24",
    4: "RGBA32",
    5: "ARGB32",
    7: "RGB565",
    13: "RGBA4444",
    34: "ETC1_RGB4",
}


def nibble_stats(raw):
    if not raw:
        return None
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


def main(paths):
    for path in paths:
        env = UnityPy.load(path)
        for obj in env.objects:
            if obj.type.name != "Texture2D":
                continue
            data = obj.read()
            fmt = int(data.m_TextureFormat)
            if fmt not in (2, 7, 13):
                continue
            raw = data.image_data
            stats = nibble_stats(raw)
            size = getattr(data, "m_CompleteImageSize", None)
            print(
                "{0}\t{1}\t{2}x{3}\tformat={4}({5})\tbytes={6}\tcomplete={7}"
                "\tlow0={8:.3f}\thigh0={9:.3f}".format(
                    os.path.basename(path),
                    data.m_Name,
                    data.m_Width,
                    data.m_Height,
                    fmt,
                    TEXTURE_FORMATS.get(fmt, "?"),
                    len(raw) if raw else 0,
                    size,
                    stats[0] if stats else -1,
                    stats[1] if stats else -1,
                )
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
