#!/usr/bin/env python3
"""List Texture2D names, sizes and formats inside a Unity 3.x bundle.

Usage: python -I inspect_textures.py <bundle.unity3d> [...]
"""
import sys

import UnityPy


def main(paths):
    for path in paths:
        env = UnityPy.load(path)
        print(path)
        for obj in env.objects:
            if obj.type.name != "Texture2D":
                continue
            data = obj.read()
            print(
                "  {0}: {1}x{2} format={3}".format(
                    data.m_Name, data.m_Width, data.m_Height, int(data.m_TextureFormat)
                )
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
