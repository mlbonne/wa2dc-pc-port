#!/usr/bin/env python3
"""Verify the finished WA2DC build against the known-good invariants.

Usage:
    python -I verify_build.py --build <build\\WA2DC> [--require-bg]

Checks the exe/Data layout, the patched PlayerSettings, the channelfixed UI
textures, the RGB24 black/white fade textures, the converted face bundle and
the launchers. Exits non-zero with a list of failures.
"""
import argparse
import os
import sys

import UnityPy

FAILS = []


def check(condition, message):
    if not condition:
        FAILS.append(message)


def load(path):
    try:
        return UnityPy.load(path)
    except Exception as exception:
        FAILS.append("cannot load {0}: {1}".format(path, exception))
        return None


def target(path):
    env = load(path)
    if env is None:
        return None
    bundle = env.file
    if type(bundle).__name__ == "BundleFile":
        for serialized in bundle.files.values():
            return getattr(serialized, "_m_target_platform", None)
        return None
    return getattr(bundle, "_m_target_platform", None)


def texture(env, name):
    for obj in env.objects:
        if obj.type.name != "Texture2D":
            continue
        data = obj.read()
        if data.m_Name == name:
            return data
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    parser.add_argument("--require-bg", action="store_true")
    args = parser.parse_args()
    build = os.path.abspath(args.build)
    data = os.path.join(build, "WA2DC_Data")

    for relative in (
        "WA2DC.exe",
        "WA2DC.bat",
        "WA2DC-1080p.bat",
        "WA2DC_Data/mainData",
        "WA2DC_Data/Managed/mscorlib.dll",
        "WA2DC_Data/Managed/UnityEngine.dll",
        "WA2DC_Data/Managed/System.Core.dll",
        "WA2DC_Data/Managed/Assembly-CSharp.dll",
        "WA2DC_Data/Managed/Assembly-CSharp-firstpass.dll",
    ):
        check(os.path.isfile(os.path.join(build, relative)), "missing " + relative)
    if FAILS:
        return report()

    check(target(os.path.join(data, "mainData")) == 5, "mainData target is not 5")
    env = load(os.path.join(data, "mainData"))
    if env is not None:
        settings = None
        for obj in env.objects:
            if obj.type.name == "PlayerSettings":
                settings = obj.read()
        check(settings is not None, "mainData has no PlayerSettings")
        if settings is not None:
            check(settings.companyName == "AQUAPLUS", "companyName is not AQUAPLUS: " + repr(settings.companyName))
            check(settings.productName == "WA2DC", "productName is not WA2DC: " + repr(settings.productName))

    for name in ("sharedassets0.assets", "sharedassets2.assets", "sharedassets5.assets"):
        path = os.path.join(data, name)
        check(target(path) == 5, name + " target is not 5")

    env = load(os.path.join(data, "sharedassets0.assets"))
    if env is not None:
        for name in ("black", "white"):
            texture_data = texture(env, name)
            check(texture_data is not None, "missing texture " + name)
            if texture_data is not None:
                check(int(texture_data.m_TextureFormat) == 3, name + " is not RGB24")
        for name in ("2D_01A_nowloading_0", "2D_01A_nowloading_1", "2D_01A_nowloading_2", "2D_01A_nowloading_3"):
            texture_data = texture(env, name)
            check(texture_data is not None, "missing texture " + name)
            if texture_data is not None:
                image = texture_data.image.convert("RGBA")
                alpha = image.getchannel("A").tobytes()
                zero = alpha.count(0) / len(alpha)
                check(zero > 0.4, name + " alpha is not transparent after the nibble fix (zero={0:.3f})".format(zero))

    for name in ("sharedassets2.assets", "sharedassets5.assets"):
        env = load(os.path.join(data, name))
        if env is None:
            continue
        for texture_data in (obj.read() for obj in env.objects if obj.type.name == "Texture2D"):
            if texture_data.m_Name.startswith(("2D_01A_font_", "2D_01A_ef_ripple")):
                image = texture_data.image.convert("RGBA")
                alpha = image.getchannel("A").tobytes()
                zero = alpha.count(0) / len(alpha)
                check(zero > 0.3, texture_data.m_Name + " alpha is not transparent after the nibble fix (zero={0:.3f})".format(zero))

    streaming = os.path.join(data, "StreamingAssets")
    bundles = [name for name in os.listdir(streaming) if name.endswith(".unity3d")]
    check(len(bundles) == 255, "StreamingAssets has {0} bundles, expected 255".format(len(bundles)))
    face = os.path.join(streaming, "CH101_01A_Face.unity3d")
    if os.path.isfile(face):
        check(target(face) == 5, "face bundle target is not 5")
        env = load(face)
        if env is not None:
            for texture_data in (obj.read() for obj in env.objects if obj.type.name == "Texture2D"):
                check(
                    int(texture_data.m_TextureFormat) == 3,
                    "face texture {0} is format {1}, expected RGB24".format(
                        texture_data.m_Name, int(texture_data.m_TextureFormat)
                    ),
                )
    else:
        check(False, "missing face bundle")
    if args.require_bg:
        check(os.path.isfile(os.path.join(streaming, "bg.png")), "missing StreamingAssets/bg.png")
    return report()


def report():
    if FAILS:
        print("VERIFY FAILED")
        for failure in FAILS:
            print("  -", failure)
        return 1
    print("VERIFY OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
