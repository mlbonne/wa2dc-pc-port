#!/usr/bin/env python3
"""Build the WA2DC patcher data (WA2DC_Patch.dat) and exe.

Usage:
    python -I build_patcher.py --build <build\\WA2DC> --xapk <game.xapk> \
        [--out "dist\\WA2DC Patcher"] [--patcher patcher] [--work work\\patcher] \
        [--background <bg.png>] [--music-intro <pcm>] [--music-loop <pcm>] [--skip-compile]

The data file layout (little endian):
  'WA2DCPK1', u32 version (2), u32 record count, u64 index offset; data from offset 64;
  index at the end:
    u8 kind (1 shipped, 2 from XAPK, 3 patcher asset, 4 delta, 5 raw bundle delta)
    kind 1/3: str path, u8 method (0 stored, 1 LZMS chunks), u64 size, u64 stored,
              u64 offset, u32 crc32
    kind 2:   u8 archive (1 APK, 2 OBB), str entry, str destination, u64 size, u32 crc32
    kind 4:   u8 archive, str entry, str destination, u64 target size, u32 source crc32,
              u32 target crc32, u8 method, u64 stored, u64 offset
    kind 5:   u8 archive, str entry, str destination, u64 target file size,
              u64 source raw size, u64 target raw size, u32 source raw crc32,
              u32 target raw crc32, u32 target file crc32, u32 header size,
              u32 file-info header size, u32 chunk count, u8 method, u64 stored,
              u64 offset; then per chunk: u64 source offset, u64 source length,
              u64 target offset, u64 target length, u64 delta length. The stored
              blob is the concatenation of the per-chunk msdelta bytes.
  str = u16 byte length + UTF-8. LZMS chunks: u32 raw, u32 packed, data (16 MB chunks).

Kind 5 is for UnityWeb (LZMA1) bundles: the delta is taken on the decompressed
payload (where a retag is only a few bytes) and the patcher re-encodes it with the
vendored xz/liblzma using UnityPy's exact settings. See web_pair() below.
"""
import argparse
import ctypes
import lzma
import os
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

from PIL import Image

CHUNK = 16 << 20
MAGIC = b"WA2DCPK1"


# ------------------------------------------------------------------ Windows APIs
class DELTA_INPUT(ctypes.Structure):
    _fields_ = [("lpStart", ctypes.c_void_p), ("uSize", ctypes.c_size_t), ("Editable", ctypes.c_int)]


class DELTA_OUTPUT(ctypes.Structure):
    _fields_ = [("lpStart", ctypes.c_void_p), ("uSize", ctypes.c_size_t)]


_msdelta = ctypes.WinDLL("msdelta")
CreateDeltaB = _msdelta.CreateDeltaB
CreateDeltaB.argtypes = [
    ctypes.c_int64, ctypes.c_int64, ctypes.c_int64,
    DELTA_INPUT, DELTA_INPUT, DELTA_INPUT, DELTA_INPUT, DELTA_INPUT,
    ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(DELTA_OUTPUT),
]
CreateDeltaB.restype = ctypes.c_int

_cab = ctypes.WinDLL("cabinet")
COMPRESS_ALGORITHM_LZMS = 5


def lzms(data):
    handle = ctypes.c_void_p()
    if not _cab.CreateCompressor(COMPRESS_ALGORITHM_LZMS, None, ctypes.byref(handle)):
        raise OSError("CreateCompressor failed")
    try:
        out = ctypes.create_string_buffer(len(data) + (len(data) >> 3) + 65536)
        written = ctypes.c_size_t()
        if not _cab.Compress(handle, data, ctypes.c_size_t(len(data)), out,
                             ctypes.c_size_t(len(out)), ctypes.byref(written)):
            raise OSError("Compress failed: %d" % ctypes.GetLastError())
        return out.raw[:written.value]
    finally:
        _cab.CloseCompressor(handle)


def delta_bytes(source, target):
    sb = ctypes.create_string_buffer(source, len(source))
    tb = ctypes.create_string_buffer(target, len(target))
    si = DELTA_INPUT(ctypes.cast(sb, ctypes.c_void_p), len(source), 0)
    ti = DELTA_INPUT(ctypes.cast(tb, ctypes.c_void_p), len(target), 0)
    empty = DELTA_INPUT(None, 0, 0)
    out = DELTA_OUTPUT()
    if not CreateDeltaB(1, 0, 0, si, ti, empty, empty, empty, None, 0, ctypes.byref(out)):
        raise OSError("CreateDeltaB failed: %d" % ctypes.GetLastError())
    try:
        return ctypes.string_at(out.lpStart, out.uSize)
    finally:
        _msdelta.DeltaFree(ctypes.c_void_p(out.lpStart))


# ------------------------------------------------------------------ helpers
def s(text):
    b = text.encode("utf-8")
    return struct.pack("<H", len(b)) + b


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def compress_blob(raw):
    """Return (method, stored_bytes). Prefer LZMS when it actually shrinks."""
    chunks = []
    for pos in range(0, max(len(raw), 1), CHUNK):
        part = raw[pos:pos + CHUNK]
        packed = lzms(part) if part else b""
        chunks.append(struct.pack("<II", len(part), len(packed)) + packed)
    packed_all = b"".join(chunks)
    if len(packed_all) < len(raw) and raw:
        return 1, packed_all
    return 0, raw


# ------------------------------------------------------------------ UnityWeb bundles
def read_cstr(data, pos):
    end = data.index(b"\0", pos)
    return data[pos:end], end + 1


def web_info(data):
    """Parse a UnityWeb v3 bundle header, or return None.

    UnityPy's BundleFile headers are big-endian after the signature/version
    strings; the LZMA stream has the usual little-endian props/dict/size."""
    try:
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
        pos += 8 * (count - 1)
        comp, _uncomp = struct.unpack_from(">II", data, pos)
        pos += 8
        complete = None
        fileinfo = None
        if version >= 2:
            complete = struct.unpack_from(">I", data, pos)[0]
            pos += 4
        if version >= 3:
            fileinfo = struct.unpack_from(">I", data, pos)[0]
            pos += 4
        if header_size < field_pos or header_size + comp > len(data) or comp < 13:
            return None
        props, dict_size, declared = struct.unpack_from("<BIQ", data, header_size)
        lc = props % 9
        rem = props // 9
        pb = rem // 5
        lp = rem % 5
        if props != 0x5D or dict_size != 0x800000:
            return None
        return {
            "version": version,
            "field_pos": field_pos,
            "prefix": data[:field_pos],
            "header": header_size,
            "comp": comp,
            "declared": declared,
            "complete": complete,
            "fileinfo": fileinfo,
            "props": props,
            "dict": dict_size,
            "lc": lc,
            "lp": lp,
            "pb": pb,
            "stream": data[header_size + 13:header_size + comp],
        }
    except (ValueError, struct.error):
        return None


def web_decode(data, info):
    dec = lzma.LZMADecompressor(
        format=lzma.FORMAT_RAW,
        filters=[{"id": lzma.FILTER_LZMA1, "dict_size": info["dict"],
                  "lc": info["lc"], "lp": info["lp"], "pb": info["pb"]}])
    return dec.decompress(info["stream"])


def web_pair(source, target):
    """Return (source_raw, target_raw, source_info, target_info) for a retagged
    UnityWeb bundle, or None if the headers are not compatible."""
    si = web_info(source)
    ti = web_info(target)
    if not si or not ti:
        return None
    if si["version"] != ti["version"] or si["field_pos"] != ti["field_pos"]:
        return None
    if si["prefix"] != ti["prefix"] or si["header"] != ti["header"]:
        return None
    if si["fileinfo"] != ti["fileinfo"]:
        return None
    source_raw = web_decode(source, si)
    target_raw = web_decode(target, ti)
    return source_raw, target_raw, si, ti


RAW_CHUNK = 24 << 20   # msdelta accepts at most 32 MiB per source/target buffer


def raw_chunks(source_raw, target_raw):
    """Split the compressed payload pair into <=24 MiB pieces and delta each.
    The retag shift is tiny, so msdelta handles the offset within a piece."""
    chunks = []
    so = to = 0
    while so < len(source_raw) or to < len(target_raw):
        sl = min(RAW_CHUNK, len(source_raw) - so)
        tl = min(RAW_CHUNK, len(target_raw) - to)
        if sl <= 0 or tl <= 0:
            raise SystemExit("raw bundle chunk mismatch")
        delta = delta_bytes(source_raw[so:so + sl], target_raw[to:to + tl])
        chunks.append((so, sl, to, tl, delta))
        so += sl
        to += tl
    return chunks


def find_icon(apk):
    names = [n for n in apk.namelist() if n.lower().endswith("app_icon.png")]
    if not names:
        names = [n for n in apk.namelist() if n.lower().endswith("/icon.png")]
    if not names:
        return None
    names.sort(key=lambda n: len(apk.read(n)), reverse=True)
    return names[0]


def write_icon(apk, path):
    name = find_icon(apk)
    if not name:
        return False
    from io import BytesIO
    image = Image.open(BytesIO(apk.read(name))).convert("RGBA")
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.save(path, sizes=sizes)
    return True


# ------------------------------------------------------------------ XAPK
class Xapk:
    def __init__(self, path, work):
        self.path = path
        self.work = work
        self.outer = zipfile.ZipFile(path, "r")
        self.apk_name = None
        self.obb_name = None
        for name in self.outer.namelist():
            low = name.lower()
            if low.endswith(".apk") and not self.apk_name:
                self.apk_name = name
            if low.endswith(".obb") and not self.obb_name:
                self.obb_name = name
        if not self.apk_name or not self.obb_name:
            raise SystemExit("not a WA2DC XAPK: missing the APK or OBB")
        os.makedirs(work, exist_ok=True)
        apk_path = os.path.join(work, "game.apk")
        obb_path = os.path.join(work, "game.obb")
        with self.outer.open(self.apk_name) as src, open(apk_path, "wb") as dst:
            shutil.copyfileobj(src, dst, 1 << 20)
        with self.outer.open(self.obb_name) as src, open(obb_path, "wb") as dst:
            shutil.copyfileobj(src, dst, 1 << 20)
        self.apk = zipfile.ZipFile(apk_path, "r")
        self.obb = zipfile.ZipFile(obb_path, "r")
        self.name = "WA2 Dream Communication"
        self.version = "1.0.7"
        try:
            import json
            manifest = json.loads(self.outer.read("manifest.json").decode("utf-8-sig"))
            self.name = manifest.get("name") or self.name
            self.version = manifest.get("version_name") or self.version
        except Exception:
            pass

    def info(self, archive, entry):
        z = self.apk if archive == 1 else self.obb
        try:
            return z.getinfo(entry)
        except KeyError:
            return None

    def read(self, archive, entry):
        z = self.apk if archive == 1 else self.obb
        return z.read(entry)

    def close(self):
        for z in (self.apk, self.obb, self.outer):
            try:
                z.close()
            except Exception:
                pass


# ------------------------------------------------------------------ classification
STOCK_EXACT = {
    "WA2DC.exe", "WA2DC.bat", "WA2DC-1080p.bat",
    "WA2DC_Data/Managed/mscorlib.dll", "WA2DC_Data/Managed/UnityEngine.dll",
    "WA2DC_Data/Resources/unity default resources",
    "WA2DC_Data/StreamingAssets/bg.png",
}
STOCK_PREFIXES = ("WA2DC_Data/Mono/",)


def source_for(rel, xapk):
    if rel in STOCK_EXACT or any(rel.startswith(p) for p in STOCK_PREFIXES):
        return None
    if rel.startswith("WA2DC_Data/StreamingAssets/"):
        name = rel[len("WA2DC_Data/StreamingAssets/"):]
        if name == "bg.png":
            return None
        return (2, name)
    if rel.startswith("WA2DC_Data/"):
        sub = rel[len("WA2DC_Data/"):]
        return (1, "assets/bin/Data/" + sub)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    parser.add_argument("--xapk", required=True)
    parser.add_argument("--out", default=os.path.join("dist", "WA2DC Patcher"))
    parser.add_argument("--patcher", default="patcher")
    parser.add_argument("--work", default=os.path.join("work", "patcher"))
    parser.add_argument("--background", default="")
    parser.add_argument("--music-intro", default="")
    parser.add_argument("--music-loop", default="")
    parser.add_argument("--skip-compile", action="store_true")
    args = parser.parse_args()

    build = os.path.abspath(args.build)
    out_dir = os.path.abspath(args.out)
    work = os.path.abspath(args.work)
    inner = os.path.join(work, "inner")
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(inner, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)

    xapk = Xapk(os.path.abspath(args.xapk), inner)
    print("XAPK: %s %s (%s / %s)" % (xapk.name, xapk.version, xapk.apk_name, xapk.obb_name))

    # build file list, skipping runtime artifacts that are never part of the game
    files = []
    for base, dirs, names in os.walk(build):
        dirs[:] = [d for d in dirs if d not in ("Documents",)]
        for name in names:
            path = os.path.join(base, name)
            rel = os.path.relpath(path, build).replace(os.sep, "/")
            if rel == "WA2DC_Data/output_log.txt" or rel.startswith("port-shot") or rel.endswith(".request"):
                continue
            files.append((rel, path))
    files.sort()

    records = []          # index records
    deltas = []           # (dest, target_size, delta_size, payload_size, kind)
    counts = {1: 0, 2: 0, 3: 0, 4: 0, 5: 0}

    with open(os.path.join(out_dir, "WA2DC_Patch.dat.tmp"), "wb") as dat:
        dat.write(b"\0" * 64)

        def add_blob(kind, path, raw, crc):
            method, stored = compress_blob(raw)
            offset = dat.tell()
            dat.write(stored)
            records.append(struct.pack("<B", kind) + s(path) +
                           struct.pack("<BQQQI", method, len(raw), len(stored), offset, crc))
            counts[kind] += 1
            return offset, stored

        # ---- patcher assets (GUI background, XAPK metadata/fingerprint, optional music)
        bg_path = os.path.join(build, "WA2DC_Data", "StreamingAssets", "bg.png")
        if not os.path.isfile(bg_path) and args.background:
            bg_path = os.path.abspath(args.background)
        bg_bytes = b""
        if os.path.isfile(bg_path):
            bg_bytes = open(bg_path, "rb").read()
            add_blob(3, "asset:bg.png", bg_bytes, crc32(bg_bytes))
            print("asset:bg.png from", bg_path)
        else:
            print("warning: no background art; the patcher will use its gradient theme")

        info = "name=%s\nversion=%s\napk_entry=%s\nobb_entry=%s\n" % (
            xapk.name, xapk.version, xapk.apk_name, xapk.obb_name)
        add_blob(3, "asset:xapk_info", info.encode("utf-8"), crc32(info.encode("utf-8")))

        fingerprint = []
        for archive, entry in ((1, "assets/bin/Data/mainData"),
                               (1, "assets/bin/Data/Managed/System.Core.dll"),
                               (1, "assets/bin/Data/level0"),
                               (2, "TITMN01A.unity3d")):
            info_entry = xapk.info(archive, entry)
            if info_entry:
                fingerprint.append("%d|%s|%d|%08X" % (archive, entry, info_entry.file_size, info_entry.CRC))
        if not fingerprint:
            raise SystemExit("could not build an XAPK fingerprint (required entries missing)")
        fp = ("\n".join(fingerprint) + "\n").encode("utf-8")
        add_blob(3, "asset:xapk_fingerprint", fp, crc32(fp))

        for option, name in (("music_intro", "music_a.pcm"), ("music_loop", "music_b.pcm")):
            path = getattr(args, option)
            if path:
                data = open(path, "rb").read()
                add_blob(3, "asset:" + name, data, crc32(data))
                print("asset:" + name, len(data), "bytes")

        # ---- shipped files and deltas
        for rel, path in files:
            data = open(path, "rb").read()
            target_crc = crc32(data)
            source = source_for(rel, xapk)
            if source is None:
                add_blob(1, rel.replace("/", "\\"), data, target_crc)
                print("ship  %-56s %10d" % (rel, len(data)))
                continue
            archive, entry = source
            info_entry = xapk.info(archive, entry)
            if info_entry is None:
                raise SystemExit("no XAPK source for %s (expected %s)" % (rel, entry))
            if info_entry.file_size == len(data) and info_entry.CRC == target_crc:
                dest = rel.replace("/", "\\")
                records.append(struct.pack("<BB", 2, archive) + s(entry) + s(dest) +
                               struct.pack("<QI", len(data), target_crc))
                counts[2] += 1
                continue
            source_data = xapk.read(archive, entry)
            dest = rel.replace("/", "\\")
            pair = web_pair(source_data, data)
            if pair:
                source_raw, target_raw, source_info, target_info = pair
                chunks = raw_chunks(source_raw, target_raw)
                blob = b"".join(c[4] for c in chunks)
                method, stored = compress_blob(blob)
                offset = dat.tell()
                dat.write(stored)
                records.append(struct.pack("<BB", 5, archive) + s(entry) + s(dest) +
                               struct.pack("<QQQIIIIIIBQQ",
                                           len(data), len(source_raw), len(target_raw),
                                           crc32(source_raw), crc32(target_raw), target_crc,
                                           source_info["header"], source_info["fileinfo"],
                                           len(chunks), method, len(stored), offset) +
                               b"".join(struct.pack("<QQQQQ", so, sl, to, tl, len(d))
                                        for so, sl, to, tl, d in chunks))
                counts[5] += 1
                deltas.append((rel, len(data), len(blob), len(target_raw), 5))
                print("web   %-56s raw %10d -> delta %8d (%d chunk%s)" % (
                    rel, len(target_raw), len(blob), len(chunks), "" if len(chunks) == 1 else "s"))
                continue
            delta = delta_bytes(source_data, data)
            method, stored = compress_blob(delta)
            offset = dat.tell()
            dat.write(stored)
            records.append(struct.pack("<BB", 4, archive) + s(entry) + s(dest) +
                           struct.pack("<QIIBQQ", len(data), info_entry.CRC, target_crc, method,
                                       len(stored), offset))
            counts[4] += 1
            deltas.append((rel, len(data), len(delta), len(data), 4))
            print("delta %-56s %10d -> %10d (%.0f%%)" % (rel, len(data), len(delta),
                                                         100.0 * len(delta) / max(len(data), 1)))

        index_offset = dat.tell()
        dat.write(b"".join(records))
        dat.seek(0)
        dat.write(MAGIC + struct.pack("<IIQ", 2, len(records), index_offset))

    dat_path = os.path.join(out_dir, "WA2DC_Patch.dat")
    os.replace(os.path.join(out_dir, "WA2DC_Patch.dat.tmp"), dat_path)
    dat_size = os.path.getsize(dat_path)

    print("\nrecords:", counts, "total", len(records))
    print("data file: %s (%.1f MB)" % (dat_path, dat_size / 1e6))
    if deltas:
        deltas.sort(key=lambda d: d[2], reverse=True)
        print("largest deltas:")
        for rel, target, delta, payload, kind in deltas[:12]:
            print("  %s %-56s file %10d delta %8d (%.2f%% of payload)" % (
                "web" if kind == 5 else "bin", rel, target, delta,
                100.0 * delta / max(payload, 1)))
        large = [d for d in deltas if d[2] > 0.5 * max(d[3], 1)]
        if large:
            print("deltas larger than half their payload:")
            for rel, target, delta, payload, kind in large:
                print("  %-56s %10d -> %10d" % (rel, payload, delta))

    if args.skip_compile:
        print("skip-compile: patcher was not built")
        return 0

    # ---- build the exe in a disposable source copy (the repo source stays clean)
    src = os.path.join(work, "src")
    if os.path.isdir(src):
        shutil.rmtree(src)
    shutil.copytree(os.path.abspath(args.patcher), src,
                    ignore=shutil.ignore_patterns("out", "*.ico", "*.obj", "*.res"))
    if not write_icon(xapk.apk, os.path.join(src, "game.ico")):
        print("warning: no APK icon found; the exe will use the default icon")
    print("building patcher...")
    bat = os.path.join(src, "build.bat")
    result = subprocess.run('call "%s"' % bat, cwd=src, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit("patcher build failed")
    exe_src = os.path.join(src, "out", "WA2DC Patcher.exe")
    if not os.path.isfile(exe_src):
        raise SystemExit("patcher build produced no exe")
    exe_dst = os.path.join(out_dir, "WA2DC Patcher.exe")
    shutil.copyfile(exe_src, exe_dst)
    shutil.copyfile(os.path.join(args.patcher, "Read me.txt"), os.path.join(out_dir, "Read me.txt"))
    print("exe: %s (%d bytes)" % (exe_dst, os.path.getsize(exe_dst)))
    xapk.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
