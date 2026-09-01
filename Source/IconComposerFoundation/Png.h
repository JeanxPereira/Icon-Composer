#pragma once
// A PNG writer, so a render can be LOOKED AT.
//
// WHY IT IS HAND-WRITTEN AND WHY IT DOES NOT COMPRESS
// ----------------------------------------------------
// This repository links no third party, and a picture nobody can open is not a
// deliverable. PNG's payload is a zlib stream, and zlib's format allows STORED
// blocks -- uncompressed, with a length and its complement. A file built that
// way is a fully valid PNG that every viewer opens; it is just larger than one
// a real deflate would produce.
//
// That is the right trade here. Compression would buy disk space in exchange
// for a Huffman coder that would need a gate of its own, and the thing being
// verified in this repository is the RENDER, not the encoder. If the size ever
// matters, the seam to replace is `deflateStored`.
//
// 8 bits per channel, RGBA, non-premultiplied -- which is what PNG means by
// alpha, and what `RenderedImage::rgba` already carries.
#include <cstdint>
#include <string>
#include <vector>

namespace icf {

// Encodes straight RGBA floats in [0, 1] as an 8-bit RGBA PNG. `pixels` is row
// major with four values per pixel; anything outside [0, 1] is clamped, because
// a channel that left the range is a defect upstream and writing a wrapped byte
// would hide it behind a plausible colour.
std::vector<std::uint8_t> encodePng(const std::vector<float>& pixels, std::uint32_t width,
                                    std::uint32_t height);

// Writes the encoding to `path`. Returns the reason it could not, or an empty
// string on success -- callers of this tower report, they do not throw.
std::string writePng(const std::string& path, const std::vector<float>& pixels,
                     std::uint32_t width, std::uint32_t height);

// ---- reading ------------------------------------------------------------
//
// WHAT IT READS, AND THE NUMBER BEHIND THE SCOPE
// ----------------------------------------------
// Raster art is 27.8% of the corpus's layers -- the largest single thing the
// renderer cannot draw (`scripts/slice-reach.py`). Measured over the corpus's
// 60 PNGs:
//
//     bit depth      8, in all 60
//     colour type    RGBA in 45, RGB in 15 -- no palette, no greyscale
//     row filters    all five occur; Paeth 7436 times
//     IDAT           667 chunks across 60 files, so they must be concatenated
//     interlace      Adam7 in 2 of the 60
//
// So: 8-bit RGB and RGBA, every filter, many IDATs, unknown chunks skipped.
//
// ADAM7 IS A GAP, NOT A SCOPE DECISION, and the difference matters. The three
// SVG filters doc 04 §3 refuses are refused because the TARGET does not read
// them -- implementing them would make this renderer differ from what it
// reproduces. Interlacing is not like that: Apple's decoder reads it, so those
// two files are a gap in this reader. It is refused BY NAME rather than decoded
// wrongly, and the count is written down so it cannot hide.

struct DecodedPng {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Straight (NOT premultiplied) RGBA in [0, 1], row major, four per pixel --
    // the same shape `encodePng` takes, so a decode/encode round trip is a
    // comparison and not a conversion.
    std::vector<float> rgba;
    // Empty on success. A decoder that hands back an empty image and no reason
    // cannot be told apart from one that read an empty file.
    std::string error;
};

DecodedPng decodePng(const std::uint8_t* data, std::size_t size);
DecodedPng readPng(const std::string& path);

}  // namespace icf
