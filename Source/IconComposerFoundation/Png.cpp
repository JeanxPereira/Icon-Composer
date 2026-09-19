#include "Source/IconComposerFoundation/Png.h"

#include "Source/IconComposerFoundation/Inflate.h"

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


namespace {

std::uint32_t be32at(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

}  // namespace

bool pngSize(const std::uint8_t* data, std::size_t size, std::uint32_t& width,
             std::uint32_t& height) {
    static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    // 8 da assinatura + 8 do cabeçalho do chunk + 13 do corpo do IHDR.
    if (!data || size < 8 + 8 + 13 || std::memcmp(data, kSig, 8) != 0) return false;
    // A especificação exige que o IHDR seja o PRIMEIRO chunk, então não há
    // varredura: se não está aqui, o arquivo não é um PNG legal e a resposta
    // certa é "não tenho caixa a afirmar".
    if (std::memcmp(data + 12, "IHDR", 4) != 0) return false;
    if (be32at(data + 8) < 13) return false;
    const std::uint32_t w = be32at(data + 16);
    const std::uint32_t h = be32at(data + 20);
    if (w == 0 || h == 0) return false;
    width = w;
    height = h;
    return true;
}

DecodedPng decodePng(const std::uint8_t* data, std::size_t size) {
    DecodedPng out;
    static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data || size < 8 || std::memcmp(data, kSig, 8) != 0) {
        out.error = "not a PNG: the signature does not match";
        return out;
    }

    std::uint32_t width = 0, height = 0;
    int depth = 0, colour = -1, interlace = 0;
    std::vector<std::uint8_t> idat;
    bool sawEnd = false;

    std::size_t off = 8;
    while (off + 12 <= size) {
        const std::uint32_t len = be32at(data + off);
        if (len > size || off + 12 + len > size) {
            out.error = "a chunk runs past the end of the file";
            return out;
        }
        const std::uint8_t* tag = data + off + 4;
        const std::uint8_t* body = data + off + 8;
        if (std::memcmp(tag, "IHDR", 4) == 0) {
            if (len < 13) {
                out.error = "IHDR is too short";
                return out;
            }
            width = be32at(body);
            height = be32at(body + 4);
            depth = body[8];
            colour = body[9];
            interlace = body[12];
        } else if (std::memcmp(tag, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(tag, "IEND", 4) == 0) {
            sawEnd = true;
            break;
        }
        // Everything else -- eXIf, pHYs, sRGB, gAMA, iTXt, iCCP -- is skipped
        // by length. The corpus carries all of them and none change the pixels.
        off += 12 + len;
    }

    if (!sawEnd) {
        out.error = "no IEND: the file is truncated";
        return out;
    }
    if (width == 0 || height == 0) {
        out.error = "no IHDR, or an image of zero area";
        return out;
    }
    if (depth != 8) {
        out.error = "bit depth " + std::to_string(depth) +
                    " is not read; every PNG in the corpus is 8";
        return out;
    }
    if (colour != 2 && colour != 6) {
        out.error = "colour type " + std::to_string(colour) +
                    " is not read; the corpus uses 2 (RGB) and 6 (RGBA)";
        return out;
    }
    if (interlace != 0) {
        out.error = "Adam7 interlacing is not read -- a GAP, not a scope "
                    "decision: the target reads it, and 2 of the corpus's 60 "
                    "PNGs use it";
        return out;
    }

    const std::size_t channels = colour == 6 ? 4u : 3u;
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    const InflateResult raw = inflateZlib(idat.data(), idat.size());
    if (!raw.error.empty()) {
        out.error = "IDAT: " + raw.error;
        return out;
    }
    // The expected length comes from IHDR, which is a DIFFERENT part of the
    // file from the stream itself -- so this is a real cross-check on the
    // inflate, not a self-consistency one.
    const std::size_t want = static_cast<std::size_t>(height) * (stride + 1);
    if (raw.data.size() != want) {
        out.error = "IDAT decompressed to " + std::to_string(raw.data.size()) +
                    " bytes where IHDR says " + std::to_string(want);
        return out;
    }

    std::vector<std::uint8_t> image(static_cast<std::size_t>(height) * stride, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t filter = raw.data[static_cast<std::size_t>(y) * (stride + 1)];
        const std::uint8_t* src = &raw.data[static_cast<std::size_t>(y) * (stride + 1) + 1];
        std::uint8_t* cur = &image[static_cast<std::size_t>(y) * stride];
        const std::uint8_t* up = y ? &image[static_cast<std::size_t>(y - 1) * stride] : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= channels ? cur[x - channels] : 0;
            const int b = up ? up[x] : 0;
            const int c = (up && x >= channels) ? up[x - channels] : 0;
            int v = src[x];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default:
                    out.error = "row " + std::to_string(y) + " uses filter " +
                                std::to_string(filter) + ", which does not exist";
                    return out;
            }
            cur[x] = static_cast<std::uint8_t>(v & 0xFF);
        }
    }

    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 1.0f);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
        for (std::size_t k = 0; k < channels; ++k) {
            out.rgba[i * 4 + k] = image[i * channels + k] / 255.0f;
        }
        if (channels == 3) out.rgba[i * 4 + 3] = 1.0f;
    }
    return out;
}

DecodedPng readPng(const std::string& path) {
    DecodedPng out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        out.error = "could not open " + path;
        return out;
    }
    std::vector<std::uint8_t> data;
    std::uint8_t buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) {
        out.error = "could not read " + path;
        return out;
    }
    return decodePng(data.data(), data.size());
}

}  // namespace icf
