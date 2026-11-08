#define LZMA_API_STATIC
#include <lzma.h>

#include <algorithm>

#include "bundle_lzma.h"

static bool RawFilters(lzma_options_lzma& opt, uint32_t dictSize, int lc, int lp, int pb,
                       lzma_filter filters[2])
{
    if (lzma_lzma_preset(&opt, 6)) return false;
    opt.dict_size = dictSize;
    opt.lc = (uint32_t)lc;
    opt.lp = (uint32_t)lp;
    opt.pb = (uint32_t)pb;
    filters[0].id = LZMA_FILTER_LZMA1;
    filters[0].options = &opt;
    filters[1].id = LZMA_VLI_UNKNOWN;
    filters[1].options = NULL;
    return true;
}

bool LzmaDecodeRaw(const uint8_t* in, size_t inSize, uint32_t dictSize,
                   int lc, int lp, int pb, std::vector<uint8_t>& out)
{
    lzma_options_lzma opt;
    lzma_filter filters[2];
    if (!RawFilters(opt, dictSize, lc, lp, pb, filters)) return false;

    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_raw_decoder(&strm, filters) != LZMA_OK) return false;

    strm.next_in = in;
    strm.avail_in = inSize;
    out.clear();
    size_t cap = inSize * 8 + (1u << 20);
    out.resize(cap);
    size_t pos = 0;
    bool ok = false;
    for (;;) {
        if (pos == out.size()) out.resize(out.size() * 2);
        strm.next_out = out.data() + pos;
        strm.avail_out = out.size() - pos;
        lzma_ret ret = lzma_code(&strm, LZMA_RUN);
        pos = out.size() - strm.avail_out;
        if (ret == LZMA_STREAM_END) { ok = true; break; }
        if (ret == LZMA_OK && strm.avail_in == 0 && strm.avail_out > 0) { ok = true; break; }
        if (ret != LZMA_OK) break;
    }
    lzma_end(&strm);
    if (!ok) return false;
    out.resize(pos);
    return true;
}

bool LzmaEncodeRaw(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out)
{
    lzma_options_lzma opt;
    lzma_filter filters[2];
    if (!RawFilters(opt, 1u << 23, 3, 0, 2, filters)) return false;
    opt.mode = LZMA_MODE_NORMAL;
    opt.mf = LZMA_MF_BT4;
    opt.nice_len = 123;

    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_raw_encoder(&strm, filters) != LZMA_OK) return false;

    strm.next_in = in;
    strm.avail_in = inSize;
    out.clear();
    out.resize(std::max<size_t>(inSize / 2 + 65536, 1u << 16));
    size_t pos = 0;
    bool ok = false;
    for (;;) {
        if (pos + 65536 > out.size()) out.resize(out.size() * 2 + 65536);
        strm.next_out = out.data() + pos;
        strm.avail_out = out.size() - pos;
        lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
        pos = out.size() - strm.avail_out;
        if (ret == LZMA_STREAM_END) { ok = true; break; }
        if (ret != LZMA_OK) break;
    }
    lzma_end(&strm);
    if (!ok) return false;
    out.resize(pos);
    return true;
}
