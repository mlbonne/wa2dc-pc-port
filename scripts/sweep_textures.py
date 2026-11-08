#!/usr/bin/env python3
"""Sweep every Texture2D in a directory for compressed / 16-bit alpha formats.

Usage: python -I sweep_textures.py <directory> [<directory> ...]

Prints each affected texture (file, name, size, format) and a per-format total.
Use this to find ETC1/PVRTC/ATC textures that the Windows player may not decode,
and ARGB4444/RGBA4444/RGB565 textures whose channel order needs checking.
"""
import os
import sys

import UnityPy

FORMATS = {
    2: "ARGB4444",
    7: "RGB565",
    13: "RGBA4444",
    30: "PVRTC_RGB2",
    31: "PVRTC_RGBA2",
    32: "PVRTC_RGB4",
    33: "PVRTC_RGBA4",
    34: "ETC1",
    35: "ATC_RGB4",
    36: "ATC_RGBA8",
}


def main(directories):
    totals = {fmt: 0 for fmt in FORMATS}
    files = 0
    textures = 0
    for directory in directories:
        for root, _, names in os.walk(directory):
            for name in sorted(names):
                path = os.path.join(root, name)
                try:
                    env = UnityPy.load(path)
                except Exception:
                    continue
                files += 1
                for obj in env.objects:
                    if obj.type.name != "Texture2D":
                        continue
                    textures += 1
                    data = obj.read()
                    fmt = int(data.m_TextureFormat)
                    if fmt not in FORMATS:
                        continue
                    totals[fmt] += 1
                    print(
                        "{0}\t{1}\t{2}x{3}\t{4}".format(
                            path, data.m_Name, data.m_Width, data.m_Height, FORMATS[fmt]
                        )
                    )
    print("files={0} textures={1}".format(files, textures))
    for fmt, label in FORMATS.items():
        print("{0}: {1}".format(label, totals[fmt]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
