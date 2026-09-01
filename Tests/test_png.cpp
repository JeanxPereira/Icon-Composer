// Reading a PNG: the inflate and the decoder.
//
// THE ORACLE IS SOMEBODY ELSE'S ENCODER. A decoder checked only against this
// repository's own `encodePng` would prove the pair agrees with itself, which
// is not the same as proving either is right. The streams below were produced
// by PYTHON'S ZLIB -- a separate, widely-exercised deflate -- at three
// compression levels, so between them they reach stored, fixed-Huffman and
// dynamic-Huffman blocks. The expected pixels are known because the generator
// chose them.
//
// And the corpus gate then runs the decoder over the 60 real PNGs, where the
// cross-check is IHDR's own arithmetic: the decompressed stream has to be
// exactly `height * (stride + 1)` bytes, a number that comes from a different
// chunk than the data.
#include "check.h"
#include "Source/IconComposerFoundation/Inflate.h"
#include "Source/IconComposerFoundation/Png.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <algorithm>
#include <vector>

namespace {

// 4x3 RGBA, rows filtered 0/1/2, zlib level 9. Python's zlib made the stream.
const std::uint8_t kRgbaAllFilters[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x08, 0x06, 0x00, 0x00, 0x00, 0xb4,
    0xf4, 0xae, 0xc6, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0b, 0x13,
    0x00, 0x00, 0x0b, 0x13, 0x01, 0x00, 0x9a, 0x9c, 0x18, 0x00, 0x00, 0x00, 0x2b, 0x49, 0x44,
    0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x64, 0xfe, 0xff, 0x5f, 0x88, 0xf9, 0xe1, 0x7f, 0x65,
    0xe6, 0xc3, 0xff, 0x4d, 0x98, 0x97, 0xfe, 0x67, 0x64, 0xd4, 0xfe, 0x7f, 0x58, 0x90, 0xe1,
    0x11, 0x03, 0x0c, 0x33, 0x31, 0x68, 0x30, 0x1c, 0x41, 0xc6, 0x00, 0xdb, 0x23, 0x10, 0x35,
    0x55, 0x68, 0x06, 0x72, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
    0x82,
};

// 4x3 RGBA, Paeth/average/up rows, zlib level 0 -- STORED blocks.
const std::uint8_t kRgbaStored[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x08, 0x06, 0x00, 0x00, 0x00, 0xb4,
    0xf4, 0xae, 0xc6, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0b, 0x13,
    0x00, 0x00, 0x0b, 0x13, 0x01, 0x00, 0x9a, 0x9c, 0x18, 0x00, 0x00, 0x00, 0x3e, 0x49, 0x44,
    0x41, 0x54, 0x78, 0x01, 0x01, 0x33, 0x00, 0xcc, 0xff, 0x04, 0x01, 0x03, 0xff, 0xff, 0x11,
    0x00, 0xe2, 0x00, 0x11, 0x00, 0xe2, 0x00, 0x11, 0x00, 0xe2, 0x00, 0x03, 0x01, 0x2a, 0x80,
    0x44, 0x09, 0x14, 0xf1, 0xe2, 0x09, 0x14, 0xf1, 0xe2, 0x09, 0x14, 0xf1, 0xe2, 0x02, 0x00,
    0x28, 0x00, 0xc4, 0x00, 0x28, 0x00, 0xc4, 0x00, 0x28, 0x00, 0xc4, 0x00, 0x28, 0x00, 0xc4,
    0x8e, 0xaf, 0x0f, 0x54, 0xd7, 0xf5, 0x10, 0x09, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
};

// 6x2 RGB: three channels, and alpha must come back 1.
const std::uint8_t kRgbNoAlpha[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xf4,
    0x3f, 0x3a, 0x09, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0b, 0x13,
    0x00, 0x00, 0x0b, 0x13, 0x01, 0x00, 0x9a, 0x9c, 0x18, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44,
    0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0x48, 0x39, 0xc1, 0xc5, 0xf0, 0x13, 0x19, 0xb1, 0x30,
    0xb0, 0x32, 0xa0, 0x01, 0x00, 0xae, 0x3f, 0x06, 0x46, 0xf7, 0x20, 0x28, 0x13, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// the pixels those two carry, straight RGBA bytes
// 4x3, row major
const std::uint8_t kRgbaPixels[] = {
    0x01, 0x03, 0xff, 0xff, 0x12, 0x03, 0xe1, 0xff, 0x23, 0x03, 0xc3, 0xff, 0x34, 0x03, 0xa5,
    0xff, 0x01, 0x2b, 0xff, 0xc3, 0x12, 0x2b, 0xe1, 0xc3, 0x23, 0x2b, 0xc3, 0xc3, 0x34, 0x2b,
    0xa5, 0xc3, 0x01, 0x53, 0xff, 0x87, 0x12, 0x53, 0xe1, 0x87, 0x23, 0x53, 0xc3, 0x87, 0x34,
    0x53, 0xa5, 0x87,
};
// 6x2, row major, three channels
const std::uint8_t kRgbPixels[] = {
    0x00, 0x64, 0xc8, 0x0a, 0x64, 0xc1, 0x14, 0x64, 0xba, 0x1e, 0x64, 0xb3, 0x28, 0x64, 0xac,
    0x32, 0x64, 0xa5, 0x00, 0x69, 0xc8, 0x0a, 0x69, 0xc1, 0x14, 0x69, 0xba, 0x1e, 0x69, 0xb3,
    0x28, 0x69, 0xac, 0x32, 0x69, 0xa5,
};

// 2x2 RGB whose Paeth row hits pb == pc: a=10, b=40, c=20. The correct predictor picks b.
const std::uint8_t kPaethTie[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd,
    0xd4, 0x9a, 0x73, 0x00, 0x00, 0x00, 0x19, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x01, 0x0e,
    0x00, 0xf1, 0xff, 0x00, 0x14, 0x05, 0x05, 0x28, 0x06, 0x06, 0x04, 0xf6, 0x02, 0x02, 0xa0,
    0x01, 0x01, 0x0b, 0x50, 0x01, 0xf3, 0xf7, 0xf5, 0x60, 0x91, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// the pixels it carries
// 2x2, row major, three channels
const std::uint8_t kPaethTiePixels[] = {
    0x14, 0x05, 0x05, 0x28, 0x06, 0x06, 0x0a, 0x07, 0x07, 0xc8, 0x08, 0x08,
};

// rejeitado por zlib: Error -3 while decompressing data: invalid code lengths set
// a raw deflate block asking for four 1-bit codes where two exist. Python's zlib rejects it too, so it is malformed by something other than the decoder under test.
const std::uint8_t kOversubscribed[] = {
    0x05, 0x00, 0x92, 0x04,
};

std::uint8_t byteAt(const icf::DecodedPng& p, std::size_t i) {
    const float v = p.rgba[i];
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

}  // namespace

// Every filter, dynamic Huffman, and an ancillary chunk in the way.
TEST_CASE(a_png_from_another_encoder_decodes_to_the_pixels_it_was_given) {
    const icf::DecodedPng p = icf::decodePng(kRgbaAllFilters, sizeof kRgbaAllFilters);
    if (!p.error.empty()) {
        std::printf("  FAIL decode: %s\n", p.error.c_str());
        ++ictest::failures();
        return;
    }
    CHECK_EQ(p.width, 4u);
    CHECK_EQ(p.height, 3u);
    REQUIRE(p.rgba.size() == 4u * 3u * 4u);
    int bad = 0;
    for (std::size_t i = 0; i < sizeof kRgbaPixels; ++i) {
        if (byteAt(p, i) != kRgbaPixels[i]) {
            if (bad < 3) {
                std::printf("  FAIL byte %zu: got %u, want %u\n", i,
                            static_cast<unsigned>(byteAt(p, i)),
                            static_cast<unsigned>(kRgbaPixels[i]));
            }
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

// The SAME pixels through STORED blocks. A decoder that only ever handled
// Huffman would pass the test above and fail this one.
TEST_CASE(a_png_whose_blocks_are_stored_decodes_the_same) {
    const icf::DecodedPng p = icf::decodePng(kRgbaStored, sizeof kRgbaStored);
    REQUIRE(p.error.empty());
    REQUIRE(p.rgba.size() == 4u * 3u * 4u);
    int bad = 0;
    for (std::size_t i = 0; i < sizeof kRgbaPixels; ++i) bad += byteAt(p, i) != kRgbaPixels[i];
    CHECK_EQ(bad, 0);
}

// Three channels: the colour has to land in RGB and the alpha has to come back
// opaque rather than zero, which is what an uninitialised buffer would give.
TEST_CASE(a_three_channel_png_comes_back_opaque) {
    const icf::DecodedPng p = icf::decodePng(kRgbNoAlpha, sizeof kRgbNoAlpha);
    REQUIRE(p.error.empty());
    CHECK_EQ(p.width, 6u);
    CHECK_EQ(p.height, 2u);
    int bad = 0;
    for (std::size_t i = 0; i < 12; ++i) {
        for (int k = 0; k < 3; ++k) bad += byteAt(p, i * 4 + k) != kRgbPixels[i * 3 + k];
        bad += p.rgba[i * 4 + 3] != 1.0f;
    }
    CHECK_EQ(bad, 0);
}

// A round trip through this repository's OWN encoder, which is a weaker check
// than the ones above and is here for a different reason: it is the one that
// would catch the two of them drifting apart.
TEST_CASE(this_repositorys_own_encoder_round_trips_through_its_decoder) {
    std::vector<float> px;
    for (int i = 0; i < 5 * 4; ++i) {
        px.push_back((i % 5) / 4.0f);
        px.push_back((i / 5) / 3.0f);
        px.push_back(0.5f);
        px.push_back(1.0f);
    }
    const std::vector<std::uint8_t> png = icf::encodePng(px, 5, 4);
    const icf::DecodedPng back = icf::decodePng(png.data(), png.size());
    REQUIRE(back.error.empty());
    CHECK_EQ(back.width, 5u);
    CHECK_EQ(back.height, 4u);
    int bad = 0;
    for (std::size_t i = 0; i < px.size(); ++i) {
        bad += std::abs(back.rgba[i] - px[i]) > 1.0f / 255.0f;
    }
    CHECK_EQ(bad, 0);
}

// What is NOT read has to say so. A decoder that returns an empty image with no
// reason cannot be told apart from one that read an empty file.
TEST_CASE(what_the_png_reader_refuses_it_names) {
    const std::uint8_t notPng[16] = {'h', 'e', 'l', 'l', 'o'};
    CHECK(icf::decodePng(notPng, sizeof notPng).error.find("signature") != std::string::npos);

    // A truncated file: the signature is right and IEND never arrives.
    std::vector<std::uint8_t> cut(kRgbaAllFilters, kRgbaAllFilters + 40);
    CHECK(!icf::decodePng(cut.data(), cut.size()).error.empty());

    // Adam7 is a GAP and the message has to say which, because it is refused
    // for a different reason than a colour type the corpus never uses.
    std::vector<std::uint8_t> lace(kRgbaAllFilters, kRgbaAllFilters + sizeof kRgbaAllFilters);
    lace[8 + 8 + 12] = 1;  // IHDR's interlace byte
    const std::string why = icf::decodePng(lace.data(), lace.size()).error;
    CHECK(why.find("Adam7") != std::string::npos);
}

// The zlib wrapper's checks are checks, not decoration.
TEST_CASE(the_zlib_wrapper_rejects_what_it_should) {
    const std::uint8_t bad_header[8] = {0x78, 0x02, 0, 0, 0, 0, 0, 0};
    CHECK(icf::inflateZlib(bad_header, sizeof bad_header).error.find("check value") !=
          std::string::npos);
    const std::uint8_t not_deflate[8] = {0x70, 0x01, 0, 0, 0, 0, 0, 0};
    CHECK(!icf::inflateZlib(not_deflate, sizeof not_deflate).error.empty());
    // A stream whose Adler-32 is wrong must be refused, or the checksum is a
    // field nobody reads.
    std::vector<std::uint8_t> s{0x78, 0x01, 0x01, 0x03, 0x00, 0xfc, 0xff,
                                'a',  'b',  'c',  0x00, 0x00, 0x00, 0x00};
    CHECK(icf::inflateZlib(s.data(), s.size()).error.find("Adler") != std::string::npos);
    s[10] = 0x02; s[11] = 0x4d; s[12] = 0x01; s[13] = 0x27;   // adler32("abc"), big-endian
    const icf::InflateResult ok = icf::inflateZlib(s.data(), s.size());
    REQUIRE(ok.error.empty());
    REQUIRE(ok.data.size() == 3u);
    CHECK_EQ(ok.data[0], 'a');
    CHECK_EQ(ok.data[2], 'c');
}

// ---- the five the mutation sweep proved were untested -------------------
//
// The sweep came back 114 of 119 and named five guards that nothing made bite.
// A guard with no test behind it is half the work, and this is the third time
// that has happened in this repository -- so these are written as the sweep
// demanded them, each aimed at exactly the mutation that survived.

// `pb == pc` is the ONLY place a wrong tie-break shows. No natural image in the
// other fixtures reaches it, which is why this one is built to.
TEST_CASE(the_paeth_predictor_breaks_its_tie_towards_the_row_above) {
    const icf::DecodedPng p = icf::decodePng(kPaethTie, sizeof kPaethTie);
    if (!p.error.empty()) {
        std::printf("  FAIL decode: %s\n", p.error.c_str());
        ++ictest::failures();
        return;
    }
    REQUIRE(p.rgba.size() == 2u * 2u * 4u);
    // Row 1, pixel 1, channel 0: neighbours a=10, b=40, c=20 give pa=20,
    // pb=pc=10. Picking `c` instead of `b` shifts this byte by 20.
    CHECK_EQ(byteAt(p, (1 * 2 + 1) * 4), 200u);
    int bad = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) bad += byteAt(p, i * 4 + k) != kPaethTiePixels[i * 3 + k];
    }
    CHECK_EQ(bad, 0);
}

// A table asking for more codes than its lengths allow is corruption, and
// accepting it decodes garbage confidently.
TEST_CASE(an_over_subscribed_huffman_table_is_refused) {
    const icf::InflateResult r = icf::inflateRaw(kOversubscribed, sizeof kOversubscribed);
    CHECK(r.error.find("over-subscribed") != std::string::npos);
    CHECK(r.data.empty());
}

// A stored block carries its length twice, the second time complemented. When
// the two disagree the stream is damaged, and copying anyway invents data.
TEST_CASE(a_stored_block_whose_length_complement_disagrees_is_refused) {
    std::vector<std::uint8_t> s{0x78, 0x01, 0x01, 0x03, 0x00, 0xfc, 0xff,
                                'a',  'b',  'c',  0x02, 0x4d, 0x01, 0x27};
    // First prove the stream is good, so the refusal below is about the change
    // and not about the fixture.
    REQUIRE(icf::inflateZlib(s.data(), s.size()).error.empty());
    s[5] = 0x00;  // NLEN no longer complements LEN
    const icf::InflateResult r = icf::inflateZlib(s.data(), s.size());
    CHECK(r.error.find("complement") != std::string::npos);
}

// IHDR fixes the decompressed size from a DIFFERENT chunk than the data. When
// the two disagree, something is wrong and the image must not be built from it.
TEST_CASE(a_png_whose_data_does_not_match_its_header_is_refused) {
    std::vector<std::uint8_t> png(kRgbaAllFilters, kRgbaAllFilters + sizeof kRgbaAllFilters);
    REQUIRE(icf::decodePng(png.data(), png.size()).error.empty());
    // IHDR's height is the second big-endian word of its body, at offset 20.
    png[23] = 2;  // was 3
    const icf::DecodedPng p = icf::decodePng(png.data(), png.size());
    CHECK(p.error.find("IHDR says") != std::string::npos);
    CHECK(p.rgba.empty());
}

// Truncation AFTER a complete IDAT. The earlier truncation test cut at 40 bytes,
// which removed the IDAT too -- so the error came from an empty zlib stream and
// the IEND guard was never what refused it. That is why that test passed while
// the mutation survived.
TEST_CASE(a_png_that_ends_before_its_iend_is_refused_for_that_reason) {
    std::vector<std::uint8_t> png(kRgbaAllFilters,
                                  kRgbaAllFilters + sizeof kRgbaAllFilters - 12);
    const icf::DecodedPng p = icf::decodePng(png.data(), png.size());
    CHECK(p.error.find("IEND") != std::string::npos);
    CHECK(p.rgba.empty());
}


// ---- THE CORPUS GATE ----------------------------------------------------
//
// The fixtures above prove the decoder against another encoder on images this
// file chose. This proves it against sixty real ones, and the check is not a
// comparison at all -- it is IHDR's own arithmetic. A PNG's decompressed stream
// has to be exactly `height * (stride + 1)` bytes, and that number comes from a
// different chunk than the data does. An inflate that drifts by one byte fails
// it; a filter applied to the wrong neighbour does not, which is why the
// fixtures exist too.
//
// Interlaced files are counted APART rather than folded into the failures. They
// are a known gap with a number on it, and a gate that reported them as errors
// would go red for something already written down -- while a gate that ignored
// them silently would let the count drift without anyone noticing.

namespace {

namespace fs = std::filesystem;

std::vector<fs::path> corpusPngs() {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    if (!dir || !*dir) return {};
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& bundle : fs::directory_iterator(fs::path(dir), ec)) {
        const fs::path assets = bundle.path() / "Assets";
        if (!fs::is_directory(assets, ec)) continue;
        for (const auto& f : fs::directory_iterator(assets, ec)) {
            if (f.path().extension() == ".png") out.push_back(f.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE(corpus_png_gate) {
    auto files = corpusPngs();
    REQUIRE(!files.empty());

    int decoded = 0, interlaced = 0;
    long long pixels = 0;
    std::map<std::string, int> refused;
    std::vector<std::string> offenders;

    for (const auto& f : files) {
        const icf::DecodedPng p = icf::readPng(f.string());
        if (p.error.empty()) {
            ++decoded;
            pixels += static_cast<long long>(p.width) * p.height;
            // The buffer has to match the size the header announced, or a later
            // consumer indexes past the end of an image that "decoded fine".
            if (p.rgba.size() != static_cast<std::size_t>(p.width) * p.height * 4) {
                offenders.push_back(f.filename().string() + ": buffer does not match its size");
            }
            continue;
        }
        if (p.error.find("Adam7") != std::string::npos) {
            ++interlaced;
            continue;
        }
        refused[p.error] += 1;
        offenders.push_back(f.filename().string() + ": " + p.error);
    }

    std::printf("  %zu PNGs: %d decoded, %d interlaced (a known gap), %lld pixels\n",
                files.size(), decoded, interlaced, pixels);
    for (const auto& [why, n] : refused) std::printf("    %d refused: %s\n", n, why.c_str());
    for (const auto& o : offenders) std::printf("    %s\n", o.c_str());

    CHECK(offenders.empty());
    // The measurement that set this reader's scope said 2 of 60 interlace. If
    // that number moves, the scope note in Png.h is out of date and this says so
    // instead of letting the documentation quietly become false.
    CHECK_EQ(interlaced, 2);
    CHECK_EQ(decoded + interlaced, static_cast<int>(files.size()));
}
