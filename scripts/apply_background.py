#!/usr/bin/env python3
"""Apply the corrected Phase-3 strip background to an Assembly-CSharp source tree.

Usage:
    python -I apply_background.py --source <...\\Assets\\Scripts\\Assembly-CSharp> \
        --patch patches\\03-background --input patches\\03-input

- copies PortCurtainSkin.cs + PortInput.cs into the source
- removes the old PortBackground.cs if present
- CoreData.Awake: replaces PortBackground.Ensure() with PortInput.Ensure()
- DropCurtain.Initalize: appends PortCurtainSkin.Apply(this) so every rebuild
  (including window resize/fullscreen) re-applies the art to the Left/Right
  letterbox curtains. Cameras/viewport/clear flags are never touched.
"""
import argparse
import os
import shutil


def patch_core_data(source):
    path = os.path.join(source, "CoreData.cs")
    with open(path, encoding="utf-8-sig") as handle:
        text = handle.read()
    original = text
    text = text.replace("PortBackground.Ensure();", "PortInput.Ensure();")
    if "PortInput.Ensure();" not in text:
        needle = "\t\tm_ResourceMan = base.gameObject.AddComponent<ResourceMan>();"
        if needle not in text:
            raise SystemExit("ResourceMan creation line not found in CoreData.Awake")
        text = text.replace(needle, needle + "\n\t\tPortInput.Ensure();", 1)
    if text != original:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        print("CoreData patched")
    else:
        print("CoreData already patched")


def patch_drop_curtain(source):
    path = os.path.join(source, "DropCurtain.cs")
    with open(path, encoding="utf-8-sig") as handle:
        text = handle.read()
    if "PortCurtainSkin.Apply(this);" in text:
        print("DropCurtain already patched")
        return
    index = text.rfind("\n\t}")
    if index < 0:
        raise SystemExit("DropCurtain.Initalize closing brace not found")
    text = text[:index] + "\n\t\tPortCurtainSkin.Apply(this);" + text[index:]
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("DropCurtain patched")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--patch", required=True)
    parser.add_argument("--input", required=True)
    args = parser.parse_args()
    old = os.path.join(args.source, "PortBackground.cs")
    if os.path.exists(old):
        os.remove(old)
        print("removed old PortBackground.cs")
    shutil.copyfile(
        os.path.join(args.patch, "PortCurtainSkin.cs"),
        os.path.join(args.source, "PortCurtainSkin.cs"),
    )
    shutil.copyfile(
        os.path.join(args.input, "PortInput.cs"),
        os.path.join(args.source, "PortInput.cs"),
    )
    patch_core_data(args.source)
    patch_drop_curtain(args.source)


if __name__ == "__main__":
    main()
