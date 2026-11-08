#!/usr/bin/env python3
"""List MonoBehaviour script/gameobject pointers and data sizes."""
import sys

import UnityPy


def main(argv):
    env = UnityPy.load(argv[1])
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            size = obj.byte_size
        except Exception:
            size = -1
        try:
            data = obj.read()
            game_object = getattr(data, "m_GameObject", None)
            script = getattr(data, "m_Script", None)
        except Exception:
            game_object = script = "?"
        print(f"{obj.path_id}\tsize={size}\tgameObject={game_object}\tscript={script}")


if __name__ == "__main__":
    main(sys.argv)
