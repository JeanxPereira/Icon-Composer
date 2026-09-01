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

}  // namespace icf
