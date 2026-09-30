// icserver -- o nucleo como processo PERSISTENTE, para a casca Tauri (ui/).
//
// O `icrender` abre o dispositivo, renderiza um PNG e morre: cada quadro paga o
// Vulkan, o parse e um render frio. Este processo abre o dispositivo uma vez e
// guarda um `rb::RenderCache` entre quadros, entao uma edicao so refaz o que
// mudou. Desde a G5 o quadro sai de `rb::renderIconGpu` (a cadeia residente;
// `icfidelity` a mede contra `rb::renderIcon`, o gabarito, no corpus inteiro).
// `icserver --cpu` volta ao caminho de CPU. A UI nao desenha nada, so mostra.
//
// Protocolo: uma linha de comando no stdin, uma resposta no stdout.
//
//   open <caminho do .icon>
//       -> "ok\n" | "err <motivo>\n"
//   doc <n>\n<n bytes de icon.json>
//       troca o documento em memoria (as edicoes da UI chegam assim)
//       -> "ok\n" | "err <motivo>\n"
//   render <size> <appearance|-> <idiom|-> <x> <y> <w> <h> [subdivisions] [effects]
//       w = h = 0 e o canvas inteiro; senao, o ladrilho (IconViewport).
//       subdivisions: segmentos por cubica (padrao 16); a UI sobe com o zoom,
//       senao uma curva vira poligono visivel a 8x.
//       effects: 1 (padrao) ou 0 -- o "Liquid Glass Effects Disabled" do
//       `EffectsRenderModePicker`: o quadro sai com `glass` desligado em toda
//       camada, sem tocar no documento.
//       -> "frame <w> <h> <originX> <originY> <ms> <n>\n" + n bytes RGBA8
//          (straight, com dithering: ver `toRgba8Dithered`)
//        | "err <motivo>\n"
//   set <g> <l> <appearance|-> <idiom|-> <prop> <n>\n<n bytes: o valor JSON>
//       escreve `prop` no no (g = -1 e a raiz; l = -1 e o grupo g) sob o
//       escopo, com `icf::setProperty` -- a mesma escrita do inspetor antigo,
//       na grafia da Apple. O valor `null` remove a entrada do escopo.
//       -> "json <n>\n" + o icon.json inteiro | "err <motivo>\n"
//   set ... com um `c` depois de <n>: mesma escrita, sem entrada nova no
//       desfazer -- um arraste e UM passo, nao um por quadro.
//   node <op> <g> <l> <n>\n<n bytes de argumento>
//       a estrutura, com as funcoes de Edit.h: add-group (arg: nome),
//       add-layer (g; arg: image-name, que vira tambem o nome), remove,
//       duplicate, move (arg: -1 ou 1), rename (arg: nome)
//       -> "json <n>\n" + o icon.json | "err <motivo>\n"
//   import <n>\n<n bytes: caminho de um arquivo>
//       copia o arquivo para Assets/ (`IconBundle::importAsset`, no disco ja)
//       -> "asset <nome>\n" | "err <motivo>\n"
//   undo | redo   -> "json <n>\n" + o icon.json | "err nada a desfazer"
//   get           -> "json <n>\n" + o icon.json
//   save          -> "ok\n" | "err <motivo>\n"   (grava no .icon aberto)
//   quit
//
// O stdout e BINARIO: no Windows ele e posto em _O_BINARY, senao cada 0x0A do
// quadro vira 0x0D 0x0A.

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/Parallel.h"
#include "Source/RenderBox/RenderCache.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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

// A conversao para 8 bits da TELA, com dithering. `[ART]` O PNG que o Icon
// Composer exporta tem dithering: numa coluna do fundo `system-dark` do Kenzu
// ele alterna 43/44/43/44 e a maior faixa lisa tem 16 px, onde o arredondamento
// seco (`ick::toRgba8`) da faixas de ate 86 px -- os "degraus no degrade".
//
// O ruido e o interleaved gradient noise nas coordenadas ABSOLUTAS do canvas,
// entao o ladrilho e a base concordam no mesmo ponto e o mesmo documento da
// sempre os mesmos bytes. So a cor: o alfa sai como `toRgba8`. A exportacao
// (Export.cpp / icrender) NAO passa por aqui.
float ditherAt(std::int64_t x, std::int64_t y) {
    const double f = 0.06711056 * static_cast<double>(x) + 0.00583715 * static_cast<double>(y);
    const double g = 52.9829189 * (f - std::floor(f));
    return static_cast<float>(g - std::floor(g)) - 0.5f;
}

std::vector<std::uint8_t> toRgba8Dithered(const std::vector<float>& in, std::uint32_t width,
                                          std::int32_t originX, std::int32_t originY) {
    // Por linha e em paralelo: serial, esta conversao custava mais que o render
    // da GPU inteiro (Kenzu 1024 px: render 14 ms, ida e volta 80 ms). O
    // arredondamento e `+0.5` e truncar, que para valores >= 0 e o mesmo
    // `lround` sem a chamada.
    std::vector<std::uint8_t> out(in.size());
    const std::size_t rows = width ? in.size() / 4 / width : 0;
    rb::parallelRanges(rows, in.size() * 8, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t yy = y0; yy < y1; ++yy) {
            const std::int64_t y = originY + static_cast<std::int64_t>(yy);
            for (std::uint32_t xx = 0; xx < width; ++xx) {
                const std::size_t t = yy * width + xx;
                const float n = ditherAt(originX + static_cast<std::int64_t>(xx), y);
                for (int c = 0; c < 4; ++c) {
                    float v = in[t * 4 + c];
                    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                    float scaled = v * 255.0f;
                    if (c < 3) scaled = scaled + n < 0.0f ? 0.0f : (scaled + n > 255.0f ? 255.0f : scaled + n);
                    out[t * 4 + c] = static_cast<std::uint8_t>(scaled + 0.5f);
                }
            }
        }
    });
    return out;
}

void replyJson(const icf::json::Value& root) {
    const std::string text = icf::json::write(root);
    std::string head = "json " + std::to_string(text.size());
    std::fwrite(head.data(), 1, head.size(), stdout);
    std::fputc('\n', stdout);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    bool gpu = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--cpu") gpu = false;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    auto device = rb::Device::create(rb::DeviceOptions{.validation = false});
    if (!device) {
        fail("no Vulkan device: " + device.error());
        return 2;
    }
    rb::RenderCache cache;
    std::optional<icf::IconBundle> bundle;
    // O desfazer guarda o documento INTEIRO antes de cada escrita: o texto e
    // pequeno, e restaurar o texto e restaurar os bytes, sem inverter edicao.
    std::vector<std::string> undo, redo;
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
            undo.clear();
            redo.clear();
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

        if (cmd == "set") {
            int g = -1, l = -1;
            std::string appearance, idiom, prop;
            std::size_t n = 0;
            in >> g >> l >> appearance >> idiom >> prop >> n;
            std::string coalesce;
            in >> coalesce;
            std::string text(n, '\0');
            std::cin.read(text.data(), static_cast<std::streamsize>(n));
            if (!bundle) {
                fail("set antes de open");
                continue;
            }
            icf::Context scope;
            if (appearance != "-") {
                auto a = icf::appearanceFromString(appearance);
                if (!a) {
                    fail("aparencia desconhecida: " + appearance);
                    continue;
                }
                scope.appearance = *a;
            }
            if (idiom != "-") {
                auto d = icf::idiomFromString(idiom);
                if (!d) {
                    fail("idioma desconhecido: " + idiom);
                    continue;
                }
                scope.idiom = *d;
            }
            auto value = icf::json::parse(text);
            if (!value) {
                fail("valor nao le como JSON: " + text);
                continue;
            }
            icf::NodePath path;
            if (g >= 0) path.group = static_cast<std::size_t>(g);
            if (g >= 0 && l >= 0) path.layer = static_cast<std::size_t>(l);
            icf::json::Value* node = icf::nodeAt(bundle->json(), path);
            if (!node) {
                fail("no inexistente");
                continue;
            }
            if (coalesce != "c" || undo.empty()) undo.push_back(icf::json::write(bundle->json()));
            redo.clear();
            std::optional<icf::json::Value> v;
            if (value->kind() != icf::json::Value::Kind::Null) v = std::move(*value);
            icf::setProperty(*node, prop, scope, std::move(v));
            replyJson(bundle->json());
            continue;
        }

        if (cmd == "node") {
            std::string op;
            int g = -1, l = -1;
            std::size_t n = 0;
            in >> op >> g >> l >> n;
            std::string arg(n, '\0');
            std::cin.read(arg.data(), static_cast<std::streamsize>(n));
            if (!bundle) {
                fail("node antes de open");
                continue;
            }
            icf::json::Value& root = bundle->json();
            icf::NodePath path;
            if (g >= 0) path.group = static_cast<std::size_t>(g);
            if (g >= 0 && l >= 0) path.layer = static_cast<std::size_t>(l);
            const std::string before = icf::json::write(root);
            bool done = false;
            if (op == "add-group") {
                icf::addGroup(root, arg.empty() ? "Group" : arg);
                done = true;
            } else if (op == "add-layer") {
                icf::json::Value* group = icf::nodeAt(root, icf::NodePath{path.group, std::nullopt});
                if (group && path.group) {
                    std::string name = arg;
                    const std::size_t dot = name.find_last_of('.');
                    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
                    icf::addLayer(*group, name, arg);
                    done = true;
                }
            } else if (op == "remove") {
                done = icf::removeNode(root, path);
            } else if (op == "duplicate") {
                done = icf::duplicateNode(root, path);
            } else if (op == "move") {
                done = icf::moveNode(root, path, std::atoi(arg.c_str()));
            } else if (op == "rename") {
                done = icf::setName(root, path, arg);
            } else {
                fail("operacao desconhecida: " + op);
                continue;
            }
            if (!done) {
                fail(op + ": no inexistente ou na borda");
                continue;
            }
            undo.push_back(before);
            redo.clear();
            replyJson(root);
            continue;
        }

        if (cmd == "import") {
            std::size_t n = 0;
            in >> n;
            std::string file(n, '\0');
            std::cin.read(file.data(), static_cast<std::streamsize>(n));
            if (!bundle) {
                fail("import antes de open");
                continue;
            }
            const std::string why = bundle->importAsset(std::filesystem::u8path(file));
            if (!why.empty()) {
                fail(why);
                continue;
            }
            const std::u8string name = std::filesystem::u8path(file).filename().u8string();
            reply("asset " + std::string(name.begin(), name.end()));
            continue;
        }

        if (cmd == "undo" || cmd == "redo") {
            auto& from = cmd == "undo" ? undo : redo;
            auto& to = cmd == "undo" ? redo : undo;
            if (!bundle || from.empty()) {
                fail(cmd == "undo" ? "nada a desfazer" : "nada a refazer");
                continue;
            }
            auto restored = icf::json::parse(from.back());
            if (!restored) {
                fail("historico ilegivel");
                continue;
            }
            to.push_back(icf::json::write(bundle->json()));
            from.pop_back();
            bundle->json() = std::move(*restored);
            replyJson(bundle->json());
            continue;
        }

        if (cmd == "get") {
            if (!bundle) {
                fail("get antes de open");
                continue;
            }
            replyJson(bundle->json());
            continue;
        }

        if (cmd == "save") {
            if (!bundle) {
                fail("save antes de open");
                continue;
            }
            const std::string why = bundle->save();
            if (why.empty()) reply("ok");
            else fail(why);
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
            int effects = 1;
            if (!(in >> effects)) effects = 1;
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
            // Efeitos desligados: uma COPIA com `glass` falso em toda camada (a
            // lista de especializacao de `glass` sai junto, senao ela ganharia
            // da chave simples). O documento aberto nao muda.
            std::optional<icf::IconBundle> flat;
            if (effects == 0) {
                flat = bundle->clone();
                if (icf::json::Value* groups = flat->json().find("groups")) {
                    for (icf::json::Value& grp : groups->elements()) {
                        icf::json::Value* layers = grp.find("layers");
                        if (!layers) continue;
                        for (icf::json::Value& layer : layers->elements()) {
                            auto& m = layer.members();
                            std::erase_if(m, [](const auto& kv) { return kv.first == "glass-specializations"; });
                            layer.set("glass", icf::json::Value::boolean(false));
                        }
                    }
                }
            }
            const icf::IconBundle& target = flat ? *flat : *bundle;
            auto icon = gpu ? rb::renderIconGpu(*device, target, io)
                            : rb::renderIcon(*device, target, io);
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
            const std::vector<std::uint8_t> bytes =
                toRgba8Dithered(icon->rgba, icon->width, icon->originX, icon->originY);
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
