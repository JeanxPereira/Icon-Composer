// icserver -- o nucleo como processo PERSISTENTE, para a casca Tauri (ui/).
//
// O `icrender` abre o dispositivo, renderiza um PNG e morre: cada quadro paga o
// Vulkan, o parse e um render frio. Este processo abre o dispositivo uma vez e
// guarda um `rb::RenderCache` entre quadros, entao uma edicao so refaz o que
// mudou. Todo pixel continua saindo de `rb::renderIcon`, a mesma cadeia que o
// gate do corpus mede -- a UI nao desenha nada, so mostra.
//
// Protocolo: uma linha de comando no stdin, uma resposta no stdout.
//
//   open <caminho do .icon>
//       -> "ok\n" | "err <motivo>\n"
//   doc <n>\n<n bytes de icon.json>
//       troca o documento em memoria (as edicoes da UI chegam assim)
//       -> "ok\n" | "err <motivo>\n"
//   render <size> <appearance|-> <idiom|-> <x> <y> <w> <h> [subdivisions]
//       w = h = 0 e o canvas inteiro; senao, o ladrilho (IconViewport).
//       subdivisions: segmentos por cubica (padrao 16); a UI sobe com o zoom,
//       senao uma curva vira poligono visivel a 8x.
//       -> "frame <w> <h> <originX> <originY> <ms> <n>\n" + n bytes RGBA8
//          (straight, a mesma conversao de `ick::toRgba8`)
//        | "err <motivo>\n"
//   quit
//
// O stdout e BINARIO: no Windows ele e posto em _O_BINARY, senao cada 0x0A do
// quadro vira 0x0D 0x0A.

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/RenderCache.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void reply(const std::string& line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void fail(const std::string& why) {
    std::string clean = why;
    for (char& c : clean) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    reply("err " + clean);
}

// `ick::toRgba8`, repetida aqui para o servidor nao linkar o Kit (ImGui).
std::vector<std::uint8_t> toRgba8(const std::vector<float>& in) {
    std::vector<std::uint8_t> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        float v = in[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[i] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
    }
    return out;
}

}  // namespace

int main() {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    auto device = rb::Device::create();
    if (!device) {
        fail("no Vulkan device: " + device.error());
        return 2;
    }
    rb::RenderCache cache;
    std::optional<icf::IconBundle> bundle;
    reply("ready");

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;

        if (cmd == "quit") break;

        if (cmd == "open") {
            std::string path;
            std::getline(in >> std::ws, path);
            auto b = icf::IconBundle::open(path);
            if (!b) {
                fail("nao abri o bundle: " + path);
                continue;
            }
            bundle = std::move(*b);
            reply("ok");
            continue;
        }

        if (cmd == "doc") {
            std::size_t n = 0;
            in >> n;
            std::string text(n, '\0');
            std::cin.read(text.data(), static_cast<std::streamsize>(n));
            if (!bundle) {
                fail("doc antes de open");
                continue;
            }
            auto parsed = icf::json::parse(text);
            if (!parsed) {
                fail("icon.json nao le");
                continue;
            }
            bundle->json() = std::move(*parsed);
            reply("ok");
            continue;
        }

        if (cmd == "render") {
            if (!bundle) {
                fail("render antes de open");
                continue;
            }
            std::uint32_t size = 0, w = 0, h = 0;
            std::int32_t x = 0, y = 0;
            std::string appearance, idiom;
            int subdivisions = 16;
            in >> size >> appearance >> idiom >> x >> y >> w >> h;
            if (!(in >> subdivisions)) subdivisions = 16;
            if (subdivisions < 1 || subdivisions > 256) {
                fail("subdivisions fora de 1..256");
                continue;
            }
            if (size == 0 || size > 8192) {
                fail("size fora de 1..8192");
                continue;
            }
            rb::IconRenderOptions io;
            io.size = size;
            io.cache = &cache;
            io.viewport = rb::IconViewport{x, y, w, h};
            io.subdivisions = subdivisions;
            if (appearance != "-") {
                auto a = icf::appearanceFromString(appearance);
                if (!a) {
                    fail("aparencia desconhecida: " + appearance);
                    continue;
                }
                io.context.appearance = *a;
            }
            if (idiom != "-") {
                auto d = icf::idiomFromString(idiom);
                if (!d) {
                    fail("idioma desconhecido: " + idiom);
                    continue;
                }
                io.context.idiom = *d;
            }
            const auto t0 = std::chrono::steady_clock::now();
            auto icon = rb::renderIcon(*device, *bundle, io);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!icon) {
                fail(icon.error());
                continue;
            }
            if (icon->viewportRefused) {
                fail("ladrilho acima do teto de area do render");
                continue;
            }
            const std::vector<std::uint8_t> bytes = toRgba8(icon->rgba);
            char head[160];
            std::snprintf(head, sizeof head, "frame %u %u %d %d %.3f %zu", icon->width, icon->height,
                          icon->originX, icon->originY, ms, bytes.size());
            std::fputs(head, stdout);
            std::fputc('\n', stdout);
            std::fwrite(bytes.data(), 1, bytes.size(), stdout);
            std::fflush(stdout);
            continue;
        }

        fail("comando desconhecido: " + cmd);
    }
    return 0;
}
