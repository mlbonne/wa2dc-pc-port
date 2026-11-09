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


def patch_data_path(path):
    """Resolve SaveData.DataPath from -save-slot or WA2DC_SAVE_SLOT, under Documents."""
    with open(path, encoding="utf-8-sig") as handle:
        text = handle.read()
    if "WA2DC_SAVE_SLOT" in text:
        return False
    old = (
        "\t\t\tstring dataPath = Application.dataPath;\n"
        "\t\t\tstring text = dataPath.Substring(0, dataPath.LastIndexOf(\"/\"));\n"
        "\t\t\treturn text + \"/Documents/\";"
    )
    new = (
        "\t\t\tstring dataPath = Application.dataPath;\n"
        "\t\t\tstring text = dataPath.Substring(0, dataPath.LastIndexOf(\"/\"));\n"
        "\t\t\tstring root = text + \"/Documents/\";\n"
        "\t\t\tstring slot = null;\n"
        "\t\t\tstring[] args = Environment.GetCommandLineArgs();\n"
        "\t\t\tfor (int i = 0; i < args.Length - 1; i++)\n"
        "\t\t\t{\n"
        "\t\t\t\tif (args[i] == \"-save-slot\")\n"
        "\t\t\t\t{\n"
        "\t\t\t\t\tslot = args[i + 1];\n"
        "\t\t\t\t\tbreak;\n"
        "\t\t\t\t}\n"
        "\t\t\t}\n"
        "\t\t\tif (string.IsNullOrEmpty(slot))\n"
        "\t\t\t{\n"
        "\t\t\t\tslot = Environment.GetEnvironmentVariable(\"WA2DC_SAVE_SLOT\");\n"
        "\t\t\t}\n"
        "\t\t\tif (string.IsNullOrEmpty(slot))\n"
        "\t\t\t{\n"
        "\t\t\t\treturn root;\n"
        "\t\t\t}\n"
        "\t\t\tstring full = Path.GetFullPath(Path.Combine(root, slot));\n"
        "\t\t\tstring rootFull = Path.GetFullPath(root);\n"
        "\t\t\tif (!rootFull.EndsWith(Path.DirectorySeparatorChar.ToString()))\n"
        "\t\t\t{\n"
        "\t\t\t\trootFull += Path.DirectorySeparatorChar;\n"
        "\t\t\t}\n"
        "\t\t\tif (!full.StartsWith(rootFull, StringComparison.OrdinalIgnoreCase))\n"
        "\t\t\t{\n"
        "\t\t\t\tthrow new ArgumentException(\"save slot escapes Documents\");\n"
        "\t\t\t}\n"
        "\t\t\tif (!full.EndsWith(\"/\") && !full.EndsWith(\"\\\\\"))\n"
        "\t\t\t{\n"
        "\t\t\t\tfull += \"/\";\n"
        "\t\t\t}\n"
        "\t\t\treturn full;"
    )
    if old not in text:
        raise SystemExit("SaveData.DataPath desktop getter not found")
    text = text.replace(old, new, 1)
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
    changed |= patch_data_path(save_data)
    print("boot fixes applied" if changed else "boot fixes already applied")


if __name__ == "__main__":
    main()
