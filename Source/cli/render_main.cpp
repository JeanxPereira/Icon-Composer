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
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/RenderCache.h"
#include "Source/RenderBox/SvgRenderer.h"
#include "Source/cli/RenderBundle.h"

#include <chrono>
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
    "  --gpu               a bundle through rb::renderIconGpu (the resident chain)\n"
    "  --repeat N          render a bundle N times sharing one RenderCache, timing each\n"
    "  --warmup            one untimed render first (pipelines built, no cache)\n"
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

// HOW LONG THE DRAWING TOOK, AND WHY IT IS PRINTED AT ALL.
//
// On 2026-09-15 six fronts landed in one afternoon, every one of them green on
// pixels, and together they took a 1024 px glass render from about a second to
// eighty-seven. Nothing failed. Nothing was reported. The editor showed an empty
// canvas because the work simply did not finish inside a human's patience, and
// there was no number anywhere on the screen or in the terminal that said so.
//
// A tool that prints `10 of 10 layer(s) drawn` and nothing else cannot tell the
// difference between fast and catastrophic, so neither can the person reading
// it. The clock is around `renderIcon`/`renderSvg` ONLY -- not the parse, not
// the PNG encode -- because that is the number the budget in
// `Tests/test_time_budget.cpp` gates and the two must mean the same thing.
class Clock {
public:
    double seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    }

private:
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
};

// Seconds, with enough digits to be useful at both ends of the range this
// pipeline actually spans: a no-glass 512 is a few hundredths, a glass 1024 is
// tens of seconds.
std::string secondsText(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", s);
    return buf;
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
    icf::Context ctx;
    bool gpu = false;
    bool warmup = false;
    int repeat = 1;
    for (std::size_t i = 1; i < args.size(); ++i) {
        // As chaves sem valor.
        if (args[i] == "--gpu") {
            gpu = true;
            continue;
        }
        if (args[i] == "--warmup") {
            warmup = true;
            continue;
        }
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
        } else if (key == "--repeat") {
            repeat = std::atoi(value.c_str());
            if (repeat < 1 || repeat > 100) return fail("--repeat must be between 1 and 100");
        } else if (key == "--appearance") {
            auto a = icf::appearanceFromString(value);
            if (!a) return fail("unexpected value for --appearance: " + value);
            ctx.appearance = *a;
        } else if (key == "--idiom") {
            auto d = icf::idiomFromString(value);
            if (!d) return fail("unexpected value for --idiom: " + value);
            ctx.idiom = *d;
        } else {
            return fail("invalid argument name " + key);
        }
    }
    if (output.empty()) return fail("missing --out");

    auto device = rb::Device::create();
    if (!device) return fail("no Vulkan device: " + device.error());

    // A bundle and a loose SVG take different paths, and the argument says which
    // by being a directory or not -- the same test `IconBundle::open` makes.
    if (auto bundle = icf::IconBundle::open(input)) {
        // AS OPCOES E O RENDER SAO DE `iccli::renderBundleIcon`, e nao destas
        // linhas, desde 19/09: e a UNICA copia da sequencia que decide os
        // bytes, e `Tests/test_e2e_export.cpp` compara a UI contra ELA. Ver
        // RenderBundle.h -- o caso comparava contra uma transcricao destas
        // linhas, e uma transcricao nao e um portao.
        //
        // `--gpu`, `--repeat` e `--warmup` existem para MEDIR (plano da frente
        // GPU, restricao 4): o mesmo documento pelos dois caminhos, frio e
        // quente. Com `--repeat N` as N chamadas dividem um `RenderCache`, entao
        // a primeira e a fria e as outras sao as quentes; o PNG e o da ultima.
        // `--warmup` faz uma chamada antes, sem cache e fora do relogio, para
        // que a montagem dos pipelines da GPU (uma vez por processo) nao entre
        // no numero frio.
        if (warmup) {
            auto w = iccli::renderBundleIcon(*device, *bundle, options.width,
                                             options.subdivisions, ctx, gpu);
            if (!w) return fail(w.error());
        }
        rb::RenderCache cache;
        rb::Result<rb::RenderedIcon> icon = std::unexpected(std::string("no render"));
        double elapsed = 0.0;
        for (int r = 0; r < repeat; ++r) {
            const Clock clock;
            icon = iccli::renderBundleIcon(*device, *bundle, options.width,
                                           options.subdivisions, ctx, gpu,
                                           repeat > 1 ? &cache : nullptr);
            elapsed = clock.seconds();
            if (!icon) return fail(icon.error());
            if (repeat > 1) {
                std::fprintf(stdout, "render %d: %s s\n", r + 1,
                             secondsText(elapsed).c_str());
            }
        }
        const std::string wrote =
            icf::writePng(output, icon->rgba, icon->width, icon->height);
        if (!wrote.empty()) return fail(wrote);
        std::fprintf(stdout, "%s: %u x %u, %zu of %zu layer(s) drawn in %s s\n",
                     output.c_str(), icon->width, icon->height, icon->drawn, icon->total,
                     secondsText(elapsed).c_str());
        for (const auto& s : icon->skipped) {
            std::fprintf(stderr, "  grupo %zu / %s: %s\n", s.group, s.layer.c_str(),
                         s.why.c_str());
        }
        for (const auto& g : icon->shapeGaps) std::fprintf(stderr, "  %s\n", g.c_str());
        // WHAT WAS DRAWN WITHOUT BEING CLAIMED, and until 2026-09-09 this line
        // did not exist -- `RenderedIcon::notes` was filled and thrown away.
        //
        // The field is for the thing this project is most afraid of: a picture
        // that looks right and is short of a measurement. The glass ruler
        // (`GlassLayer.h`, GAP ONE) and the blur's kernel both announce
        // themselves through it, and a reader of `icrender` saw "5 of 5
        // layer(s) drawn" and nothing else. A gap nobody prints becomes
        // folklore the moment the picture looks plausible.
        for (const auto& n : icon->notes) std::fprintf(stderr, "  [OBS] %s\n", n.c_str());
        return icon->skipped.empty() && icon->shapeGaps.empty() ? 0 : 1;
    }

    bool read = true;
    const std::string svg = readFile(input, read);
    if (!read || svg.empty()) return fail("could not read " + input);

    auto doc = icf::svg::SvgDocument::parse(svg);
    if (!doc) return fail("not an SVG this reader can open: " + input);

    const Clock clock;
    auto image = rb::renderSvg(*device, *doc, options);
    const double elapsed = clock.seconds();
    if (!image) return fail(image.error());

    const std::string wrote = icf::writePng(output, image->rgba, image->width, image->height);
    if (!wrote.empty()) return fail(wrote);

    std::fprintf(stdout, "%s: %u x %u, %zu of %zu shape(s) drawn in %s s\n", output.c_str(),
                 image->width, image->height, image->drawn, doc->shapes.size(),
                 secondsText(elapsed).c_str());

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
