#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>

// Raw LZMA1 as Unity 3.x writes it in UnityWeb bundles (LZMA header handled by
// the caller). Decode uses the source bundle's props/dict; encode uses the
// exact settings UnityPy/Python lzma used to build the target bundles:
//   preset 6, dict 8 MiB, lc=3 lp=0 pb=2, normal mode, BT4, nice_len=123.
bool LzmaDecodeRaw(const uint8_t* in, size_t inSize, uint32_t dictSize,
                   int lc, int lp, int pb, std::vector<uint8_t>& out);
bool LzmaEncodeRaw(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out);
