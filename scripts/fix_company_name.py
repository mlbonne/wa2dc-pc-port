#!/usr/bin/env python3
"""Fix the WA2DC PlayerSettings company/product names in a mainData file.

Usage:
    python -I fix_company_name.py <mainData> <output-mainData>

Unity's WWW cache builds its path from companyName/productName. The original
values are "AQUAPLUS\\nAQUAPLUS\\r\\n" and "WA2DC", which makes CreateDirectory
receive a path containing newlines. This rewrites them to clean names while
preserving the serialized file's target platform.
"""
import sys

import UnityPy


def main(argv):
    source, destination = argv[1], argv[2]
    env = UnityPy.load(source)
    found = False
    for obj in env.objects:
        if obj.type.name != "PlayerSettings":
            continue
        data = obj.read()
        print("before companyName=", repr(getattr(data, "companyName", None)))
        print("before productName=", repr(getattr(data, "productName", None)))
        print("target=", getattr(getattr(env, "file", None), "_m_target_platform", None))
        data.companyName = "AQUAPLUS"
        data.productName = "WA2DC"
        data.save()
        found = True
        print("after companyName=", repr(data.companyName))
        print("after productName=", repr(data.productName))
    if not found:
        print("PlayerSettings not found", file=sys.stderr)
        return 1
    payload = env.file.save()
    with open(destination, "wb") as handle:
        handle.write(payload)
    print("wrote", destination, len(payload), "bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
