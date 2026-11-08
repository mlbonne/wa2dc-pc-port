#!/usr/bin/env python3
"""Inspect Unity AssetBundles / serialized files with UnityPy.

Usage:
    python -I inspect_bundles.py [--textures] [--text] <path> [<path> ...]

Prints one JSON object per input file. Read-only: never executes or modifies
the inspected data.
"""
import collections
import json
import sys

import UnityPy


TEXTURE_FORMATS = {
    1: "Alpha8", 2: "ARGB4444", 3: "RGB24", 4: "RGBA32", 5: "ARGB32",
    7: "RGB565", 10: "DXT1", 11: "DXT5", 12: "PVRTC_RGB2",
    13: "PVRTC_RGBA2", 14: "PVRTC_RGB4", 15: "PVRTC_RGBA4",
    16: "ETC_RGB4", 17: "ATC_RGB4", 18: "ATC_RGBA8",
}


def texture_format_name(value):
    name = getattr(value, "name", None)
    if name is not None:
        return name
    try:
        number = int(value)
    except (TypeError, ValueError):
        return str(value)
    return TEXTURE_FORMATS.get(number, f"unknown({number})")


def inspect(path, want_textures, want_text):
    env = UnityPy.load(path)
    versions = sorted({getattr(f, "unity_version", "") for f in env.files.values() if getattr(f, "unity_version", "")})
    types = collections.Counter()
    textures = []
    audio = []
    texts = []
    scripts = []
    for obj in env.objects:
        type_name = obj.type.name
        types[type_name] += 1
        if type_name == "MonoScript":
            try:
                data = obj.read()
                scripts.append({
                    "name": getattr(data, "m_Name", None),
                    "class": getattr(data, "m_ClassName", None),
                })
            except Exception:
                pass
        if not (want_textures or want_text):
            continue
        try:
            data = obj.read()
        except Exception as exc:  # keep going; report per-object read failures
            continue
        if type_name == "Texture2D" and want_textures:
            textures.append({
                "name": data.m_Name,
                "width": int(data.m_Width),
                "height": int(data.m_Height),
                "format": texture_format_name(getattr(data, "m_TextureFormat", None)),
            })
        elif type_name == "AudioClip" and want_textures:
            raw = getattr(data, "m_AudioData", b"")
            if isinstance(raw, (bytes, bytearray, memoryview)):
                header = bytes(raw[:4]).hex()
                size = len(raw)
            elif isinstance(raw, (list, tuple)):
                header = bytes(raw[:4]).hex()
                size = len(raw)
            else:
                header = ""
                size = 0
            audio.append({
                "name": data.m_Name,
                "format": int(getattr(data, "m_Format", -1)),
                "header_hex": header,
                "bytes": size,
                "load_type": str(getattr(data, "m_LoadType", None)),
            })
        elif type_name == "TextAsset" and want_text:
            body = getattr(data, "m_Script", b"")
            if isinstance(body, str):
                body = body.encode("utf-8", "replace")
            texts.append({
                "name": data.m_Name,
                "size": len(body),
                "preview": body[:200].decode("utf-8", "replace"),
            })
    entry = getattr(env, "file", None)
    return {
        "path": path,
        "versions": versions,
        "bundle_name": getattr(entry, "name", None),
        "engine": getattr(entry, "version_engine", None),
        "types": dict(types),
        "scripts": scripts,
        "textures": textures,
        "audio": audio,
        "text_assets": texts,
    }


def main(argv):
    want_textures = "--textures" in argv
    want_text = "--text" in argv
    paths = [a for a in argv[1:] if not a.startswith("--")]
    for path in paths:
        try:
            print(json.dumps(inspect(path, want_textures, want_text), ensure_ascii=False))
        except Exception as exc:
            print(json.dumps({"path": path, "error": f"{type(exc).__name__}: {exc}"}, ensure_ascii=False))


if __name__ == "__main__":
    main(sys.argv)
