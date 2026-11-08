#!/usr/bin/env python3
"""Print Unity SerializedFile header fields for one or more files."""
import sys

import UnityPy


def main(argv):
    for path in argv[1:]:
        env = UnityPy.load(path)
        for name, f in env.files.items():
            header = getattr(f, "header", None)
            print(f"== {path} :: {name} ({type(f).__name__})")
            for attr in ("unity_version", "target_platform", "_m_target_platform", "_enable_type_tree", "is_big_endian"):
                print(f"  {attr}={getattr(f, attr, None)!r}")
            try:
                print(f"  types={len(f.types)}")
            except Exception:
                pass
            if header is None:
                print("  no header")
                continue
            values = {}
            for key in dir(header):
                if key.startswith("_"):
                    continue
                try:
                    value = getattr(header, key)
                except Exception:
                    continue
                if callable(value):
                    continue
                values[key] = value
            print("  " + ", ".join(f"{k}={v!r}" for k, v in values.items()))


if __name__ == "__main__":
    main(sys.argv)
