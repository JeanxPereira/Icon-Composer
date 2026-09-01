// icrender -- draw an SVG with the transcribed pipeline, and write a PNG.
//
// WHY THIS IS A SEPARATE BINARY FROM `ictool`
// -------------------------------------------
// `ictool` reads documents and needs nothing but a filesystem: it runs on a
// machine with no GPU, no driver and no Vulkan loader. Linking the renderer into
// it would make every `ictool --tree` depend on a device being present. The
// target draws the same line -- it ships `ictool` and `icrtool` as two programs
// (doc 01 §11) -- so this follows its shape rather than inventing one.
//
// WHAT IT DRAWS is what `scripts/slice-reach.py` measures: flat-filled paths.
// Everything it cannot draw is listed on stderr, by shape, with a reason.
#include "Source/CoreSVG/Document.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/SvgRenderer.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* kUsage =
    "icrender -- draws an SVG with the transcribed pipeline\n"
    "\n"
    "  icrender <file.svg> --out <file.png>\n"
    "\n"
    "  --size N            the square target, in pixels (default 512)\n"
    "  --subdivisions N    line segments per cubic (default 16)\n"
    "\n"
    "Flat fills only. Whatever cannot be drawn is named on stderr.\n";

int fail(const std::string& message) {
    std::fprintf(stderr, "icrender: %s\n", message.c_str());
    return 2;
}

std::string readFile(const std::string& path, bool& ok) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        ok = false;
        return {};
    }
    std::string out;
    char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    ok = std::ferror(f) == 0;
    std::fclose(f);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::fputs(kUsage, stdout);
        return args.empty() ? 2 : 0;
    }

    const std::string input = args[0];
    std::string output;
    rb::RenderOptions options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (i + 1 >= args.size()) return fail("missing a value for " + args[i]);
        const std::string& key = args[i];
        const std::string& value = args[++i];
        if (key == "--out") {
            output = value;
        } else if (key == "--size") {
            const int n = std::atoi(value.c_str());
            if (n <= 0 || n > 8192) return fail("--size must be between 1 and 8192");
            options.width = options.height = static_cast<std::uint32_t>(n);
        } else if (key == "--subdivisions") {
            const int n = std::atoi(value.c_str());
            if (n < 1 || n > 256) return fail("--subdivisions must be between 1 and 256");
            options.subdivisions = n;
        } else {
            return fail("invalid argument name " + key);
        }
    }
    if (output.empty()) return fail("missing --out");

    bool read = true;
    const std::string svg = readFile(input, read);
    if (!read || svg.empty()) return fail("could not read " + input);

    auto doc = icf::svg::SvgDocument::parse(svg);
    if (!doc) return fail("not an SVG this reader can open: " + input);

    auto device = rb::Device::create();
    if (!device) return fail("no Vulkan device: " + device.error());

    auto image = rb::renderSvg(*device, *doc, options);
    if (!image) return fail(image.error());

    const std::string wrote = icf::writePng(output, image->rgba, image->width, image->height);
    if (!wrote.empty()) return fail(wrote);

    std::fprintf(stdout, "%s: %u x %u, %zu of %zu shape(s) drawn\n", output.c_str(),
                 image->width, image->height, image->drawn, doc->shapes.size());

    // Every gap is reported, and the exit code says whether there was one. A
    // picture with a piece missing that exits 0 is a picture nobody checks.
    for (const auto& s : image->skipped) {
        std::fprintf(stderr, "  shape %zu (%s): %s\n", s.index, s.element.c_str(),
                     s.why.c_str());
    }
    for (std::size_t i : image->unconvertedP3) {
        std::fprintf(stderr, "  shape %zu: display-p3 desenhado SEM conversao de espaco\n", i);
    }
    for (const auto& e : doc->unsupported()) {
        std::fprintf(stderr, "  elemento nao desenhado: %s\n", e.c_str());
    }
    return image->skipped.empty() && doc->unsupported().empty() ? 0 : 1;
}
