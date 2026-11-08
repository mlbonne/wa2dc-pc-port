#!/usr/bin/env python3
"""Remove illegal `virtual` modifiers from EZGui explicit interface members.

AssetRipper (and ILSpy) emit declarations such as:
    virtual GameObject ISpriteAggregator.gameObject
    virtual void ISpriteAggregator.SetUVs(Rect uvs)
C# does not allow `virtual` on an explicit interface implementation. The
original assembly has these as private virtual IL methods; dropping `virtual`
is the minimal compile fix (or restructuring them as public virtual where a
member is actually overridden).

Usage: python -I fix_ezgui_virtual.py --source <Assembly-CSharp-firstpass dir>
"""
import argparse
import os
import re

PATTERN = re.compile(
    r"(?m)^(\s*)virtual\s+(\S+)\s+([A-Za-z_]\w*\.[A-Za-z_]\w*)(\s*\([^)]*\))?\s*$"
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()
    changed_files = 0
    changed_lines = 0
    for root, _, files in os.walk(args.source):
        for name in files:
            if not name.endswith(".cs"):
                continue
            path = os.path.join(root, name)
            with open(path, encoding="utf-8-sig") as handle:
                text = handle.read()
            new_text, count = PATTERN.subn(r"\1\2 \3\4", text)
            if count:
                changed_files += 1
                changed_lines += count
                with open(path, "w", encoding="utf-8", newline="\n") as handle:
                    handle.write(new_text)
    print(f"removed {changed_lines} virtual modifiers in {changed_files} files")
    if changed_lines == 0:
        print("nothing to do (already fixed?)")


if __name__ == "__main__":
    main()
