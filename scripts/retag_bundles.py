#!/usr/bin/env python3
"""Retag every AssetBundle in a directory from one build target to another.

Usage: python -I retag_bundles.py <source-dir> <dest-dir> [--from 13] [--to 5]

Writes a mirrored set of .unity3d files. Existing destination files are
overwritten. Prints a summary and any per-bundle errors.
"""
import argparse
import os
import sys

import UnityPy


def retag(source, destination, source_target, target):
    env = UnityPy.load(source)
    bundle = getattr(env, "file", None)
    files = getattr(bundle, "files", None)
    if not files:
        return "no-serialized-files"
    changed = 0
    for name, serialized in files.items():
        current = getattr(serialized, "_m_target_platform", None)
        if current != source_target:
            continue
        serialized._m_target_platform = target
        try:
            serialized.mark_changed()
        except Exception:
            pass
        changed += 1
    data = env.file.save()
    with open(destination, "wb") as handle:
        handle.write(data)
    return f"retagged-{changed}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir")
    parser.add_argument("dest_dir")
    parser.add_argument("--from", dest="source_target", type=int, default=13)
    parser.add_argument("--to", dest="target", type=int, default=5)
    args = parser.parse_args()
    os.makedirs(args.dest_dir, exist_ok=True)
    names = sorted(name for name in os.listdir(args.source_dir) if name.endswith(".unity3d"))
    summary = {}
    errors = []
    for index, name in enumerate(names, 1):
        try:
            result = retag(os.path.join(args.source_dir, name), os.path.join(args.dest_dir, name), args.source_target, args.target)
        except Exception as exc:
            result = f"error-{type(exc).__name__}"
            errors.append(f"{name}: {type(exc).__name__}: {exc}")
        summary[result] = summary.get(result, 0) + 1
        if index % 25 == 0 or index == len(names):
            print(f"{index}/{len(names)}")
    print("summary:", dict(sorted(summary.items())))
    for error in errors[:20]:
        print("  " + error)


if __name__ == "__main__":
    main()
