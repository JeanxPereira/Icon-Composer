// icfidelity -- o corpus inteiro por `renderIcon` e por `renderIconGpu`, e o
// erro de cada documento em niveis de 8 bits (restricao 2 do plano da frente
// GPU, `Docs/Plans/2026-09-29-render-gpu.md`).
//
//   icfidelity <pasta do corpus> [--size N]
//
// Uma linha por documento (`<bundle> mean max over x,y canal`), e no fim a
// distribuicao e os dez piores. Sai 1 se algum documento passa do teto (media
// <= 0,5 nivel, pior pixel <= 4 niveis) ou se as decisoes (skipped, gaps, notas,
// contadores) divergem -- o teto se EXPLICA, nao se levanta.
//
// Existe apesar do caso em `Tests/test_gpu_fidelity.cpp` porque a suite roda em
// Debug, onde os 145 documentos pelos dois caminhos custam minutos; aqui, em
// Release, custam segundos. As duas medem com `iccli::compareIcons`.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "Source/cli/Fidelity.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs("icfidelity <corpus dir> [--size N]\n", stderr);
        return 2;
    }
    const fs::path dir = argv[1];
    std::uint32_t size = 512;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (std::string(argv[i]) == "--size") size = static_cast<std::uint32_t>(std::atoi(argv[i + 1]));
    }
    auto device = rb::Device::create();
    if (!device) {
        std::fprintf(stderr, "icfidelity: no Vulkan device: %s\n", device.error().c_str());
        return 2;
    }

    std::vector<fs::path> bundles;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_directory() && fs::exists(e.path() / "icon.json")) bundles.push_back(e.path());
    }
    std::sort(bundles.begin(), bundles.end());

    struct Row {
        std::string name;
        iccli::FidelityStats s;
    };
    std::vector<Row> rows;
    int failed = 0;
    for (const fs::path& p : bundles) {
        auto b = icf::IconBundle::open(p);
        const std::string name = p.filename().string();
        if (!b) {
            std::printf("%s NAO ABRE\n", name.c_str());
            ++failed;
            continue;
        }
        auto s = iccli::fidelityOf(*device, *b, size);
        if (!s) {
            std::printf("%s ERRO %s\n", name.c_str(), s.error().c_str());
            ++failed;
            continue;
        }
        std::printf("%s %.4f %d %zu %u,%u c%d%s%s\n", name.c_str(), s->mean, s->max, s->over,
                    s->worstX, s->worstY, s->worstChannel,
                    s->sameShape ? "" : " DECISOES-DIFEREM:", s->shapeWhy.c_str());
        if (!s->withinCeiling()) ++failed;
        rows.push_back({name, *s});
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.s.max != b.s.max) return a.s.max > b.s.max;
        return a.s.mean > b.s.mean;
    });
    int hist[6] = {0, 0, 0, 0, 0, 0};   // max 0, 1, 2, 3, 4, >4
    double worstMean = 0.0;
    for (const Row& r : rows) {
        ++hist[std::min(r.s.max, 5)];
        worstMean = std::max(worstMean, r.s.mean);
    }
    std::printf("\n%zu documentos a %u px. pior pixel: 0:%d 1:%d 2:%d 3:%d 4:%d >4:%d. "
                "maior media: %.4f\n",
                rows.size(), size, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5],
                worstMean);
    std::printf("os dez piores (max, media):\n");
    for (std::size_t i = 0; i < rows.size() && i < 10; ++i) {
        std::printf("  %-60s max %d  media %.4f  (%u,%u c%d)\n", rows[i].name.c_str(),
                    rows[i].s.max, rows[i].s.mean, rows[i].s.worstX, rows[i].s.worstY,
                    rows[i].s.worstChannel);
    }
    std::printf("%d documento(s) fora do teto ou com decisao divergente\n", failed);
    return failed ? 1 : 0;
}
