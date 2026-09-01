#include "Source/IconComposerFoundation/Inflate.h"

#include <array>
#include <cstring>

namespace icf {
namespace {

// A canonical Huffman decoder built from code LENGTHS, which is the only form
// DEFLATE ever transmits. Codes are read MSB-first within the code but the bit
// stream is LSB-first, so the reader accumulates and the table walks bit by bit
// -- slower than a lookup table and much harder to get subtly wrong.
class Huffman {
public:
    bool build(const std::uint8_t* lengths, std::size_t n) {
        counts_.fill(0);
        symbols_.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            if (lengths[i] > 15) return false;
            ++counts_[lengths[i]];
        }
        counts_[0] = 0;
        // An over-subscribed table is a corrupt stream, not a decodable one.
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= counts_[len];
            if (left < 0) return false;
        }
        std::array<int, 16> offsets{};
        for (int len = 1; len < 15; ++len) offsets[len + 1] = offsets[len] + counts_[len];
        for (std::size_t i = 0; i < n; ++i) {
            if (lengths[i]) symbols_[offsets[lengths[i]]++] = static_cast<int>(i);
        }
        return true;
    }

    const std::array<int, 16>& counts() const { return counts_; }
    const std::vector<int>& symbols() const { return symbols_; }

private:
    std::array<int, 16> counts_{};
    std::vector<int> symbols_;
};

// RFC 1951 §3.2.5, the two static tables.
constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                           15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                           67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,    13,
                                         17,   25,   33,   49,   65,   97,    129,  193,
                                         257,  385,  513,  769,  1025, 1537,  2049, 3073,
                                         4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13};
// The order the dynamic block sends the code-length code's own lengths in.
constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                     11, 4,  12, 3, 13, 2, 14, 1, 15};

class Reader {
public:
    Reader(const std::uint8_t* d, std::size_t n) : d_(d), n_(n) {}

    // -1 when the stream ran out; every caller checks, because a reader that
    // returns zero past the end turns a truncated file into plausible data.
    int bits(int count) {
        while (held_ < count) {
            if (pos_ >= n_) return -1;
            acc_ |= static_cast<std::uint32_t>(d_[pos_++]) << held_;
            held_ += 8;
        }
        const int v = static_cast<int>(acc_ & ((1u << count) - 1u));
        acc_ >>= count;
        held_ -= count;
        return v;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            const int b = bits(1);
            if (b < 0) return -1;
            code |= b;
            const int count = h.counts()[len];
            if (code - first < count) return h.symbols()[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }

    void alignToByte() {
        const int drop = held_ & 7;
        acc_ >>= drop;
        held_ -= drop;
    }

    bool copyStored(std::vector<std::uint8_t>& out) {
        alignToByte();
        // Whatever whole bytes are still held belong to the stream.
        std::vector<std::uint8_t> head;
        while (held_ >= 8) {
            head.push_back(static_cast<std::uint8_t>(acc_ & 0xFFu));
            acc_ >>= 8;
            held_ -= 8;
        }
        auto take = [&](std::size_t i) -> int {
            if (i < head.size()) return head[i];
            const std::size_t k = pos_ + (i - head.size());
            return k < n_ ? d_[k] : -1;
        };
        const int l0 = take(0), l1 = take(1), n0 = take(2), n1 = take(3);
        if (l0 < 0 || l1 < 0 || n0 < 0 || n1 < 0) return false;
        const std::uint16_t len = static_cast<std::uint16_t>(l0 | (l1 << 8));
        const std::uint16_t nlen = static_cast<std::uint16_t>(n0 | (n1 << 8));
        if (static_cast<std::uint16_t>(~len) != nlen) return false;
        for (std::size_t i = 0; i < len; ++i) {
            const int b = take(4 + i);
            if (b < 0) return false;
            out.push_back(static_cast<std::uint8_t>(b));
        }
        const std::size_t consumed = 4 + len;
        pos_ += consumed > head.size() ? consumed - head.size() : 0;
        acc_ = 0;
        held_ = 0;
        return true;
    }

private:
    const std::uint8_t* d_;
    std::size_t n_;
    std::size_t pos_ = 0;
    std::uint32_t acc_ = 0;
    int held_ = 0;
};

void buildStatic(Huffman& lit, Huffman& dist) {
    std::uint8_t l[288];
    for (int i = 0; i < 144; ++i) l[i] = 8;
    for (int i = 144; i < 256; ++i) l[i] = 9;
    for (int i = 256; i < 280; ++i) l[i] = 7;
    for (int i = 280; i < 288; ++i) l[i] = 8;
    lit.build(l, 288);
    std::uint8_t d[30];
    for (int i = 0; i < 30; ++i) d[i] = 5;
    dist.build(d, 30);
}

}  // namespace

InflateResult inflateRaw(const std::uint8_t* data, std::size_t size) {
    InflateResult out;
    if (!data || size == 0) {
        out.error = "empty deflate stream";
        return out;
    }
    Reader r(data, size);
    for (;;) {
        const int last = r.bits(1);
        const int type = r.bits(2);
        if (last < 0 || type < 0) {
            out.error = "stream ended inside a block header";
            return out;
        }
        if (type == 0) {
            if (!r.copyStored(out.data)) {
                out.error = "a stored block's length does not match its complement";
                return out;
            }
        } else if (type == 1 || type == 2) {
            Huffman lit, dist;
            if (type == 1) {
                buildStatic(lit, dist);
            } else {
                const int hlit = r.bits(5), hdist = r.bits(5), hclen = r.bits(4);
                if (hlit < 0 || hdist < 0 || hclen < 0) {
                    out.error = "stream ended inside a dynamic block's header";
                    return out;
                }
                const int nlit = hlit + 257, ndist = hdist + 1, ncode = hclen + 4;
                std::uint8_t clen[19] = {0};
                for (int i = 0; i < ncode; ++i) {
                    const int v = r.bits(3);
                    if (v < 0) {
                        out.error = "stream ended inside the code-length code";
                        return out;
                    }
                    clen[kOrder[i]] = static_cast<std::uint8_t>(v);
                }
                Huffman codeLen;
                if (!codeLen.build(clen, 19)) {
                    out.error = "the code-length code is over-subscribed";
                    return out;
                }
                std::vector<std::uint8_t> lengths(static_cast<std::size_t>(nlit) + ndist, 0);
                std::size_t i = 0;
                while (i < lengths.size()) {
                    const int sym = r.decode(codeLen);
                    if (sym < 0) {
                        out.error = "stream ended inside the code lengths";
                        return out;
                    }
                    if (sym < 16) {
                        lengths[i++] = static_cast<std::uint8_t>(sym);
                        continue;
                    }
                    int repeat = 0;
                    std::uint8_t value = 0;
                    if (sym == 16) {
                        if (i == 0) {
                            out.error = "a repeat of the previous length with no previous length";
                            return out;
                        }
                        value = lengths[i - 1];
                        const int e = r.bits(2);
                        if (e < 0) {
                            out.error = "stream ended inside a length repeat";
                            return out;
                        }
                        repeat = 3 + e;
                    } else {
                        const int e = r.bits(sym == 17 ? 3 : 7);
                        if (e < 0) {
                            out.error = "stream ended inside a run of zero lengths";
                            return out;
                        }
                        repeat = (sym == 17 ? 3 : 11) + e;
                    }
                    if (i + static_cast<std::size_t>(repeat) > lengths.size()) {
                        out.error = "a length repeat runs past the end of the table";
                        return out;
                    }
                    for (int k = 0; k < repeat; ++k) lengths[i++] = value;
                }
                if (!lit.build(lengths.data(), static_cast<std::size_t>(nlit)) ||
                    !dist.build(lengths.data() + nlit, static_cast<std::size_t>(ndist))) {
                    out.error = "a Huffman table in the dynamic block is over-subscribed";
                    return out;
                }
            }
            for (;;) {
                const int sym = r.decode(lit);
                if (sym < 0) {
                    out.error = "stream ended inside a block";
                    return out;
                }
                if (sym == 256) break;
                if (sym < 256) {
                    out.data.push_back(static_cast<std::uint8_t>(sym));
                    continue;
                }
                const int li = sym - 257;
                if (li >= 29) {
                    out.error = "a length symbol outside the table";
                    return out;
                }
                const int extra = r.bits(kLengthExtra[li]);
                if (extra < 0) {
                    out.error = "stream ended inside a length";
                    return out;
                }
                const std::size_t length = kLengthBase[li] + static_cast<std::size_t>(extra);
                const int ds = r.decode(dist);
                if (ds < 0 || ds >= 30) {
                    out.error = "a distance symbol outside the table";
                    return out;
                }
                const int dextra = r.bits(kDistExtra[ds]);
                if (dextra < 0) {
                    out.error = "stream ended inside a distance";
                    return out;
                }
                const std::size_t distance = kDistBase[ds] + static_cast<std::size_t>(dextra);
                if (distance > out.data.size()) {
                    out.error = "a back reference points before the start of the output";
                    return out;
                }
                // Byte at a time on purpose: an overlapping copy is LEGAL and is
                // how a run is encoded, so memcpy would be wrong here.
                const std::size_t from = out.data.size() - distance;
                for (std::size_t k = 0; k < length; ++k) out.data.push_back(out.data[from + k]);
            }
        } else {
            out.error = "reserved block type 3";
            return out;
        }
        if (last) break;
    }
    return out;
}

InflateResult inflateZlib(const std::uint8_t* data, std::size_t size) {
    InflateResult out;
    if (size < 6) {
        out.error = "too short to be a zlib stream";
        return out;
    }
    const unsigned cmf = data[0], flg = data[1];
    if ((cmf & 0x0F) != 8) {
        out.error = "not deflate";
        return out;
    }
    if (((cmf << 8) | flg) % 31u != 0) {
        out.error = "the zlib header fails its check value";
        return out;
    }
    if (flg & 0x20) {
        out.error = "a preset dictionary is not supported";
        return out;
    }
    out = inflateRaw(data + 2, size - 2);
    if (!out.error.empty()) return out;

    // The Adler-32 is checked. A checksum nobody reads is a field, not a check.
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t v : out.data) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    const std::uint32_t want = (static_cast<std::uint32_t>(data[size - 4]) << 24) |
                               (static_cast<std::uint32_t>(data[size - 3]) << 16) |
                               (static_cast<std::uint32_t>(data[size - 2]) << 8) |
                               static_cast<std::uint32_t>(data[size - 1]);
    if (((b << 16) | a) != want) {
        out.error = "the Adler-32 does not match the data";
        out.data.clear();
    }
    return out;
}

}  // namespace icf
