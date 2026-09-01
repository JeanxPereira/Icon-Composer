#pragma once
// DEFLATE decompression (RFC 1951), because a PNG's pixels are behind it.
//
// WHY IT IS HAND-WRITTEN
// ----------------------
// This repository links no third party, and 27.8% of the corpus's layers are
// raster art (`scripts/slice-reach.py`) -- the single largest thing the
// renderer cannot draw. Every one of those is a PNG, and every PNG's pixels are
// a deflate stream.
//
// Unlike the encoder in `Png.cpp`, which gets away with stored blocks, a
// DECODER has no such shortcut: real PNGs use dynamic Huffman, and the 60 in
// the corpus are no exception. So all three block types are here.
//
// WHAT MAKES IT CHECKABLE
// -----------------------
// A wrong inflate does not usually produce plausible output -- it produces the
// wrong LENGTH, and a PNG's expected length is fixed independently by IHDR
// (`height * (stride + 1)`). That is a real constraint from another part of the
// file, not a self-consistency check. On top of it the tests feed streams
// produced by Python's zlib, which is a genuinely independent encoder.
#include <cstdint>
#include <string>
#include <vector>

namespace icf {

struct InflateResult {
    std::vector<std::uint8_t> data;
    // Empty on success. A decoder that returns half a buffer and no reason is
    // indistinguishable from one that succeeded on a short file.
    std::string error;
};

// A raw DEFLATE stream (no zlib header).
InflateResult inflateRaw(const std::uint8_t* data, std::size_t size);

// A zlib stream (RFC 1950): two header bytes, the deflate data, and an Adler-32
// which IS checked -- an unchecked checksum is a field nobody reads.
InflateResult inflateZlib(const std::uint8_t* data, std::size_t size);

}  // namespace icf
