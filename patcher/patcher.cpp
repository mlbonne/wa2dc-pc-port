// WA2 Dream Communication (WA2DC) Windows patcher.
//
// Turns the owner's WA2DC 1.0.7 XAPK (a zip containing an APK and an OBB) into the
// finished Windows port: stock Unity 3.5.6f4 player files, game data retagged to the
// Windows target, the two recompiled assemblies, and the optional strip art.
//
// Files taken unchanged from the XAPK are copied out of the nested APK/OBB zips after a
// CRC32 check. Modified files ship as Windows msdelta deltas against the user's own
// copies (CreateDeltaB at build time, ApplyDeltaB here). The shipped player files,
// launchers and patches are embedded in this executable as RCDATA resources (see
// scripts/build_patcher.py), so there is no separate .dat file.
//
// UI: borderless GDI+ wizard, WHITE ALBUM2 Dream Communication strip art, falling snow,
// and an optional soundtrack with a mute toggle.
//
// Test mode: WA2DC Patcher.exe /auto <xapk> <dest>   (installs, then exits 0/1;
// the result and any error are written to patcher-auto.txt next to the exe)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <compressapi.h>
#include <msdelta.h>
#include <mmsystem.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>
#include <math.h>
#include <string>
#include <vector>
#include <map>
#include <initializer_list>
#include <algorithm>
#include "third_party/miniz/miniz.h"
#include "bundle_lzma.h"

#pragma comment(lib, "bcrypt.lib")

using namespace Gdiplus;
using std::wstring;
using std::string;
using std::vector;

// Resource IDs written by scripts/build_patcher.py.
#define IDR_PATCH_DATA 101
#define IDR_PATCH_SHA  102

static const wchar_t* kExeName = L"WA2DC.exe";
static const wchar_t* kTitle = L"WA2DC Patcher";

// ======================================================================= utilities
static string Narrow(const wstring& s)
{
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0, NULL, NULL);
    string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, NULL, NULL);
    return out;
}

static wstring Widen(const string& s, UINT cp = CP_UTF8)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), NULL, 0);
    wstring w(n, 0);
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static uint32_t g_crcTable[256];
static void CrcInit()
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crcTable[i] = c;
    }
}
static uint32_t Crc(uint32_t crc, const void* data, size_t n)
{
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    while (n--) crc = g_crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static wstring FormatSize(uint64_t b)
{
    wchar_t buf[64];
    if (b >= (1ull << 30)) swprintf_s(buf, L"%.1f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) swprintf_s(buf, L"%.0f MB", b / 1048576.0);
    else swprintf_s(buf, L"%.0f KB", b / 1024.0);
    return buf;
}

static bool WriteFileAll(HANDLE f, const void* d, DWORD n)
{
    DWORD w = 0;
    return WriteFile(f, d, n, &w, NULL) && w == n;
}

static bool PathExists(const wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool MakeDirs(const wstring& path)       // creates every folder of path
{
    for (size_t i = 3; i <= path.size(); i++) {
        if (i == path.size() || path[i] == L'\\') {
            wstring d = path.substr(0, i);
            if (!CreateDirectoryW(d.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
    }
    return true;
}

static void RemoveTree(const wstring& path)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            wstring p = path + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(path.c_str());
}

static bool DirEmpty(const wstring& path)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    bool empty = true;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wstring n = fd.cFileName;
            if (n != L"." && n != L"..") { empty = false; break; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return empty;
}

// ======================================================================= zip / XAPK
struct ZipArchive {
    mz_zip_archive zip;
    FILE* file = NULL;
    bool open = false;

    ZipArchive() { memset(&zip, 0, sizeof(zip)); }

    bool Open(const wstring& path)
    {
        Close();
        file = _wfopen(path.c_str(), L"rb");
        if (!file) return false;
        _fseeki64(file, 0, SEEK_END);
        mz_uint64 size = (mz_uint64)_ftelli64(file);
        _fseeki64(file, 0, SEEK_SET);
        if (!mz_zip_reader_init_cfile(&zip, file, size, 0)) {
            fclose(file); file = NULL;
            return false;
        }
        open = true;
        return true;
    }

    void Close()
    {
        if (open) {
            mz_zip_reader_end(&zip);
            open = false;
        }
        memset(&zip, 0, sizeof(zip));
        if (file) { fclose(file); file = NULL; }
    }

    int Find(const wstring& name)
    {
        if (!open) return -1;
        return mz_zip_reader_locate_file(&zip, Narrow(name).c_str(), NULL, 0);
    }

    bool Stat(int index, mz_zip_archive_file_stat& st)
    {
        return open && mz_zip_reader_file_stat(&zip, index, &st) != 0;
    }

    bool Read(const wstring& name, vector<uint8_t>& out)
    {
        int index = Find(name);
        if (index < 0) return false;
        mz_zip_archive_file_stat st;
        if (!Stat(index, st)) return false;
        out.resize((size_t)st.m_uncomp_size);
        if (!out.empty() && !mz_zip_reader_extract_to_mem(&zip, index, out.data(), out.size(), 0)) return false;
        return true;
    }
};

struct ExtractSink {
    HANDLE file;
    uint32_t crc;
    uint64_t done;
    uint64_t total;
    volatile bool* cancel;
};

static size_t ExtractCallback(void* opaque, mz_uint64 fileOfs, const void* buf, size_t n)
{
    (void)fileOfs;
    ExtractSink* sink = (ExtractSink*)opaque;
    if (sink->cancel && *sink->cancel) return 0;
    if (!WriteFileAll(sink->file, buf, (DWORD)n)) return 0;
    sink->crc = Crc(sink->crc, buf, n);
    sink->done += n;
    return n;
}

static vector<uint8_t> LoadAsset(const wchar_t* name);

struct Xapk {
    ZipArchive outer, apk, obb;
    wstring cacheDir, apkPath, obbPath;
    wstring infoName = L"WA2 Dream Communication";
    wstring infoVersion;
    bool open = false;

    void Close()
    {
        apk.Close();
        obb.Close();
        outer.Close();
        if (!cacheDir.empty()) {
            RemoveTree(cacheDir);
            cacheDir.clear();
        }
        open = false;
    }

    bool Open(const wstring& xapkPath, wstring& error);
    bool Validate(wstring& status);
    bool ReadSource(int archive, const wstring& entry, vector<uint8_t>& out, wstring& error);
    bool ExtractSource(int archive, const wstring& entry, const wstring& dest, uint64_t expectedSize,
                       uint32_t expectedCrc, volatile bool* cancel, wstring& error);
    bool StatSource(int archive, const wstring& entry, uint64_t& size, uint32_t& crc);
};

bool Xapk::Open(const wstring& xapkPath, wstring& error)
{
    Close();
    wchar_t temp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, temp);
    if (!n) { error = L"Could not find the temporary folder."; return false; }
    cacheDir = wstring(temp) + L"wa2dc-patcher-" + std::to_wstring(GetCurrentProcessId());
    if (!MakeDirs(cacheDir)) { error = L"Could not create the temporary folder."; return false; }
    if (!outer.Open(xapkPath)) { error = L"The XAPK is missing or is not a zip file."; return false; }

    // the optional xapk_info asset records the inner archive names; otherwise scan by extension
    wstring apkEntry, obbEntry;
    vector<uint8_t> info = LoadAsset(L"xapk_info");
    if (!info.empty()) {
        string text((const char*)info.data(), info.size());
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == string::npos) end = text.size();
            string line = text.substr(pos, end - pos);
            pos = end + 1;
            size_t eq = line.find('=');
            if (eq == string::npos) continue;
            string key = line.substr(0, eq), value = line.substr(eq + 1);
            if (key == "apk_entry") apkEntry = Widen(value);
            else if (key == "obb_entry") obbEntry = Widen(value);
            else if (key == "name") infoName = Widen(value);
            else if (key == "version") infoVersion = Widen(value);
        }
    }
    if (apkEntry.empty() || obbEntry.empty()) {
        mz_uint count = mz_zip_reader_get_num_files(&outer.zip);
        for (mz_uint i = 0; i < count; i++) {
            mz_zip_archive_file_stat st;
            if (!outer.Stat(i, st)) continue;
            wstring name = Widen(st.m_filename);
            wstring lower = name;
            for (auto& c : lower) c = towlower(c);
            if (apkEntry.empty() && lower.size() > 4 && lower.substr(lower.size() - 4) == L".apk") apkEntry = name;
            if (obbEntry.empty() && lower.size() > 4 && lower.substr(lower.size() - 4) == L".obb") obbEntry = name;
        }
    }
    if (apkEntry.empty() || obbEntry.empty()) {
        error = L"This XAPK does not contain an APK and an OBB.\nIt is not a WA2 Dream Communication XAPK.";
        return false;
    }

    apkPath = cacheDir + L"\\game.apk";
    obbPath = cacheDir + L"\\game.obb";
    int apkIndex = outer.Find(apkEntry);
    int obbIndex = outer.Find(obbEntry);
    if (apkIndex < 0 || obbIndex < 0) {
        error = L"The XAPK is missing " + (apkIndex < 0 ? apkEntry : obbEntry) + L".";
        return false;
    }
    mz_zip_archive_file_stat apkStat, obbStat;
    if (!outer.Stat(apkIndex, apkStat) || !outer.Stat(obbIndex, obbStat)) {
        error = L"The XAPK's inner archives could not be read.";
        return false;
    }
    ULARGE_INTEGER freeTemp;
    uint64_t cacheNeed = (uint64_t)apkStat.m_uncomp_size + (uint64_t)obbStat.m_uncomp_size + (128ull << 20);
    if (GetDiskFreeSpaceExW(cacheDir.c_str(), &freeTemp, NULL, NULL) && freeTemp.QuadPart < cacheNeed) {
        error = L"Not enough temporary disk space for the XAPK's inner archives.\nNeed " +
                FormatSize(cacheNeed) + L"; " + FormatSize(freeTemp.QuadPart) + L" are free.";
        return false;
    }
    {
        mz_zip_archive_file_stat st = apkStat;
        error.clear();
        MakeDirs(cacheDir);
        HANDLE out = CreateFileW(apkPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (out == INVALID_HANDLE_VALUE) { error = L"Could not write the temporary APK."; return false; }
        ExtractSink sink = { out, 0, 0, (uint64_t)st.m_uncomp_size, NULL };
        bool ok = mz_zip_reader_extract_to_callback(&outer.zip, apkIndex, ExtractCallback, &sink, 0) != 0;
        CloseHandle(out);
        if (!ok || sink.crc != st.m_crc32) { error = L"The XAPK's APK is damaged (" + apkEntry + L")."; return false; }
    }
    {
        mz_zip_archive_file_stat st = obbStat;
        HANDLE out = CreateFileW(obbPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (out == INVALID_HANDLE_VALUE) { error = L"Could not write the temporary OBB."; return false; }
        ExtractSink sink = { out, 0, 0, (uint64_t)st.m_uncomp_size, NULL };
        bool ok = mz_zip_reader_extract_to_callback(&outer.zip, obbIndex, ExtractCallback, &sink, 0) != 0;
        CloseHandle(out);
        if (!ok || sink.crc != st.m_crc32) { error = L"The XAPK's OBB is damaged (" + obbEntry + L")."; return false; }
    }
    if (!apk.Open(apkPath) || !obb.Open(obbPath)) {
        error = L"The XAPK's inner APK/OBB could not be opened.";
        return false;
    }
    open = true;
    return true;
}

bool Xapk::StatSource(int archive, const wstring& entry, uint64_t& size, uint32_t& crc)
{
    ZipArchive& zip = archive == 1 ? apk : obb;
    int index = zip.Find(entry);
    if (index < 0) return false;
    mz_zip_archive_file_stat st;
    if (!zip.Stat(index, st)) return false;
    size = st.m_uncomp_size;
    crc = st.m_crc32;
    return true;
}

bool Xapk::ReadSource(int archive, const wstring& entry, vector<uint8_t>& out, wstring& error)
{
    ZipArchive& zip = archive == 1 ? apk : obb;
    int index = zip.Find(entry);
    if (index < 0) {
        error = L"The XAPK is missing " + entry + L".";
        return false;
    }
    mz_zip_archive_file_stat st;
    if (!zip.Stat(index, st) || !zip.Read(entry, out)) {
        error = L"The XAPK entry could not be read (" + entry + L").";
        return false;
    }
    if (out.size() != st.m_uncomp_size || Crc(0, out.data(), out.size()) != st.m_crc32) {
        error = L"The XAPK entry failed its CRC (" + entry + L").";
        return false;
    }
    return true;
}

bool Xapk::ExtractSource(int archive, const wstring& entry, const wstring& dest, uint64_t expectedSize,
                         uint32_t expectedCrc, volatile bool* cancel, wstring& error)
{
    ZipArchive& zip = archive == 1 ? apk : obb;
    int index = zip.Find(entry);
    if (index < 0) {
        error = L"The XAPK is missing " + entry + L".";
        return false;
    }
    mz_zip_archive_file_stat st;
    if (!zip.Stat(index, st)) {
        error = L"The XAPK entry could not be read (" + entry + L").";
        return false;
    }
    if (st.m_uncomp_size != expectedSize || st.m_crc32 != expectedCrc) {
        error = L"The XAPK entry does not match the expected data (" + entry + L").";
        return false;
    }
    MakeDirs(dest.substr(0, dest.rfind(L'\\')));
    HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (out == INVALID_HANDLE_VALUE) { error = L"Could not write\n" + dest; return false; }
    ExtractSink sink = { out, 0, 0, expectedSize, cancel };
    bool ok = mz_zip_reader_extract_to_callback(&zip.zip, index, ExtractCallback, &sink, 0) != 0;
    CloseHandle(out);
    if (!ok || sink.crc != expectedCrc || sink.done != expectedSize) {
        DeleteFileW(dest.c_str());
        error = cancel && *cancel ? L"Cancelled." : L"The XAPK entry failed its CRC (" + entry + L").";
        return false;
    }
    return true;
}

bool Xapk::Validate(wstring& status)
{
    if (!open) { status = L"The XAPK is not open."; return false; }
    vector<uint8_t> data = LoadAsset(L"xapk_fingerprint");
    if (data.empty()) { status = L"The patcher data is missing its XAPK fingerprint."; return false; }
    string text((const char*)data.data(), data.size());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == string::npos) end = text.size();
        string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty()) continue;
        // archive|entry|size|crc
        size_t p1 = line.find('|');
        size_t p2 = p1 == string::npos ? string::npos : line.find('|', p1 + 1);
        size_t p3 = p2 == string::npos ? string::npos : line.find('|', p2 + 1);
        if (p3 == string::npos) continue;
        int archive = atoi(line.substr(0, p1).c_str());
        wstring entry = Widen(line.substr(p1 + 1, p2 - p1 - 1));
        uint64_t size = _strtoui64(line.substr(p2 + 1, p3 - p2 - 1).c_str(), NULL, 10);
        uint32_t crc = (uint32_t)strtoul(line.substr(p3 + 1).c_str(), NULL, 16);
        uint64_t actualSize = 0;
        uint32_t actualCrc = 0;
        if (!StatSource(archive, entry, actualSize, actualCrc) || actualSize != size || actualCrc != crc) {
            status = L"This is not the expected WA2DC XAPK (entry " + entry + L" failed its CRC).";
            return false;
        }
    }
    status = infoName + (infoVersion.empty() ? L"" : L" " + infoVersion) + L" - XAPK verified";
    return true;
}

// ======================================================================= patch data
struct RawChunk {
    uint64_t srcOff = 0, srcLen = 0, trgOff = 0, trgLen = 0, deltaLen = 0;
};

struct Record {
    int kind = 0;               // 1 shipped, 2 from XAPK, 3 patcher asset, 4 delta, 5 raw bundle delta
    wstring path;               // kind 1/3
    int method = 0;
    uint64_t size = 0, stored = 0, offset = 0;
    uint32_t crc = 0;
    int archive = 0;            // kind 2/4/5: 1 APK, 2 OBB
    wstring entry, dest;
    uint32_t sourceCrc = 0, targetCrc = 0;
    // kind 5: chunked delta on the decompressed UnityWeb payload
    uint64_t sourceRawSize = 0, targetRawSize = 0;
    uint32_t sourceRawCrc = 0, targetRawCrc = 0, targetFileCrc = 0;
    uint32_t headerSize = 0, fileInfoHeaderSize = 0;
    vector<RawChunk> chunks;
};

static const uint8_t* g_data = NULL;
static DWORD g_dataSize = 0;
static vector<Record> g_records;
static wstring g_exeDir;
static Xapk g_xapk;

static bool Sha256(const uint8_t* data, DWORD size, uint8_t out[32])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
            ok = BCryptHashData(hash, (PUCHAR)data, size, 0) == 0 &&
                 BCryptFinishHash(hash, out, 32, 0) == 0;
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return ok;
}

static bool ReadDat(uint64_t off, void* buf, DWORD n)
{
    if (!g_data || off > g_dataSize || (uint64_t)n > (uint64_t)g_dataSize - off) return false;
    memcpy(buf, g_data + off, n);
    return true;
}

static bool ReadStr(const uint8_t*& p, const uint8_t* end, wstring& out)
{
    if ((size_t)(end - p) < 2) return false;
    uint16_t n = *(const uint16_t*)p; p += 2;
    if ((size_t)(end - p) < n) return false;
    out = Widen(string((const char*)p, n));
    p += n;
    return true;
}

static bool LoadData()
{
    HMODULE self = GetModuleHandleW(NULL);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(IDR_PATCH_DATA), RT_RCDATA);
    if (!res) return false;
    DWORD size = SizeofResource(self, res);
    HGLOBAL handle = LoadResource(self, res);
    if (!handle || size < 24) return false;
    const uint8_t* data = (const uint8_t*)LockResource(handle);
    if (!data) return false;

    HRSRC shaRes = FindResourceW(self, MAKEINTRESOURCEW(IDR_PATCH_SHA), RT_RCDATA);
    if (!shaRes) return false;
    DWORD shaLen = SizeofResource(self, shaRes);
    HGLOBAL shaHandle = LoadResource(self, shaRes);
    const uint8_t* expected = shaHandle ? (const uint8_t*)LockResource(shaHandle) : NULL;
    if (!expected || shaLen != 32) return false;
    uint8_t actual[32];
    if (!Sha256(data, size, actual) || memcmp(actual, expected, 32) != 0) return false;

    if (memcmp(data, "WA2DCPK1", 8)) return false;
    uint32_t count = *(const uint32_t*)(data + 12);
    uint64_t idx = *(const uint64_t*)(data + 16);
    if (idx < 24 || idx > size || count > 100000) return false;
    g_data = data;
    g_dataSize = size;

    const uint8_t* p = data + idx;
    const uint8_t* end = data + size;
    auto need = [&](size_t n) { return (size_t)(end - p) >= n; };
    for (uint32_t i = 0; i < count; i++) {
        Record r;
        if (!need(1)) return false;
        r.kind = *p++;
        if (r.kind == 2) {
            if (!need(1)) return false;
            r.archive = *p++;
            if (!ReadStr(p, end, r.entry) || !ReadStr(p, end, r.dest) || !need(12)) return false;
            r.size = *(const uint64_t*)p; p += 8;
            r.crc = *(const uint32_t*)p; p += 4;
        } else if (r.kind == 4) {
            if (!need(1)) return false;
            r.archive = *p++;
            if (!ReadStr(p, end, r.entry) || !ReadStr(p, end, r.dest) || !need(33)) return false;
            r.size = *(const uint64_t*)p; p += 8;       // target size
            r.sourceCrc = *(const uint32_t*)p; p += 4;
            r.targetCrc = *(const uint32_t*)p; p += 4;
            r.method = *p++;
            r.stored = *(const uint64_t*)p; p += 8;
            r.offset = *(const uint64_t*)p; p += 8;
        } else if (r.kind == 5) {
            if (!need(1)) return false;
            r.archive = *p++;
            if (!ReadStr(p, end, r.entry) || !ReadStr(p, end, r.dest) || !need(65)) return false;
            r.size = *(const uint64_t*)p; p += 8;       // target file size
            r.sourceRawSize = *(const uint64_t*)p; p += 8;
            r.targetRawSize = *(const uint64_t*)p; p += 8;
            r.sourceRawCrc = *(const uint32_t*)p; p += 4;
            r.targetRawCrc = *(const uint32_t*)p; p += 4;
            r.targetFileCrc = *(const uint32_t*)p; p += 4;
            r.headerSize = *(const uint32_t*)p; p += 4;
            r.fileInfoHeaderSize = *(const uint32_t*)p; p += 4;
            uint32_t chunkCount = *(const uint32_t*)p; p += 4;
            r.method = *p++;
            r.stored = *(const uint64_t*)p; p += 8;
            r.offset = *(const uint64_t*)p; p += 8;
            if (chunkCount == 0 || chunkCount > 16 || !need((size_t)chunkCount * 40)) return false;
            for (uint32_t c = 0; c < chunkCount; c++) {
                RawChunk chunk;
                chunk.srcOff = *(const uint64_t*)p; p += 8;
                chunk.srcLen = *(const uint64_t*)p; p += 8;
                chunk.trgOff = *(const uint64_t*)p; p += 8;
                chunk.trgLen = *(const uint64_t*)p; p += 8;
                chunk.deltaLen = *(const uint64_t*)p; p += 8;
                r.chunks.push_back(chunk);
            }
        } else {
            if (!ReadStr(p, end, r.path) || !need(29)) return false;
            r.method = *p++;
            r.size = *(const uint64_t*)p; p += 8;
            r.stored = *(const uint64_t*)p; p += 8;
            r.offset = *(const uint64_t*)p; p += 8;
            r.crc = *(const uint32_t*)p; p += 4;
        }
        g_records.push_back(r);
    }
    return true;
}

// Streams one shipped/asset record (stored or LZMS chunks) to a callback.
template <class F> static bool UnpackRecord(const Record& r, F sink)
{
    const DWORD BUF = 16u << 20;
    if (r.method == 0) {
        vector<uint8_t> buf((size_t)std::min<uint64_t>(BUF, std::max<uint64_t>(r.size, 1)));
        uint64_t pos = 0;
        while (pos < r.size) {
            DWORD n = (DWORD)std::min<uint64_t>(buf.size(), r.size - pos);
            if (!ReadDat(r.offset + pos, buf.data(), n)) return false;
            if (!sink(buf.data(), n)) return false;
            pos += n;
        }
        return true;
    }
    DECOMPRESSOR_HANDLE dh;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, NULL, &dh)) return false;
    vector<uint8_t> in, out;
    uint64_t pos = r.offset, end = r.offset + r.stored;
    bool ok = true;
    while (ok && pos < end) {
        uint32_t h[2];
        if (!ReadDat(pos, h, 8)) { ok = false; break; }
        pos += 8;
        in.resize(h[1]); out.resize(h[0]);
        SIZE_T got = 0;
        ok = ReadDat(pos, in.data(), h[1]) &&
             Decompress(dh, in.data(), h[1], out.data(), h[0], &got) && got == h[0] && sink(out.data(), h[0]);
        pos += h[1];
    }
    CloseDecompressor(dh);
    return ok;
}

static bool LoadBlob(const Record& r, vector<uint8_t>& out)
{
    out.clear();
    if (r.method == 0) {
        out.resize((size_t)r.stored);
        return out.empty() || ReadDat(r.offset, out.data(), (DWORD)out.size());
    }
    DECOMPRESSOR_HANDLE dh;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, NULL, &dh)) return false;
    vector<uint8_t> in, chunk;
    uint64_t pos = r.offset, end = r.offset + r.stored;
    bool ok = true;
    while (ok && pos < end) {
        uint32_t h[2];
        if (!ReadDat(pos, h, 8)) { ok = false; break; }
        pos += 8;
        in.resize(h[1]); chunk.resize(h[0]);
        SIZE_T got = 0;
        ok = ReadDat(pos, in.data(), h[1]) &&
             Decompress(dh, in.data(), h[1], chunk.data(), h[0], &got) && got == h[0];
        if (ok) out.insert(out.end(), chunk.begin(), chunk.end());
        pos += h[1];
    }
    CloseDecompressor(dh);
    return ok;
}

// ======================================================================= UnityWeb raw deltas
// A UnityWeb v3 bundle is:
//   "UnityWeb\0", u32 BE version, player\0, engine\0,
//   u32 BE minimumStreamedBytes, headerSize, levelsToDownload, levelCount,
//   u32 BE compressedSize, uncompressedSize, completeFileSize, fileInfoHeaderSize,
//   padding to headerSize, then an LZMA1 stream:
//   0x5D, u32 LE dict (8 MiB), u64 LE uncompressed size, raw LZMA1 data.
// The retag only changes a few bytes of the decompressed payload, so kind 5 stores
// the msdelta of the payload and the patcher re-encodes it with UnityPy's settings.
struct WebHeader {
    size_t fieldPos = 0, dataOff = 0;
    uint32_t version = 0, headerSize = 0, levels = 0, levelCount = 0;
    uint32_t compSize = 0, completeSize = 0, fileInfoHeaderSize = 0;
    uint32_t dictSize = 0;
    int lc = 3, lp = 0, pb = 2;
};

static uint32_t Be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t Le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t Le64(const uint8_t* p)
{
    return (uint64_t)Le32(p) | ((uint64_t)Le32(p + 4) << 32);
}

static void PutBe32(vector<uint8_t>& v, uint32_t x)
{
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
}

static void PutLe32(vector<uint8_t>& v, uint32_t x)
{
    v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 24));
}

static void PutLe64(vector<uint8_t>& v, uint64_t x)
{
    PutLe32(v, (uint32_t)x); PutLe32(v, (uint32_t)(x >> 32));
}

static bool ReadCStr(const vector<uint8_t>& d, size_t& pos, string& out)
{
    size_t start = pos;
    while (pos < d.size() && d[pos] != 0) pos++;
    if (pos >= d.size()) return false;
    out.assign((const char*)d.data() + start, pos - start);
    pos++;
    return true;
}

static bool ParseWebHeader(const vector<uint8_t>& d, WebHeader& h)
{
    size_t pos = 0;
    string sig, player, engine;
    if (!ReadCStr(d, pos, sig) || sig != "UnityWeb") return false;
    if (d.size() < pos + 4) return false;
    h.version = Be32(d.data() + pos); pos += 4;
    if (h.version != 3) return false;
    if (!ReadCStr(d, pos, player) || !ReadCStr(d, pos, engine)) return false;
    h.fieldPos = pos;
    if (d.size() < pos + 32) return false;
    pos += 4;                                   // minimumStreamedBytes
    h.headerSize = Be32(d.data() + pos); pos += 4;
    h.levels = Be32(d.data() + pos); pos += 4;
    h.levelCount = Be32(d.data() + pos); pos += 4;
    h.compSize = Be32(d.data() + pos); pos += 4;
    pos += 4;                                   // uncompressedSize
    h.completeSize = Be32(d.data() + pos); pos += 4;
    h.fileInfoHeaderSize = Be32(d.data() + pos); pos += 4;
    if (h.levelCount != 1 || h.levels != 1) return false;
    if (h.headerSize < h.fieldPos + 32 || h.headerSize > d.size()) return false;
    if (h.compSize < 13 || (uint64_t)h.headerSize + h.compSize > d.size()) return false;
    h.dataOff = h.headerSize;
    uint8_t props = d[h.dataOff];
    h.dictSize = Le32(d.data() + h.dataOff + 1);
    h.lc = props % 9;
    int rem = props / 9;
    h.pb = rem / 5;
    h.lp = rem % 5;
    return h.dictSize > 0;
}

static bool BuildWebFile(const vector<uint8_t>& source, const WebHeader& h, const Record& r,
                         const vector<uint8_t>& stream, vector<uint8_t>& out)
{
    if (h.fieldPos == 0 || h.fieldPos + 32 > r.headerSize) return false;
    uint64_t comp = 13 + stream.size();
    uint64_t total = (uint64_t)r.headerSize + comp;
    if (total > 0xFFFFFFFFull || r.targetRawSize > 0xFFFFFFFFull) return false;
    out.clear();
    out.insert(out.end(), source.begin(), source.begin() + h.fieldPos);
    PutBe32(out, (uint32_t)total);              // minimumStreamedBytes
    PutBe32(out, r.headerSize);
    PutBe32(out, 1);                            // levelsToDownloadBeforeStreaming
    PutBe32(out, 1);                            // levelCount
    PutBe32(out, (uint32_t)comp);               // compressedSize
    PutBe32(out, (uint32_t)r.targetRawSize);    // uncompressedSize
    PutBe32(out, (uint32_t)total);              // completeFileSize
    PutBe32(out, r.fileInfoHeaderSize);
    if (out.size() > r.headerSize) return false;
    out.resize(r.headerSize, 0);
    out.push_back(0x5D);
    PutLe32(out, 0x800000);
    PutLe64(out, r.targetRawSize);
    out.insert(out.end(), stream.begin(), stream.end());
    return true;
}

static bool ApplyRawBundleRecord(const Record& r, const wstring& dst, wstring& error)
{
    vector<uint8_t> source;
    if (!g_xapk.ReadSource(r.archive, r.entry, source, error)) return false;
    WebHeader h;
    if (!ParseWebHeader(source, h)) {
        error = L"The XAPK bundle header is not supported (" + r.entry + L").";
        return false;
    }
    if (h.headerSize != r.headerSize || h.fileInfoHeaderSize != r.fileInfoHeaderSize) {
        error = L"The XAPK bundle layout does not match (" + r.entry + L").";
        return false;
    }
    vector<uint8_t> rawSource;
    if (!LzmaDecodeRaw(source.data() + h.dataOff + 13, h.compSize - 13, h.dictSize,
                       h.lc, h.lp, h.pb, rawSource)) {
        error = L"Could not decompress the XAPK bundle (" + r.entry + L").";
        return false;
    }
    if (rawSource.size() != r.sourceRawSize ||
        Crc(0, rawSource.data(), rawSource.size()) != r.sourceRawCrc) {
        error = L"The XAPK bundle does not match the expected payload (" + r.entry + L").";
        return false;
    }
    vector<uint8_t> delta;
    if (!LoadBlob(r, delta)) { error = L"The delta data is damaged (" + r.dest + L")."; return false; }
    vector<uint8_t> rawTarget;
    rawTarget.reserve((size_t)r.targetRawSize);
    size_t deltaPos = 0;
    for (const RawChunk& c : r.chunks) {
        if (c.srcOff > rawSource.size() || c.srcLen > rawSource.size() - c.srcOff ||
            c.trgOff != rawTarget.size() || deltaPos > delta.size() ||
            c.deltaLen > delta.size() - deltaPos) {
            error = L"The bundle delta is damaged (" + r.dest + L").";
            return false;
        }
        DELTA_INPUT si; si.lpcStart = rawSource.data() + c.srcOff; si.uSize = (SIZE_T)c.srcLen; si.Editable = FALSE;
        DELTA_INPUT di; di.lpcStart = delta.data() + deltaPos; di.uSize = (SIZE_T)c.deltaLen; di.Editable = FALSE;
        DELTA_OUTPUT out; out.lpStart = NULL; out.uSize = 0;
        if (!ApplyDeltaB(0, si, di, &out) || out.uSize != c.trgLen) {
            if (out.lpStart) DeltaFree(out.lpStart);
            error = L"Could not apply the bundle delta for " + r.dest + L".";
            return false;
        }
        rawTarget.insert(rawTarget.end(), (uint8_t*)out.lpStart, (uint8_t*)out.lpStart + out.uSize);
        DeltaFree(out.lpStart);
        deltaPos += (size_t)c.deltaLen;
    }
    if (deltaPos != delta.size() || rawTarget.size() != r.targetRawSize ||
        Crc(0, rawTarget.data(), rawTarget.size()) != r.targetRawCrc) {
        error = L"The patched bundle did not match (" + r.dest + L").";
        return false;
    }

    vector<uint8_t> stream;
    if (!LzmaEncodeRaw(rawTarget.data(), rawTarget.size(), stream)) {
        error = L"Could not re-encode the bundle (" + r.dest + L").";
        return false;
    }
    vector<uint8_t> file;
    if (!BuildWebFile(source, h, r, stream, file) ||
        file.size() != r.size || Crc(0, file.data(), file.size()) != r.targetFileCrc) {
        error = L"The rebuilt bundle did not match (" + r.dest + L").";
        return false;
    }
    HANDLE f = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) { error = L"Could not write\n" + dst; return false; }
    bool ok = WriteFileAll(f, file.data(), (DWORD)file.size());
    CloseHandle(f);
    if (!ok) { DeleteFileW(dst.c_str()); error = L"Could not write\n" + dst; return false; }
    return true;
}

static vector<uint8_t> LoadAsset(const wchar_t* name)
{
    vector<uint8_t> out;
    wstring want = wstring(L"asset:") + name;
    for (auto& r : g_records) {
        if (r.kind == 3 && r.path == want) {
            out.reserve((size_t)r.size);
            UnpackRecord(r, [&](const uint8_t* d, DWORD n) { out.insert(out.end(), d, d + n); return true; });
        }
    }
    return out;
}

static Bitmap* LoadPng(const wchar_t* name)
{
    vector<uint8_t> d = LoadAsset(name);
    if (d.empty()) return NULL;
    IStream* s = SHCreateMemStream(d.data(), (UINT)d.size());
    if (!s) return NULL;
    Bitmap* b = Bitmap::FromStream(s);
    s->Release();
    if (b && b->GetLastStatus() != Ok) { delete b; b = NULL; }
    return b;
}

// ======================================================================= music
static HWAVEOUT g_wave;
static vector<int16_t> g_intro, g_loop;
static size_t g_playPos;
static bool g_inLoop, g_muted;
static WAVEHDR g_hdr[4];
static vector<int16_t> g_bufs[4];
static HANDLE g_waveEvent;
static volatile bool g_quitAudio;

static void FillBuffer(int i)
{
    const size_t frames = 24000;
    vector<int16_t>& b = g_bufs[i];
    b.resize(frames * 2);
    for (size_t k = 0; k < frames * 2; k++) {
        vector<int16_t>& src = g_inLoop ? g_loop : g_intro;
        if (src.empty()) { b[k] = 0; continue; }
        if (g_playPos >= src.size()) { g_inLoop = true; g_playPos = 0; }
        vector<int16_t>& s2 = g_inLoop ? g_loop : g_intro;
        b[k] = s2.empty() ? 0 : s2[g_playPos++];
    }
    WAVEHDR& h = g_hdr[i];
    if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wave, &h, sizeof(h));
    memset(&h, 0, sizeof(h));
    h.lpData = (LPSTR)b.data();
    h.dwBufferLength = (DWORD)(b.size() * 2);
    waveOutPrepareHeader(g_wave, &h, sizeof(h));
    waveOutWrite(g_wave, &h, sizeof(h));
}

static DWORD WINAPI AudioThread(LPVOID)
{
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 48000, 48000 * 4, 4, 16, 0 };
    if (waveOutOpen(&g_wave, WAVE_MAPPER, &wf, (DWORD_PTR)g_waveEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return 0;
    waveOutSetVolume(g_wave, 0xB000B000);
    for (int i = 0; i < 4; i++) FillBuffer(i);
    while (!g_quitAudio) {
        WaitForSingleObject(g_waveEvent, 200);
        for (int i = 0; i < 4; i++)
            if (g_hdr[i].dwFlags & WHDR_DONE) FillBuffer(i);
    }
    waveOutReset(g_wave);
    for (int i = 0; i < 4; i++) waveOutUnprepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
    waveOutClose(g_wave);
    return 0;
}

static void StartMusic()
{
    vector<uint8_t> a = LoadAsset(L"music_a.pcm"), b = LoadAsset(L"music_b.pcm");
    g_intro.assign((int16_t*)a.data(), (int16_t*)(a.data() + a.size()));
    g_loop.assign((int16_t*)b.data(), (int16_t*)(b.data() + b.size()));
    if (g_intro.empty() && g_loop.empty()) return;
    if (g_intro.empty()) g_inLoop = true;
    g_waveEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, AudioThread, NULL, 0, NULL));
}

static void SetMuted(bool m)
{
    g_muted = m;
    if (g_wave) waveOutSetVolume(g_wave, m ? 0 : 0xB000B000);
}

// ======================================================================= install
enum Page { P_XAPK, P_DEST, P_INSTALL, P_DONE, P_ERROR };
static int g_page = P_XAPK;
static wstring g_xapkPath, g_destDir, g_error, g_curFile;
static wstring g_xapkStatus;
static bool g_xapkOk;
static bool g_dataOk = true;
static wstring g_dataError;
static bool g_useArt = true, g_use1080 = true;
static volatile LONG64 g_doneBytes, g_totalBytes;
static volatile bool g_cancel, g_installing;
static CRITICAL_SECTION g_cs;
static DWORD g_installStart;
static bool g_auto;
static bool g_hasRawBundles;
static int g_exitCode = 1;
static bool g_shortcutMade;
static wstring g_stage;

static uint64_t TotalBytes()
{
    uint64_t total = 0;
    for (auto& r : g_records) {
        if (r.kind == 3) continue;
        if (r.kind == 1) {
            if (!g_useArt && r.path == L"WA2DC_Data\\StreamingAssets\\bg.png") continue;
            if (!g_use1080 && r.path == L"WA2DC-1080p.bat") continue;
            total += r.size;
        } else if (r.kind == 2) {
            total += r.size;
        } else if (r.kind == 4 || r.kind == 5) {
            total += r.size;
        }
    }
    return total;
}

static void SetCur(const wstring& s) { EnterCriticalSection(&g_cs); g_curFile = s; LeaveCriticalSection(&g_cs); }

static bool Fail(const wstring& msg)
{
    EnterCriticalSection(&g_cs); g_error = msg; LeaveCriticalSection(&g_cs);
    return false;
}

static bool ShouldSkip(const Record& r)
{
    if (!g_useArt && r.kind == 1 && r.path == L"WA2DC_Data\\StreamingAssets\\bg.png") return true;
    if (!g_use1080 && r.kind == 1 && r.path == L"WA2DC-1080p.bat") return true;
    return false;
}

static bool ApplyDeltaRecord(const Record& r, const wstring& dst, wstring& error)
{
    vector<uint8_t> source;
    if (!g_xapk.ReadSource(r.archive, r.entry, source, error)) return false;
    if (Crc(0, source.data(), source.size()) != r.sourceCrc) {
        error = L"The XAPK entry failed its CRC (" + r.entry + L").";
        return false;
    }
    vector<uint8_t> delta;
    if (!LoadBlob(r, delta)) { error = L"The delta data is damaged (" + r.dest + L")."; return false; }
    DELTA_INPUT si; si.lpcStart = source.data(); si.uSize = source.size(); si.Editable = FALSE;
    DELTA_INPUT di; di.lpcStart = delta.data(); di.uSize = delta.size(); di.Editable = FALSE;
    DELTA_OUTPUT out; out.lpStart = NULL; out.uSize = 0;
    if (!ApplyDeltaB(0, si, di, &out)) {
        error = L"Could not apply the delta for " + r.dest + L".";
        return false;
    }
    bool ok = out.uSize == r.size && Crc(0, out.lpStart, out.uSize) == r.targetCrc;
    if (ok) {
        HANDLE f = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (f == INVALID_HANDLE_VALUE) ok = false;
        else {
            ok = WriteFileAll(f, out.lpStart, (DWORD)out.uSize);
            CloseHandle(f);
        }
    }
    DeltaFree(out.lpStart);
    if (!ok) { DeleteFileW(dst.c_str()); error = L"The patched file did not match (" + r.dest + L")."; return false; }
    return true;
}

static bool MergeMove(const wstring& srcDir, const wstring& dstDir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((srcDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        wstring src = srcDir + L"\\" + n, dst = dstDir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!MakeDirs(dst) || !MergeMove(src, dst)) { ok = false; break; }
            RemoveDirectoryW(src.c_str());
        } else {
            if (!MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) { ok = false; break; }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

static void CleanupInstall(bool destCreated)
{
    if (!g_stage.empty()) RemoveTree(g_stage);
    g_stage.clear();
    if (destCreated && PathExists(g_destDir) && DirEmpty(g_destDir)) RemoveDirectoryW(g_destDir.c_str());
}

static bool InstallAll()
{
    g_error.clear();
    g_cancel = false;
    g_installing = true;
    g_installStart = GetTickCount();
    uint64_t total = TotalBytes();
    g_totalBytes = (LONG64)total;
    g_doneBytes = 0;
    bool destCreated = !PathExists(g_destDir);
    if (!MakeDirs(g_destDir)) return Fail(L"Could not create the folder\n" + g_destDir);
    ULARGE_INTEGER freeB;
    if (GetDiskFreeSpaceExW(g_destDir.c_str(), &freeB, NULL, NULL) && freeB.QuadPart < total + (256ull << 20)) {
        CleanupInstall(destCreated);
        return Fail(L"Not enough free space on that drive.\nWA2DC needs " + FormatSize(total) +
                    L"; " + FormatSize(freeB.QuadPart) + L" are free.");
    }
    g_stage = g_destDir + L"\\.wa2dc-staging";
    if (PathExists(g_stage)) RemoveTree(g_stage);
    if (!MakeDirs(g_stage)) { CleanupInstall(destCreated); return Fail(L"Could not create the staging folder."); }

    DWORD cancelMs = 0;
    wchar_t cancelBuf[32] = L"";
    if (GetEnvironmentVariableW(L"WA2DC_CANCEL_MS", cancelBuf, 32)) cancelMs = (DWORD)_wtoi(cancelBuf);

    for (auto& r : g_records) {
        if (g_cancel) { CleanupInstall(destCreated); return Fail(L"Cancelled."); }
        if (cancelMs && GetTickCount() - g_installStart >= cancelMs) {
            g_cancel = true;
            CleanupInstall(destCreated);
            return Fail(L"Cancelled.");
        }
        if (r.kind == 3 || ShouldSkip(r)) continue;
        wstring rel = r.kind == 1 ? r.path : r.dest;
        SetCur(rel);
        wstring dst = g_stage + L"\\" + rel;
        MakeDirs(dst.substr(0, dst.rfind(L'\\')));
        if (r.kind == 1) {
            HANDLE out = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (out == INVALID_HANDLE_VALUE) { CleanupInstall(destCreated); return Fail(L"Could not write\n" + dst); }
            uint32_t crc = 0;
            bool ok = UnpackRecord(r, [&](const uint8_t* d, DWORD n) {
                if (g_cancel) return false;
                crc = Crc(crc, d, n);
                InterlockedAdd64(&g_doneBytes, n);
                return WriteFileAll(out, d, n);
            });
            CloseHandle(out);
            if (!ok || crc != r.crc) {
                DeleteFileW(dst.c_str());
                CleanupInstall(destCreated);
                return Fail(g_cancel ? L"Cancelled." : L"Could not unpack " + rel +
                            L"\n(embedded patch data damaged, or the disk is full)");
            }
        } else if (r.kind == 2) {
            wstring error;
            if (!g_xapk.ExtractSource(r.archive, r.entry, dst, r.size, r.crc, &g_cancel, error)) {
                CleanupInstall(destCreated);
                return Fail(error.empty() ? L"Could not copy " + rel : error);
            }
            InterlockedAdd64(&g_doneBytes, r.size);
        } else if (r.kind == 4) {
            wstring error;
            if (!ApplyDeltaRecord(r, dst, error)) {
                CleanupInstall(destCreated);
                return Fail(error);
            }
            InterlockedAdd64(&g_doneBytes, r.size);
        } else if (r.kind == 5) {
            wstring error;
            if (!ApplyRawBundleRecord(r, dst, error)) {
                CleanupInstall(destCreated);
                return Fail(error);
            }
            InterlockedAdd64(&g_doneBytes, r.size);
        }
    }
    if (g_cancel) { CleanupInstall(destCreated); return Fail(L"Cancelled."); }
    if (!MergeMove(g_stage, g_destDir)) { CleanupInstall(destCreated); return Fail(L"Could not move the finished files into place."); }
    RemoveDirectoryW(g_stage.c_str());
    g_stage.clear();
    g_doneBytes = g_totalBytes;
    return true;
}

static HWND g_hwnd;
static DWORD WINAPI InstallThread(LPVOID)
{
    bool ok = InstallAll();
    g_installing = false;
    g_exitCode = ok ? 0 : 1;
    PostMessageW(g_hwnd, WM_APP + 1, ok, 0);
    return 0;
}

static void WriteAutoLog(const wstring& text)
{
    wstring path = g_exeDir + L"\\patcher-auto.txt";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    string utf8 = Narrow(text);
    WriteFileAll(f, utf8.data(), (DWORD)utf8.size());
    CloseHandle(f);
}

static wstring PickFile(HWND owner, const wchar_t* title)
{
    wstring result;
    IFileOpenDialog* dlg = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opt; dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(title);
    COMDLG_FILTERSPEC filter[] = { { L"WA2DC XAPK", L"*.xapk;*.zip" }, { L"All files", L"*.*" } };
    dlg->SetFileTypes(2, filter);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* it = NULL;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR p = NULL;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { result = p; CoTaskMemFree(p); }
            it->Release();
        }
    }
    dlg->Release();
    return result;
}

static wstring PickFolder(HWND owner, const wchar_t* title, const wstring& start)
{
    wstring result;
    IFileOpenDialog* dlg = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opt; dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(title);
    if (!start.empty()) {
        IShellItem* si = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), NULL, IID_PPV_ARGS(&si)))) { dlg->SetFolder(si); si->Release(); }
    }
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* it = NULL;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR p = NULL;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { result = p; CoTaskMemFree(p); }
            it->Release();
        }
    }
    dlg->Release();
    return result;
}

static void CreateShortcut()
{
    IShellLinkW* sl = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return;
    wstring exe = g_destDir + L"\\" + kExeName;
    sl->SetPath(exe.c_str());
    sl->SetWorkingDirectory(g_destDir.c_str());
    sl->SetDescription(L"WA2 Dream Communication for Windows");
    IPersistFile* pf = NULL;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
        PWSTR desk = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &desk))) {
            wstring lnk = wstring(desk) + L"\\WA2DC.lnk";
            pf->Save(lnk.c_str(), TRUE);
            CoTaskMemFree(desk);
        }
        pf->Release();
    }
    sl->Release();
}

// ======================================================================= drawing
static const int W = 1040, H = 640;
static Bitmap* g_art;
static Bitmap* g_back;
static float g_mx = -1, g_my = -1;
static DWORD g_t0;

struct Flake { float x, y, r, vy, vx, ph, rot, spin; int kind; };
static vector<Flake> g_snow;

static void InitSnow()
{
    srand(GetTickCount());
    for (int i = 0; i < 150; i++) {
        Flake f;
        f.x = (float)(rand() % (W + 100)) - 50; f.y = (float)(rand() % H);
        f.kind = rand() % 9 == 0 ? 1 : 0;
        f.r = f.kind ? 6.0f + rand() % 7 : 1.0f + (rand() % 100) / 40.0f;
        f.vy = 18 + f.r * 9 + rand() % 12; f.vx = -(4 + rand() % 10);
        f.ph = (rand() % 628) / 100.0f; f.rot = (rand() % 628) / 100.0f; f.spin = ((rand() % 100) - 50) / 120.0f;
        g_snow.push_back(f);
    }
}

static void Crystal(Graphics& g, float cx, float cy, float r, float rot, BYTE a)
{
    Pen pen(Color(a, 235, 245, 255), std::max(1.0f, r / 7));
    for (int k = 0; k < 6; k++) {
        float ang = rot + k * 3.14159265f / 3;
        float ex = cx + cosf(ang) * r, ey = cy + sinf(ang) * r;
        g.DrawLine(&pen, cx, cy, ex, ey);
        for (int s = -1; s <= 1; s += 2) {
            float bx = cx + cosf(ang) * r * 0.55f, by = cy + sinf(ang) * r * 0.55f;
            float ba = ang + s * 0.7f;
            g.DrawLine(&pen, bx, by, bx + cosf(ba) * r * 0.35f, by + sinf(ba) * r * 0.35f);
        }
    }
}

static void DrawSnow(Graphics& g, float dt, bool front)
{
    for (size_t i = front ? 0 : 1; i < g_snow.size(); i += 2) {
        Flake& f = g_snow[i];
        f.y += f.vy * dt; f.x += f.vx * dt; f.rot += f.spin * dt;
        if (f.y > H + 20 || f.x < -40) { f.y = -20; f.x = (float)(rand() % (W + 100)); }
        float x = f.x + sinf(GetTickCount() / 1600.0f + f.ph) * 8;
        BYTE a = (BYTE)std::min(255.0f, 110 + f.r * 30);
        if (f.kind) Crystal(g, x, f.y, f.r, f.rot, (BYTE)(a * 0.85f));
        else {
            SolidBrush b(Color(a, 255, 255, 255));
            g.FillEllipse(&b, x - f.r, f.y - f.r, f.r * 2, f.r * 2);
        }
    }
}

static void RoundRect(GraphicsPath& p, RectF r, float rad);

static void BuildBackground()
{
    g_back = new Bitmap(W, H, PixelFormat32bppPARGB);
    Graphics g(g_back);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    LinearGradientBrush bg(Point(0, 0), Point(0, H), Color(255, 8, 11, 24), Color(255, 22, 30, 54));
    g.FillRectangle(&bg, 0, 0, W, H);
    if (g_art) {
        float scale = (float)H / g_art->GetHeight();
        float w = g_art->GetWidth() * scale;
        float x = W - w + 8;
        g.DrawImage(g_art, RectF(x, 0, w, (float)H));
        // soft rounded card behind the text column (title stays on the art)
        RectF card(36, 210, 584, 390);
        GraphicsPath shadow;
        RoundRect(shadow, RectF(card.X + 4, card.Y + 6, card.Width, card.Height), 18);
        SolidBrush shadowBrush(Color(80, 0, 0, 0));
        g.FillPath(&shadowBrush, &shadow);
        GraphicsPath panel;
        RoundRect(panel, card, 18);
        SolidBrush panelFill(Color(172, 7, 11, 26));
        g.FillPath(&panelFill, &panel);
        Pen panelEdge(Color(85, 175, 205, 240), 1.2f);
        g.DrawPath(&panelEdge, &panel);
        Pen highlight(Color(28, 255, 255, 255), 1.0f);
        g.DrawLine(&highlight, card.X + 20, card.Y + 1, card.X + card.Width - 20, card.Y + 1);
        LinearGradientBrush fade(PointF(0, H - 150.0f), PointF(0, H + 1.0f), Color(0, 18, 25, 46), Color(220, 18, 25, 46));
        fade.SetWrapMode(WrapModeTileFlipXY);
        g.FillRectangle(&fade, 0, H - 150, W, 150);
    }
    Pen frame(Color(120, 150, 190, 235), 1.0f);
    g.DrawRectangle(&frame, 0, 0, W - 1, H - 1);
}

static FontFamily* g_serif;
static FontFamily* g_sans;
static FontFamily* g_mincho;

static void Text(Graphics& g, const wstring& s, FontFamily* fam, float size, int style, Color c, RectF r,
                 StringAlignment al = StringAlignmentNear, bool shadow = true)
{
    Font f(fam, size, style, UnitPixel);
    StringFormat sf; sf.SetAlignment(al); sf.SetLineAlignment(StringAlignmentNear);
    if (shadow) {
        SolidBrush sb(Color((BYTE)(c.GetA() * 0.7f), 0, 0, 0));
        RectF r2 = r; r2.X += 1.5f; r2.Y += 1.5f;
        g.DrawString(s.c_str(), -1, &f, r2, &sf, &sb);
    }
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, &f, r, &sf, &b);
}

struct Button { int id; RectF r; wstring label; bool enabled, primary; };
static vector<Button> g_buttons;
struct Check { int id; RectF r; wstring label; };
static vector<Check> g_checks;
enum { B_BROWSE_XAPK = 1, B_NEXT, B_BACK, B_BROWSE_DEST, B_INSTALL, B_CANCEL, B_PLAY, B_OPEN, B_SHORTCUT,
       B_CLOSE, B_MIN, B_MUSIC, B_RETRY, B_USE_ART, B_USE_1080 };

static void AddButton(int id, float x, float y, float w, float h, const wstring& label, bool enabled = true, bool primary = false)
{
    g_buttons.push_back({ id, RectF(x, y, w, h), label, enabled, primary });
}

static bool Hover(const RectF& r) { return g_mx >= r.X && g_my >= r.Y && g_mx < r.X + r.Width && g_my < r.Y + r.Height; }

static void RoundRect(GraphicsPath& p, RectF r, float rad)
{
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

static void DrawButton(Graphics& g, const Button& b)
{
    bool hv = b.enabled && Hover(b.r);
    GraphicsPath p; RoundRect(p, b.r, 7);
    Color fill = !b.enabled ? Color(30, 255, 255, 255) : b.primary ? Color(hv ? 235 : 200, 120, 170, 225)
               : Color(hv ? 70 : 38, 200, 225, 255);
    SolidBrush fb(fill);
    g.FillPath(&fb, &p);
    Pen pen(Color(b.enabled ? (hv ? 255 : 170) : 60, 190, 220, 255), 1.2f);
    g.DrawPath(&pen, &p);
    Color tc = !b.enabled ? Color(110, 200, 210, 225) : b.primary ? Color(255, 10, 18, 38) : Color(255, 235, 242, 255);
    RectF tr = b.r; tr.Y += (b.r.Height - 22) / 2 - 1;
    Text(g, b.label, g_sans, 16, FontStyleBold, tc, tr, StringAlignmentCenter, false);
}

static void DrawPathBox(Graphics& g, float x, float y, float w, const wstring& path, const wchar_t* placeholder)
{
    GraphicsPath p; RoundRect(p, RectF(x, y, w, 36), 6);
    SolidBrush b(Color(120, 0, 0, 0)); g.FillPath(&b, &p);
    Pen pen(Color(110, 160, 190, 230), 1); g.DrawPath(&pen, &p);
    Font f(g_sans, 14, FontStyleRegular, UnitPixel);
    StringFormat sf; sf.SetTrimming(StringTrimmingEllipsisPath); sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush tb(path.empty() ? Color(120, 190, 200, 220) : Color(255, 230, 238, 250));
    g.DrawString(path.empty() ? placeholder : path.c_str(), -1, &f, RectF(x + 12, y + 9, w - 24, 20), &sf, &tb);
}

static void DrawProgress(Graphics& g, float x, float y, float w, float frac)
{
    GraphicsPath track; RoundRect(track, RectF(x, y, w, 14), 7);
    SolidBrush tb(Color(120, 0, 0, 0)); g.FillPath(&tb, &track);
    Pen tp(Color(100, 160, 190, 230), 1); g.DrawPath(&tp, &track);
    float fw = std::max(14.0f, w * frac);
    GraphicsPath fill; RoundRect(fill, RectF(x, y, fw, 14), 7);
    LinearGradientBrush fb(PointF(x, 0), PointF(x + w, 0), Color(255, 90, 150, 220), Color(255, 225, 240, 255));
    g.FillPath(&fb, &fill);
    Crystal(g, x + fw, y + 7, 11, GetTickCount() / 900.0f, 230);
}

static void DrawCheck(Graphics& g, const Check& c)
{
    RectF box(c.r.X, c.r.Y, 18, 18);
    SolidBrush b(Color(120, 0, 0, 0)); g.FillRectangle(&b, box);
    Pen p(Color(150, 190, 215, 245), 1); g.DrawRectangle(&p, box);
    bool on = (c.id == B_USE_ART) ? g_useArt : g_use1080;
    if (on) {
        Pen tick(Color(255, 170, 235, 255), 2.2f);
        g.DrawLine(&tick, box.X + 4, box.Y + 9, box.X + 8, box.Y + 13);
        g.DrawLine(&tick, box.X + 8, box.Y + 13, box.X + 15, box.Y + 4);
    }
    Text(g, c.label, g_sans, 14, FontStyleRegular, Color(235, 222, 232, 248), RectF(c.r.X + 26, c.r.Y - 1, 500, 22));
}

static void Render(HDC hdc, float dt)
{
    static Bitmap frame(W, H, PixelFormat32bppPARGB);
    Graphics g(&frame);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(g_back, 0, 0, W, H);
    DrawSnow(g, dt, false);

    Text(g, L"WHITE ALBUM2", g_serif, 46, FontStyleBold, Color(255, 240, 248, 255), RectF(54, 30, 560, 60));
    Text(g, L"～ドリームコミュニケーション～", g_mincho, 22, FontStyleRegular, Color(235, 205, 228, 252), RectF(56, 92, 560, 30));
    Text(g, L"for Windows", g_serif, 25, FontStyleItalic, Color(255, 222, 236, 255), RectF(56, 126, 420, 30));
    Text(g, L"Patcher", g_serif, 20, FontStyleRegular, Color(255, 192, 216, 245), RectF(56, 164, 300, 26));
    Pen line(Color(130, 170, 200, 240), 1);
    g.DrawLine(&line, 54.0f, 205.0f, 550.0f, 205.0f);

    g_buttons.clear();
    g_checks.clear();
    float x = 54, y = 224;
    Color body(235, 222, 232, 248), dim(200, 170, 190, 220);
    switch (g_page) {
    case P_XAPK:
        Text(g, L"Choose the XAPK file.", g_serif, 22, FontStyleRegular, Color(255, 200, 220, 250), RectF(x, y, 500, 30));
        Text(g, L"Choose the WA2 Dream Communication 1.0.7 XAPK.", g_sans, 15, FontStyleRegular, body,
             RectF(x, y + 36, 540, 24));
        DrawPathBox(g, x, y + 70, 380, g_xapkPath, L"No XAPK chosen");
        AddButton(B_BROWSE_XAPK, x + 392, y + 70, 120, 36, L"Browse...");
        if (!g_dataOk) {
            Text(g, g_dataError, g_sans, 14, FontStyleItalic,
                 Color(240, 240, 160, 160), RectF(x, y + 128, 540, 60));
        } else if (!g_xapkStatus.empty()) {
            Text(g, g_xapkStatus, g_sans, 14, g_xapkOk ? FontStyleRegular : FontStyleItalic,
                 g_xapkOk ? Color(220, 190, 235, 215) : Color(235, 240, 170, 170), RectF(x, y + 128, 540, 44));
        }
        AddButton(B_NEXT, x, 540, 190, 44, L"Next", g_xapkOk && g_dataOk, true);
        break;
    case P_DEST: {
        Text(g, L"Choose destination folder.", g_serif, 22, FontStyleRegular, Color(255, 200, 220, 250), RectF(x, y, 500, 30));
        Text(g, L"Choose the folder for the Windows game.", g_sans, 15, FontStyleRegular, body, RectF(x, y + 36, 540, 24));
        DrawPathBox(g, x, y + 70, 380, g_destDir, L"No folder chosen");
        AddButton(B_BROWSE_DEST, x + 392, y + 70, 120, 36, L"Browse...");
        uint64_t need = TotalBytes();
        wstring info = L"Space needed: " + FormatSize(need) + L"  (+ " + FormatSize(220ull << 20) + L" temporary)";
        ULARGE_INTEGER fb;
        bool enough = true;
        if (!g_destDir.empty()) {
            wstring root = g_destDir.substr(0, 3);
            if (GetDiskFreeSpaceExW(root.c_str(), &fb, NULL, NULL)) {
                info += L"     Free on " + root.substr(0, 2) + L": " + FormatSize(fb.QuadPart);
                enough = fb.QuadPart > need + (256ull << 20);
            }
        }
        Text(g, info, g_sans, 14, FontStyleRegular, enough ? dim : Color(240, 240, 160, 160), RectF(x, y + 120, 540, 22));
        Check art = { B_USE_ART, RectF(x, y + 166, 300, 22), L"Use the bundled strip art" };
        Check launcher = { B_USE_1080, RectF(x, y + 196, 340, 22), L"Create the 1920x1080 launcher" };
        g_checks.push_back(art); g_checks.push_back(launcher);
        for (auto& c : g_checks) DrawCheck(g, c);
        AddButton(B_BACK, x, 540, 120, 44, L"Back");
        AddButton(B_INSTALL, x + 136, 540, 190, 44, L"Install", g_xapkOk && !g_destDir.empty() && enough, true);
        break;
    }
    case P_INSTALL: {
        Text(g, L"Building WA2DC...", g_serif, 22, FontStyleRegular, Color(255, 200, 220, 250), RectF(x, y, 540, 30));
        double frac = g_totalBytes ? (double)g_doneBytes / g_totalBytes : 0;
        DrawProgress(g, x, y + 64, 470, (float)frac);
        wchar_t pct[64]; swprintf_s(pct, L"%.0f%%", frac * 100);
        Text(g, pct, g_sans, 15, FontStyleBold, body, RectF(x + 476, y + 58, 70, 24));
        wstring cur; EnterCriticalSection(&g_cs); cur = g_curFile; LeaveCriticalSection(&g_cs);
        Text(g, cur, g_sans, 13, FontStyleRegular, dim, RectF(x, y + 92, 520, 20));
        DWORD el = (GetTickCount() - g_installStart) / 1000;
        wchar_t eta[160];
        if (g_hasRawBundles) {
            swprintf_s(eta, L"%s of %s   re-encoding bundles (a few minutes)",
                       FormatSize(g_doneBytes).c_str(), FormatSize(g_totalBytes).c_str());
        } else if (frac > 0.02) {
            DWORD rem = (DWORD)(el * (1 - frac) / frac);
            swprintf_s(eta, L"%s of %s   about %u min %02u s left", FormatSize(g_doneBytes).c_str(),
                       FormatSize(g_totalBytes).c_str(), rem / 60, rem % 60);
        } else swprintf_s(eta, L"%s of %s", FormatSize(g_doneBytes).c_str(), FormatSize(g_totalBytes).c_str());
        Text(g, eta, g_sans, 13, FontStyleRegular, dim, RectF(x, y + 114, 520, 20));
        AddButton(B_CANCEL, x, 540, 150, 44, L"Cancel", !g_cancel);
        break;
    }
    case P_DONE:
        Text(g, L"Finished.", g_serif, 22, FontStyleRegular, Color(255, 200, 220, 250), RectF(x, y, 500, 30));
        Text(g, L"WA2DC is ready in\n" + g_destDir + L"\n\nStart it with WA2DC.exe.", g_sans, 15, FontStyleRegular, body,
             RectF(x, y + 40, 540, 120));
        AddButton(B_PLAY, x, 470, 180, 44, L"Play now", true, true);
        AddButton(B_OPEN, x + 196, 470, 160, 44, L"Open folder");
        AddButton(B_SHORTCUT, x, 528, 356, 40, g_shortcutMade ? L"Desktop shortcut created" : L"Create desktop shortcut",
                  !g_shortcutMade);
        AddButton(B_CLOSE + 100, x + 372, 528, 120, 40, L"Close");
        break;
    case P_ERROR:
        Text(g, L"Something went wrong", g_serif, 22, FontStyleRegular, Color(255, 240, 190, 190), RectF(x, y, 500, 30));
        Text(g, g_error, g_sans, 15, FontStyleRegular, body, RectF(x, y + 40, 540, 180));
        AddButton(B_RETRY, x, 540, 160, 44, L"Back", true, true);
        break;
    }
    for (auto& b : g_buttons) DrawButton(g, b);

    // window buttons
    {
        bool music = !g_intro.empty() || !g_loop.empty();
        float right = W - 52.0f;
        RectF rc(right, 10, 34, 28), rmin(right - 40, 10, 34, 28), rm(right - 80, 10, 34, 28);
        if (music) g_buttons.push_back({ B_MUSIC, rm, L"", true, false });
        g_buttons.push_back({ B_MIN, rmin, L"", true, false });
        g_buttons.push_back({ B_CLOSE, rc, L"", !g_installing, false });
        if (music && Hover(rm)) { SolidBrush hb(Color(60, 255, 255, 255)); g.FillRectangle(&hb, rm); }
        if (Hover(rmin)) { SolidBrush hb(Color(60, 255, 255, 255)); g.FillRectangle(&hb, rmin); }
        if (Hover(rc)) { SolidBrush hb(Color(60, 255, 255, 255)); g.FillRectangle(&hb, rc); }
        Pen p(Color(220, 230, 240, 255), 1.6f);
        g.DrawLine(&p, rmin.X + 11, rmin.Y + 15, rmin.X + 23, rmin.Y + 15);
        g.DrawLine(&p, rc.X + 11, rc.Y + 8, rc.X + 23, rc.Y + 20);
        g.DrawLine(&p, rc.X + 23, rc.Y + 8, rc.X + 11, rc.Y + 20);
        if (music) {
            Font f(g_sans, 17, FontStyleRegular, UnitPixel);
            SolidBrush nb(Color(g_muted ? 110 : 230, 230, 240, 255));
            g.DrawString(L"笙ｪ", -1, &f, PointF(rm.X + 9, rm.Y + 3), &nb);
            if (g_muted) g.DrawLine(&p, rm.X + 7, rm.Y + 22, rm.X + 27, rm.Y + 6);
        }
    }
    DrawSnow(g, dt, true);

    Graphics out(hdc);
    out.DrawImage(&frame, 0, 0, W, H);
}

// ======================================================================= window
static void StartInstall()
{
    if (!g_xapk.open) {
        wstring error;
        if (!g_xapk.Open(g_xapkPath, error) || !g_xapk.Validate(error)) {
            g_xapkOk = false;
            g_error = error;
            g_page = P_ERROR;
            return;
        }
        g_xapkOk = true;
    }
    if (!g_xapkOk) { g_error = L"Choose a valid XAPK first."; g_page = P_ERROR; return; }
    g_page = P_INSTALL;
    g_cancel = false;
    g_installing = true;
    g_installStart = GetTickCount();
    CloseHandle(CreateThread(NULL, 0, InstallThread, NULL, 0, NULL));
}

static void OnButton(HWND h, int id)
{
    switch (id) {
    case B_BROWSE_XAPK: {
        wstring p = PickFile(h, L"Choose the WA2DC 1.0.7 XAPK");
        if (!p.empty()) {
            g_xapkPath = p;
            SetCursor(LoadCursor(NULL, IDC_WAIT));
            g_xapk.Close();
            wstring error;
            g_xapkOk = false;
            if (!g_xapk.Open(p, error)) { g_xapkStatus = error; }
            else {
                g_xapkOk = g_xapk.Validate(g_xapkStatus);
                if (!g_xapkOk) { g_xapk.Close(); }
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        break;
    }
    case B_NEXT: if (g_xapkOk) g_page = P_DEST; break;
    case B_BACK: if (g_page == P_DEST) g_page = P_XAPK; break;
    case B_BROWSE_DEST: {
        wstring d = PickFolder(h, L"Folder for the WA2DC Windows game", g_destDir);
        if (!d.empty()) {
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileW((d + L"\\*").c_str(), &fd);
            bool empty = true;
            if (f != INVALID_HANDLE_VALUE) {
                do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) empty = false; } while (FindNextFileW(f, &fd));
                FindClose(f);
            }
            bool isOurs = PathExists(d + L"\\" + kExeName);
            g_destDir = (empty || isOurs) ? d : d + L"\\WA2DC";
        }
        break;
    }
    case B_INSTALL: StartInstall(); break;
    case B_CANCEL: g_cancel = true; break;
    case B_PLAY: {
        wstring exe = g_destDir + L"\\" + kExeName;
        STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
        if (CreateProcessW(exe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, g_destDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            PostMessageW(h, WM_CLOSE, 0, 0);
        }
        break;
    }
    case B_OPEN: ShellExecuteW(h, L"open", g_destDir.c_str(), NULL, NULL, SW_SHOWNORMAL); break;
    case B_SHORTCUT: CreateShortcut(); g_shortcutMade = true; break;
    case B_CLOSE: case B_CLOSE + 100: PostMessageW(h, WM_CLOSE, 0, 0); break;
    case B_MIN: ShowWindow(h, SW_MINIMIZE); break;
    case B_MUSIC: SetMuted(!g_muted); break;
    case B_RETRY: g_page = P_DEST; break;
    case B_USE_ART: g_useArt = !g_useArt; break;
    case B_USE_1080: g_use1080 = !g_use1080; break;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER: {
        static DWORD last = GetTickCount();
        DWORD now = GetTickCount();
        float dt = std::min(0.1f, (now - last) / 1000.0f);
        last = now;
        HDC dc = GetDC(h);
        Render(dc, dt);
        ReleaseDC(h, dc);
        return 0;
    }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); Render(dc, 0); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        g_mx = (float)GET_X_LPARAM(lp); g_my = (float)GET_Y_LPARAM(lp);
        bool over = false;
        for (auto& b : g_buttons) if (b.enabled && Hover(b.r)) over = true;
        SetCursor(LoadCursor(NULL, over ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: g_mx = g_my = -1; return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) return TRUE; break;
    case WM_LBUTTONDOWN: {
        g_mx = (float)GET_X_LPARAM(lp); g_my = (float)GET_Y_LPARAM(lp);
        for (auto& c : g_checks) {
            if (Hover(c.r)) { OnButton(h, c.id); return 0; }
        }
        for (auto& b : g_buttons)
            if (Hover(b.r)) { if (b.enabled) OnButton(h, b.id); return 0; }
        if (g_my < 60) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_APP + 1:
        if (wp) g_page = P_DONE;
        else {
            EnterCriticalSection(&g_cs);
            if (g_error.empty()) g_error = L"The installation stopped.";
            LeaveCriticalSection(&g_cs);
            g_page = P_ERROR;
        }
        g_xapk.Close();
        if (g_auto) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        if (g_installing) {
            if (MessageBoxW(h, L"Stop the installation?", L"WA2DC Patcher", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            g_cancel = true;
            while (g_installing) Sleep(50);
        }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CrcInit();
    InitializeCriticalSection(&g_cs);
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    g_exeDir = self; g_exeDir.resize(g_exeDir.rfind(L'\\'));

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        if ((!wcscmp(argv[i], L"/auto") || !wcscmp(argv[i], L"--auto")) && i + 2 < argc) {
            g_auto = true;
            g_xapkPath = argv[++i];
            g_destDir = argv[++i];
        } else if (!wcscmp(argv[i], L"/xapk") && i + 1 < argc) {
            g_xapkPath = argv[++i];
        } else if (!wcscmp(argv[i], L"/dest") && i + 1 < argc) {
            g_destDir = argv[++i];
        }
    }
    LocalFree(argv);

    if (!LoadData()) {
        g_dataOk = false;
        g_dataError = L"The patcher's embedded patch data is missing or damaged.\n"
                      L"Download WA2DC Patcher.exe again.";
        if (g_auto) {
            WriteAutoLog(L"The patcher's embedded patch data is missing or damaged. Download WA2DC Patcher.exe again.");
            return 1;
        }
    }
    for (auto& r : g_records) if (r.kind == 5) { g_hasRawBundles = true; break; }

    if (g_auto) {
        if (g_xapkPath.empty() || g_destDir.empty()) {
            WriteAutoLog(L"usage: WA2DC Patcher.exe /auto <xapk> <dest>");
            return 1;
        }
        wstring error;
        if (!g_xapk.Open(g_xapkPath, error)) { WriteAutoLog(error); return 1; }
        if (!g_xapk.Validate(error)) { WriteAutoLog(error); g_xapk.Close(); return 1; }
        bool ok = InstallAll();
        WriteAutoLog(ok ? L"OK" : g_error);
        g_xapk.Close();
        return ok ? 0 : 1;
    }

    GdiplusStartupInput gsi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gsi, NULL);
    g_art = LoadPng(L"bg.png");
    auto family = [](std::initializer_list<const wchar_t*> names) {
        for (const wchar_t* name : names) {
            FontFamily* f = new FontFamily(name);
            if (f->GetLastStatus() == Ok) return f;
            delete f;
        }
        return new FontFamily(L"Arial");
    };
    g_serif = family({ L"Book Antiqua", L"Georgia", L"Times New Roman" });
    g_sans = family({ L"Segoe UI", L"Arial" });
    g_mincho = family({ L"Yu Mincho", L"MS Mincho", L"MS Gothic", L"Meiryo" });
    BuildBackground();
    InitSnow();
    StartMusic();

    // a GUI launched with /xapk pre-validates the file so the wizard opens ready
    if (!g_xapkPath.empty()) {
        wstring error;
        SetCursor(LoadCursor(NULL, IDC_WAIT));
        g_xapkOk = false;
        if (!g_xapk.Open(g_xapkPath, error)) g_xapkStatus = error;
        else {
            g_xapkOk = g_xapk.Validate(g_xapkStatus);
            if (!g_xapkOk) g_xapk.Close();
        }
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"WA2DCPatcher";
    RegisterClassExW(&wc);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, kTitle,
                             WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, wa.left + (wa.right - wa.left - W) / 2,
                             wa.top + (wa.bottom - wa.top - H) / 2, W, H, NULL, NULL, inst, NULL);
    ShowWindow(g_hwnd, show);
    SetTimer(g_hwnd, 1, 33, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) { TranslateMessage(&m); DispatchMessage(&m); }
    g_quitAudio = true;
    Sleep(250);
    g_xapk.Close();
    return g_exitCode;
}
