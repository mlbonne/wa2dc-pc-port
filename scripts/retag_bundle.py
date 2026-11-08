#!/usr/bin/env python3
"""Retag Unity 3.x AssetBundles from one build target to another.

Usage:
    python -I retag_bundle.py <in.unity3d> <out.unity3d> [--from 13] [--to 5]

Rewrites the bundle with UnityPy while changing the target-platform field of
every contained SerializedFile. Only bundles with the UnityWeb/UnityFS
signatures are supported by UnityPy.
"""
import argparse
import sys

import UnityPy


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--from", dest="source_target", type=int, default=13)
    parser.add_argument("--to", dest="target", type=int, default=5)
    args = parser.parse_args()

    env = UnityPy.load(args.source)
    changed = 0
    bundle = getattr(env, "file", None)
    files = getattr(bundle, "files", None)
    if not files:
        files = env.files
    for name, serialized in files.items():
        current = getattr(serialized, "_m_target_platform", None)
        if current is None:
            print(f"skip {name}: no target platform")
            continue
        if current != args.source_target:
            print(f"skip {name}: target is {current}, not {args.source_target}")
            continue
        serialized._m_target_platform = args.target
        try:
            serialized.mark_changed()
        except Exception:
            pass
        changed += 1
        print(f"retag {name}: {current} -> {args.target}")

    if changed == 0:
        print("nothing changed", file=sys.stderr)
        return 1

    data = env.file.save()
    with open(args.destination, "wb") as handle:
        handle.write(data)
    print(f"wrote {args.destination} ({len(data)} bytes, source {args.source})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
