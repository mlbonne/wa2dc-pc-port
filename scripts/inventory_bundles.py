#!/usr/bin/env python3
"""Inventory a folder of Unity AssetBundles with UnityPy.

Usage:
    python -I inventory_bundles.py <bundle-dir> <output.tsv>

Writes one TSV row per bundle: name, bytes, engine version, object-type
counts, and container entry count. Read-only; no asset data is decoded.
"""
import collections
import os
import sys

import UnityPy


def row_for(path, name):
    env = UnityPy.load(path)
    types = collections.Counter(obj.type.name for obj in env.objects)
    entry = getattr(env, "file", None)
    engine = getattr(entry, "version_engine", None) if entry is not None else None
    container = getattr(env, "container", None) or {}
    type_text = ";".join(f"{key}={types[key]}" for key in sorted(types))
    return [
        name,
        str(os.path.getsize(path)),
        engine or "",
        str(len(container)),
        type_text,
    ]


def main(argv):
    bundle_dir, output_path = argv[1], argv[2]
    names = sorted(name for name in os.listdir(bundle_dir) if name.endswith(".unity3d"))
    with open(output_path, "w", encoding="utf-8", newline="\n") as output:
        output.write("name\tbytes\tengine\tcontainer_entries\ttypes\n")
        for name in names:
            try:
                row = row_for(os.path.join(bundle_dir, name), name)
            except Exception as exc:
                row = [name, str(os.path.getsize(os.path.join(bundle_dir, name))), "", "", f"ERROR: {type(exc).__name__}: {exc}"]
            output.write("\t".join(row) + "\n")
    print(f"wrote {output_path} ({len(names)} bundles)")


if __name__ == "__main__":
    main(sys.argv)
