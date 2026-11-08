#!/usr/bin/env python3
"""Compact inspector for Unity serialized files (mainData, levels, .assets).

Usage:
    python -I inspect_serialized.py <path> [<path> ...]

Read-only. Prints object-type counts and selected metadata (BuildSettings
scene list, PlayerSettings, AssetBundle container entries).
"""
import collections
import json
import sys

import UnityPy


def small(value, limit=300):
    text = repr(value)
    return text if len(text) <= limit else text[:limit] + "...(truncated)"


def inspect(path):
    env = UnityPy.load(path)
    types = collections.Counter(obj.type.name for obj in env.objects)
    result = {
        "path": path,
        "files": [
            {
                "kind": type(f).__name__,
                "name": getattr(f, "name", ""),
                "unity_version": getattr(f, "unity_version", None),
                "version_engine": getattr(f, "version_engine", None),
            }
            for f in env.files.values()
        ],
        "types": dict(types),
    }
    build_settings = []
    player_settings = {}
    for obj in env.objects:
        try:
            if obj.type.name == "BuildSettings":
                data = obj.read()
                levels = getattr(data, "levels", None) or getattr(data, "m_Scenes", None) or []
                parsed = []
                for level in levels:
                    name = getattr(level, "name", None) or getattr(level, "m_Name", None)
                    if name is None and isinstance(level, str):
                        name = level
                    parsed.append(str(name))
                build_settings.append(parsed)
            elif obj.type.name == "PlayerSettings":
                data = obj.read()
                for attribute in (
                    "companyName", "productName", "bundleVersion", "defaultScreenWidth",
                    "defaultScreenHeight", "m_CompanyName", "m_ProductName", "m_BundleVersion",
                ):
                    value = getattr(data, attribute, None)
                    if value is not None:
                        player_settings[attribute] = small(value)
        except Exception as exc:
            result.setdefault("errors", []).append(f"{obj.type.name}: {type(exc).__name__}: {exc}")
    result["build_settings"] = build_settings
    result["player_settings"] = player_settings
    return result


def main(argv):
    for path in argv[1:]:
        try:
            print(json.dumps(inspect(path), ensure_ascii=False))
        except Exception as exc:
            print(json.dumps({"path": path, "error": f"{type(exc).__name__}: {exc}"}, ensure_ascii=False))


if __name__ == "__main__":
    main(sys.argv)
