#!/usr/bin/env python3
"""Scan every AssetBundle for Texture2D and AudioClip storage formats.

Usage:
    python -I scan_media_formats.py <bundle-dir> <output.tsv>

Writes one TSV row per media object (bundle, kind, name, format id, width,
height/bytes). Prints format counts to stdout. Read-only.
"""
import collections
import os
import sys

import UnityPy


def main(argv):
    bundle_dir, output_path = argv[1], argv[2]
    names = sorted(name for name in os.listdir(bundle_dir) if name.endswith(".unity3d"))
    texture_formats = collections.Counter()
    audio_formats = collections.Counter()
    errors = []
    with open(output_path, "w", encoding="utf-8", newline="\n") as output:
        output.write("bundle\tkind\tname\tformat\twidth\theight_or_bytes\n")
        for name in names:
            try:
                env = UnityPy.load(os.path.join(bundle_dir, name))
            except Exception as exc:
                errors.append(f"{name}: {type(exc).__name__}: {exc}")
                continue
            for obj in env.objects:
                if obj.type.name == "Texture2D":
                    try:
                        data = obj.read()
                        fmt = int(getattr(data, "m_TextureFormat", -1))
                        texture_formats[fmt] += 1
                        output.write(f"{name}\tTexture2D\t{data.m_Name}\t{fmt}\t{int(data.m_Width)}\t{int(data.m_Height)}\n")
                    except Exception as exc:
                        errors.append(f"{name} Texture2D: {type(exc).__name__}: {exc}")
                elif obj.type.name == "AudioClip":
                    try:
                        data = obj.read()
                        fmt = int(getattr(data, "m_Format", -1))
                        audio_formats[fmt] += 1
                        raw = getattr(data, "m_AudioData", b"")
                        size = len(raw) if hasattr(raw, "__len__") else 0
                        output.write(f"{name}\tAudioClip\t{data.m_Name}\t{fmt}\t0\t{size}\n")
                    except Exception as exc:
                        errors.append(f"{name} AudioClip: {type(exc).__name__}: {exc}")
    print("texture formats:", dict(sorted(texture_formats.items())))
    print("audio formats:", dict(sorted(audio_formats.items())))
    print(f"errors: {len(errors)}")
    for error in errors[:20]:
        print("  " + error)


if __name__ == "__main__":
    main(sys.argv)
