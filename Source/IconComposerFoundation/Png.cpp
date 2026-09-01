#include "Source/IconComposerFoundation/Png.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace icf {
namespace {

std::uint32_t crcTable(std::uint32_t n) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    return c;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t seed = 0xFFFFFFFFu) {
    static const std::vector<std::uint32_t> table = [] {
        std::vector<std::uint32_t> t(256);
        for (std::uint32_t i = 0; i < 256; ++i) t[i] = crcTable(i);
        return t;
    }();
    std::uint32_t c = seed;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t n) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < n; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, const char (&tag)[5],
           const std::vector<std::uint8_t>& body) {
    be32(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t start = out.size();
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(tag[i]));
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t c = crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
    be32(out, c);
}

// A zlib stream whose deflate blocks are all STORED. Each block carries at most
// 65535 bytes, then LEN and its one's complement, which is what makes an
// uncompressed block legal rather than a corruption.
std::vector<std::uint8_t> deflateStored(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z;
    z.push_back(0x78);  // CMF: deflate, 32K window
    z.push_back(0x01);  // FLG: no dictionary, fastest -- and (0x78<<8|0x01) % 31 == 0
    std::size_t off = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535u, raw.size() - off);
        const bool last = (off + n) >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>((~n) >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(off),
                 raw.begin() + static_cast<std::ptrdiff_t>(off + n));
        off += n;
    } while (off < raw.size());
    const std::uint32_t a = adler32(raw.data(), raw.size());
    z.push_back(static_cast<std::uint8_t>(a >> 24));
    z.push_back(static_cast<std::uint8_t>(a >> 16));
    z.push_back(static_cast<std::uint8_t>(a >> 8));
    z.push_back(static_cast<std::uint8_t>(a));
    return z;
}

std::uint8_t toByte(float v) {
    if (!(v > 0.0f)) return 0;  // catches NaN as well as the negatives
    if (v >= 1.0f) return 255;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

}  // namespace

std::vector<std::uint8_t> encodePng(const std::vector<float>& pixels, std::uint32_t width,
                                    std::uint32_t height) {
    const std::size_t need = static_cast<std::size_t>(width) * height * 4;
    if (width == 0 || height == 0 || pixels.size() < need) return {};

    // Each row is prefixed with its filter byte; 0 is None, and none of the
    // others would help a stored block.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 4));
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
            for (int k = 0; k < 4; ++k) raw.push_back(toByte(pixels[i + k]));
        }
    }

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, width);
    be32(ihdr, height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // colour type: RGBA
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", deflateStored(raw));
    chunk(out, "IEND", {});
    return out;
}

std::string writePng(const std::string& path, const std::vector<float>& pixels,
                     std::uint32_t width, std::uint32_t height) {
    const std::vector<std::uint8_t> png = encodePng(pixels, width, height);
    if (png.empty()) return "nothing to write: the pixel buffer is smaller than the size given";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return "could not open " + path + " for writing";
    const std::size_t n = std::fwrite(png.data(), 1, png.size(), f);
    const bool closed = std::fclose(f) == 0;
    if (n != png.size()) return "short write to " + path;
    if (!closed) return "could not close " + path;
    return {};
}

}  // namespace icf
