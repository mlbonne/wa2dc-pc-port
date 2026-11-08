#!/usr/bin/env python3
"""Convert an ETC1 Texture2D inside a Unity 3.x AssetBundle to RGB24/RGBA32.

Usage:
    python -I convert_etc1.py <in.unity3d> <out.unity3d> [--format 3|4]

The bundle's other objects are preserved. Use format 3 (RGB24) or 4 (RGBA32);
the default is 3 to match the companion face texture.
"""
import argparse
import sys

import UnityPy

ETC1 = 34


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--format", dest="target_format", type=int, default=3, choices=(3, 4))
    args = parser.parse_args()

    env = UnityPy.load(args.source)
    converted = 0
    for obj in env.objects:
        if obj.type.name != "Texture2D":
            continue
        data = obj.read()
        if int(data.m_TextureFormat) != ETC1:
            continue
        image = data.image
        print(f"converting {data.m_Name}: ETC1 {data.m_Width}x{data.m_Height} -> format {args.target_format}")
        data.set_image(image, target_format=args.target_format)
        data.save()
        converted += 1
    if converted == 0:
        print("no ETC1 texture found", file=sys.stderr)
        return 1
    bundle = env.file.save()
    with open(args.destination, "wb") as handle:
        handle.write(bundle)
    print(f"wrote {args.destination} ({len(bundle)} bytes), converted={converted}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
