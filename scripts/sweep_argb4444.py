#!/usr/bin/env python3
"""Fail if any game texture still uses the Android RGBA4444 nibble order.

The Windows player reads TextureFormat.ARGB4444 (2) as true ARGB4444. Textures
built on Android with alpha in the low nibble decode as opaque yellow (the
"yellow square" bug). A texture is affected when it is format 2 and essentially
no pixel has a zero high nibble (high_zero < 0.001) while some have a zero low
nibble. Format 13 has the inverse order.

Usage:
    python -I sweep_argb4444.py --build <build\\WA2DC>

Scans every serialized file in WA2DC_Data (including hash-named files, levels
and sharedassets) and every .unity3d bundle in StreamingAssets. Exits 1 with
the list if any affected texture remains.
"""
import argparse
import os
import sys

import UnityPy

ARGB4444 = 2
RGBA4444 = 13


def nibble_stats(raw):
    total = len(raw) // 2
    if total == 0:
        return 0.0, 0.0
    low = high = 0
    for i in range(0, total * 2, 2):
        v = raw[i] | (raw[i + 1] << 8)
        if (v & 0x000F) == 0:
            low += 1
        if (v & 0xF000) == 0:
            high += 1
    return low / total, high / total


def affected(fmt, low, high):
    if fmt == ARGB4444:
        return high < 0.001 and low > 0.05
    if fmt == RGBA4444:
        return low < 0.001 and high > 0.05
    return False


def candidates(build):
    data = os.path.join(build, "WA2DC_Data")
    for base, dirs, names in os.walk(data):
        for name in names:
            path = os.path.join(base, name)
            if name.endswith((".assets", ".unity3d")) or name == "mainData":
                yield path
            elif "." not in name and os.path.getsize(path) > 1000:
                yield path


def scan(path):
    try:
        env = UnityPy.load(path)
    except Exception:
        return []
    found = []
    for obj in env.objects:
        if obj.type.name != "Texture2D":
            continue
        try:
            data = obj.read()
        except Exception:
            continue
        fmt = int(data.m_TextureFormat)
        if fmt not in (ARGB4444, RGBA4444):
            continue
        raw = data.image_data
        if not raw:
            continue
        low, high = nibble_stats(raw)
        if affected(fmt, low, high):
            found.append((data.m_Name, fmt, data.m_Width, data.m_Height, low, high))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    args = parser.parse_args()
    build = os.path.abspath(args.build)
    bad = []
    for path in candidates(build):
        for name, fmt, width, height, low, high in scan(path):
            rel = os.path.relpath(path, build)
            print("BAD %s | %s fmt=%d %dx%d low0=%.3f high0=%.3f" % (
                rel, name, fmt, width, height, low, high))
            bad.append(rel)
    if bad:
        print("FAILED: %d texture(s) still use the wrong 16-bit nibble order" % len(bad))
        return 1
    print("OK: no texture uses the Android RGBA4444 nibble order")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
