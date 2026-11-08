#!/usr/bin/env python3
"""List MonoBehaviour serialized field names in a Unity serialized file.

Usage: python -I dump_monobehaviours.py <file>
"""
import sys

import UnityPy


def main(argv):
    env = UnityPy.load(argv[1])
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            data = obj.read()
        except Exception as exc:
            print(f"{obj.path_id}\tREAD_ERROR\t{type(exc).__name__}: {exc}")
            continue
        keys = [k for k in vars(data) if not k.startswith("_") and k != "object_reader"]
        print(f"{obj.path_id}\tm_Name={getattr(data, 'm_Name', None)!r}\tfields={keys}")
        print(f"    m_Script={getattr(data, 'm_Script', None)!r}")


if __name__ == "__main__":
    main(sys.argv)
