#!/usr/bin/env python3
"""Apply the two PC boot fixes to an AssetRipper Assembly-CSharp source tree.

Usage: python -I apply_boot_fixes.py --source <...\\Assets\\Scripts\\Assembly-CSharp>

1. SaveData.Initialize(): create the desktop <exe>/Documents directory.
2. StoreManager.SD_Initialize(): create it too, so Awake order cannot matter.

The original game never creates that directory; on desktop the first
FileStream(FileMode.Create) otherwise throws DirectoryNotFoundException.
"""
import argparse
import os


def insert_before(path, needle, insertion, marker):
    with open(path, encoding="utf-8-sig") as handle:
        text = handle.read()
    if insertion.strip() in text:
        return False
    if needle not in text:
        raise SystemExit(f"needle not found in {path}: {needle}")
    text = text.replace(needle, insertion + needle, 1)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()
    save_data = os.path.join(args.source, "SaveData.cs")
    store_manager = os.path.join(args.source, "StoreManager.cs")
    changed = insert_before(
        save_data,
        "\t\tmFullName = DataPath + \"save.bin\";",
        "\t\tDirectory.CreateDirectory(DataPath);\n",
        "Directory.CreateDirectory",
    )
    changed |= insert_before(
        store_manager,
        "\t\tm_FullName = SaveData.DataPath + \"share.bin\";",
        "\t\tDirectory.CreateDirectory(SaveData.DataPath);\n",
        "Directory.CreateDirectory",
    )
    print("boot fixes applied" if changed else "boot fixes already applied")


if __name__ == "__main__":
    main()
