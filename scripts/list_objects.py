#!/usr/bin/env python3
"""List serialized object ids, types and byte sizes in a Unity file."""
import collections
import sys

import UnityPy


def main(argv):
    env = UnityPy.load(argv[1])
    sizes = collections.Counter()
    for obj in env.objects:
        size = getattr(obj, "byte_size", None)
        if size is None:
            try:
                size = len(obj.get_raw_data())
            except Exception:
                size = -1
        sizes[(obj.type.name, size)] += 1
        print(f"{obj.path_id}\t{obj.type.name}\t{size}")
    print("--- summary ---")
    for (type_name, size), count in sorted(sizes.items()):
        print(f"{type_name}\t{size}\t{count}")


if __name__ == "__main__":
    main(sys.argv)
