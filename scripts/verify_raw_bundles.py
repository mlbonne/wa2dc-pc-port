#!/usr/bin/env python3
"""Prove that the patcher's LZMA settings reproduce every UnityWeb bundle.

For each *.unity3d in a finished build, decode the raw LZMA1 payload and re-encode
it with the exact settings the patcher uses (and UnityPy/Python lzma used to build
the target): raw LZMA1, 8 MiB dictionary, lc=3 lp=0 pb=2, normal mode, BT4,
nice_len=123. The re-encoded stream must equal the bytes in the bundle.

Usage:
    python -I scripts/verify_raw_bundles.py --build work\\cleanroom\\WA2DC \
        [--source work\\obb] [--bundle-dir WA2DC_Data\\StreamingAssets]

Exit code 0 = all bundles reproduced; 1 = a mismatch (printed).
"""
import argparse
import glob
import lzma
import os
import struct
import sys

SETTINGS = {
    "id": lzma.FILTER_LZMA1,
    "dict_size": 0x800000,
    "lc": 3,
    "lp": 0,
    "pb": 2,
    "mode": lzma.MODE_NORMAL,
    "mf": lzma.MF_BT4,
    "nice_len": 123,
}


def read_cstr(data, pos):
    end = data.index(b"\0", pos)
    return data[pos:end], end + 1


def parse(data):
    sig, pos = read_cstr(data, 0)
    if sig != b"UnityWeb":
        return None
    version = struct.unpack_from(">I", data, pos)[0]
    pos += 4
    _player, pos = read_cstr(data, pos)
    _engine, pos = read_cstr(data, pos)
    if version > 3:
        return None
    field_pos = pos
    _min, header_size, _levels, count = struct.unpack_from(">IIII", data, pos)
    pos += 16
    if count != 1:
        return None
    comp, _uncomp = struct.unpack_from(">II", data, pos)
    pos += 8
    if version >= 2:
        pos += 4
    if version >= 3:
        pos += 4
    if header_size < field_pos or header_size + comp > len(data) or comp < 13:
        return None
    props, dict_size, _declared = struct.unpack_from("<BIQ", data, header_size)
    lc = props % 9
    rem = props // 9
    pb = rem // 5
    lp = rem % 5
    return header_size, comp, props, dict_size, lc, lp, pb


def decode(data, info):
    header_size, comp, _props, dict_size, lc, lp, pb = info
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[{
        "id": lzma.FILTER_LZMA1, "dict_size": dict_size,
        "lc": lc, "lp": lp, "pb": pb}])
    return dec.decompress(data[header_size + 13:header_size + comp])


def encode(raw):
    comp = lzma.LZMACompressor(format=lzma.FORMAT_RAW, filters=[SETTINGS])
    return comp.compress(raw) + comp.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    parser.add_argument("--bundle-dir", default=os.path.join("WA2DC_Data", "StreamingAssets"))
    args = parser.parse_args()

    bundle_dir = os.path.join(os.path.abspath(args.build), args.bundle_dir)
    names = sorted(glob.glob(os.path.join(bundle_dir, "*.unity3d")))
    if not names:
        print("no bundles in", bundle_dir)
        return 1
    bad = []
    for path in names:
        data = open(path, "rb").read()
        info = parse(data)
        if not info:
            bad.append((os.path.basename(path), "not UnityWeb v3"))
            continue
        raw = decode(data, info)
        stream = data[info[0] + 13:info[0] + info[1]]
        if encode(raw) != stream:
            bad.append((os.path.basename(path), "re-encode differs"))
    print("bundles %d, mismatches %d" % (len(names), len(bad)))
    for name, why in bad[:20]:
        print("  %s: %s" % (name, why))
    if bad:
        print("VERIFY FAILED")
        return 1
    print("VERIFY OK (all bundles re-encode byte-identically)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
