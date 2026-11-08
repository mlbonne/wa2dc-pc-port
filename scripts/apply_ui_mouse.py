#!/usr/bin/env python3
"""Make EZGUI use the mouse on desktop players.

Usage: python -I apply_ui_mouse.py --source <...\\work\\pc-firstpass>

The shipped `UIManager.Awake()` forces `AUTO_TOUCHPAD` to `TOUCHPAD` in every
non-editor player. `PollTouchpad` reads `Input.touchCount`, which is always 0 on
Windows, so every EZGUI control (scroll lists, list buttons, shop items, the
PVSelect arrows) receives no input at all. On desktop we switch the poller to
`POINTER_TYPE.MOUSE` (PollMouse: press/drag/tap/release + wheel); Android/iOS
keep the original touchpad behavior.
"""
import argparse
import os


OLD = (
    "\t\tif (pointerType == POINTER_TYPE.AUTO_TOUCHPAD && !Application.isEditor)\n"
    "\t\t{\n"
    "\t\t\tpointerType = POINTER_TYPE.TOUCHPAD;\n"
    "\t\t}"
)

NEW = (
    "\t\tif (pointerType == POINTER_TYPE.AUTO_TOUCHPAD || pointerType == POINTER_TYPE.TOUCHPAD)\n"
    "\t\t{\n"
    "\t\t\tif (Application.platform == RuntimePlatform.Android || Application.platform == RuntimePlatform.IPhonePlayer)\n"
    "\t\t\t{\n"
    "\t\t\t\tpointerType = POINTER_TYPE.TOUCHPAD;\n"
    "\t\t\t}\n"
    "\t\t\telse\n"
    "\t\t\t{\n"
    "\t\t\t\tpointerType = POINTER_TYPE.MOUSE;\n"
    "\t\t\t}\n"
    "\t\t}"
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()
    path = os.path.join(args.source, "UIManager.cs")
    with open(path, encoding="utf-8-sig") as handle:
        text = handle.read()
    if NEW in text:
        print("UIManager already patched")
        return
    if OLD not in text:
        raise SystemExit("UIManager.Awake AUTO_TOUCHPAD block not found")
    text = text.replace(OLD, NEW, 1)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("UIManager patched for mouse input")


if __name__ == "__main__":
    main()
