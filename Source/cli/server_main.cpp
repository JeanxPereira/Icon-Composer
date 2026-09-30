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
//       tint <r> <g> <b> <saturation>, depois de effects: a rendicao Tinted
//       Dark (`rb::applyTintedDark`), com a aparencia `tinted` do documento.
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
//       duplicate, move (arg: delta; |delta| > 1 vai ate a borda), rename (arg: nome)
//       -> "json <n>\n" + o icon.json | "err <motivo>\n"
//   import <n>\n<n bytes: caminho de um arquivo>
//       copia o arquivo para Assets/ (`IconBundle::importAsset`, no disco ja)
//       -> "asset <nome>\n" | "err <motivo>\n"
//   undo | redo   -> "json <n>\n" + o icon.json | "err nada a desfazer"
//   get           -> "json <n>\n" + o icon.json
//   backdrop <w> <h> <n>\n<n bytes RGBA8>
//       o fundo da janela como a tela o mostra, para o Clear -> "ok\n"
//   render ... clear <x> <y> <lado>, depois de effects: o Clear (passe de
//       clarear, `rb::applyClearLightening`), com o quadrado do canvas em
//       pixels do backdrop; a cor sai do fundo transformado e o alfa e a mascara
//   rects <appearance|-> <idiom|->
//       o retangulo de cada camada em pontos do canvas (0..1024), para o
//       destaque da selecao e o clique no canvas
//       -> "json <n>\n" + [{g, l, x0, y0, x1, y1, hidden}, ...]
//   save          -> "ok\n" | "err <motivo>\n"   (grava no .icon aberto)
//   quit
//
// O stdout e BINARIO: no Windows ele e posto em _O_BINARY, senao cada 0x0A do
// quadro vira 0x0D 0x0A.

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/IconComposerFoundation/Values.h"
#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/GpuResident.h"
#include "Source/RenderBox/Parallel.h"
#include "Source/RenderBox/RenderCache.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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

// O AQUECIMENTO, antes do "ready": o estado residente (pipelines e a reserva de
// 1 GB do heap, `rb::gpu::Resident::of`) e um render de um icone minimo -- um
// circulo de vidro com sombra e especular sobre o fundo de sistema -- para que
// cada kernel rode uma vez e o driver compile o que compila no primeiro uso.
// Sem isto o primeiro documento aberto pagava ~430 ms que nao eram dele.
// O icone e escrito numa pasta temporaria e apagado; um erro aqui nao impede
// o servidor de subir, so deixa o primeiro render frio.
void warmUp(rb::Device& device) {
    (void)rb::gpu::Resident::of(device);
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("icserver-warmup-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) + ".icon");
    if (ec || !fs::create_directories(dir / "Assets", ec)) return;
    {
        std::FILE* f = std::fopen((dir / "Assets" / "c.svg").string().c_str(), "wb");
        if (!f) return;
        const char svg[] =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
            "<circle cx=\"50\" cy=\"50\" r=\"40\" fill=\"#ffffff\"/></svg>";
        std::fwrite(svg, 1, sizeof svg - 1, f);
        std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((dir / "icon.json").string().c_str(), "wb");
        if (!f) return;
        const char json[] =
            "{\"fill\":\"system-dark\",\"groups\":[{\"layers\":[{\"image-name\":\"c.svg\","
            "\"name\":\"c\"}],\"shadow\":{\"kind\":\"neutral\",\"opacity\":0.5},"
            "\"specular\":true,\"translucency\":{\"enabled\":true,\"value\":0.5}}],"
            "\"supported-platforms\":{\"squares\":\"shared\"}}";
        std::fwrite(json, 1, sizeof json - 1, f);
        std::fclose(f);
    }
    if (auto b = icf::IconBundle::open(dir)) {
        rb::IconRenderOptions io;
        io.size = 256;
        (void)rb::renderIconGpu(device, *b, io);
    }
    fs::remove_all(dir, ec);
}

// A caixa da arte, como `Session::assetViewBox` (IconComposerKit/Session.cpp):
// o `viewBox` do SVG ou a largura e a altura do IHDR de um PNG. Lida uma vez
// por nome; `assetsGeneration` invalida quando Assets/ muda.
struct BoxCache {
    std::uint64_t generation = ~0ull;
    std::map<std::string, std::optional<icf::svg::ViewBox>, std::less<>> boxes;

    const icf::svg::ViewBox* of(const icf::IconBundle& b, const std::string& name) {
        if (b.assetsGeneration() != generation) {
            boxes.clear();
            generation = b.assetsGeneration();
        }
        auto it = boxes.find(name);
        if (it == boxes.end()) {
            std::optional<icf::svg::ViewBox> box;
            std::ifstream f(b.assetPath(name), std::ios::binary);
            if (f) {
                const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                std::uint32_t w = 0, h = 0;
                if (icf::pngSize(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), w, h))
                    box = icf::svg::ViewBox{0.0, 0.0, static_cast<double>(w), static_cast<double>(h)};
                else
                    box = icf::svg::readViewBox(bytes);
            }
            it = boxes.emplace(name, box).first;
        }
        return it->second ? &*it->second : nullptr;
    }
};

icf::Position positionUnder(const icf::json::Value& node, icf::Context ctx) {
    if (const icf::json::Value* pv = icf::resolve(node, "position", ctx)) {
        if (auto p = icf::positionFrom(*pv)) return *p;
    }
    return icf::Position{};
}

bool hiddenUnder(const icf::json::Value& node, icf::Context ctx) {
    const icf::json::Value* v = icf::resolve(node, "hidden", ctx);
    return v && v->kind() == icf::json::Value::Kind::Bool && v->boolean();
}

// O retangulo de cada camada, em pontos do canvas (0..kCanvasPoints), sob o
// contexto que o canvas renderiza: a aritmetica de `ick::canvasLayerRect`
// (PanelCanvas.cpp), que o caso 12 de test_kit_canvas cobra contra
// `rb::artPlacementRect`. Sem caixa a afirmar vale o canvas inteiro.
std::string layerRects(const icf::IconBundle& b, BoxCache& boxes, icf::Context ctx) {
    const icf::json::Value* groups = b.json().find("groups");
    if (!groups || groups->kind() != icf::json::Value::Kind::Array) return "[]";
    std::string out = "[";
    const auto& gs = groups->elements();
    for (std::size_t g = 0; g < gs.size(); ++g) {
        const icf::Position gp = positionUnder(gs[g], ctx);
        const bool gHidden = hiddenUnder(gs[g], ctx);
        const icf::json::Value* layers = gs[g].find("layers");
        if (!layers || layers->kind() != icf::json::Value::Kind::Array) continue;
        const auto& ls = layers->elements();
        for (std::size_t l = 0; l < ls.size(); ++l) {
            icf::Position p = positionUnder(ls[l], ctx);
            p.translation.x = gp.scale * p.translation.x + gp.translation.x;
            p.translation.y = gp.scale * p.translation.y + gp.translation.y;
            p.scale = gp.scale * p.scale;
            double w = rb::kCanvasPoints, h = rb::kCanvasPoints;
            if (const icf::json::Value* nv = icf::resolve(ls[l], "image-name", ctx);
                nv && nv->kind() == icf::json::Value::Kind::String) {
                if (const icf::svg::ViewBox* box = boxes.of(b, nv->rawString())) {
                    w = box->width > 0.0 ? box->width : 1.0;
                    h = box->height > 0.0 ? box->height : 1.0;
                }
            }
            const double x0 = (rb::kCanvasPoints - w * p.scale) * 0.5 + p.translation.x;
            const double y0 = (rb::kCanvasPoints - h * p.scale) * 0.5 + p.translation.y;
            char item[200];
            std::snprintf(item, sizeof item,
                          "%s{\"g\":%zu,\"l\":%zu,\"x0\":%.3f,\"y0\":%.3f,\"x1\":%.3f,\"y1\":%.3f,\"hidden\":%s}",
                          out.size() > 1 ? "," : "", g, l, x0, y0, x0 + w * p.scale, y0 + h * p.scale,
                          gHidden || hiddenUnder(ls[l], ctx) ? "true" : "false");
            out += item;
        }
    }
    return out + "]";
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
    BoxCache boxes;
    rb::ClearBackdrop backdrop;   // o fundo da tela, para o Clear
    // O desfazer guarda o documento INTEIRO antes de cada escrita: o texto e
    // pequeno, e restaurar o texto e restaurar os bytes, sem inverter edicao.
    std::vector<std::string> undo, redo;
    if (gpu) warmUp(*device);
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
                // |delta| > 1 anda um passo por vez ate a borda (Bring to Front /
                // Send to Back): uma entrada so no desfazer.
                const int delta = std::atoi(arg.c_str());
                const int step = delta < 0 ? -1 : 1;
                for (int i = 0; i != delta; i += step) {
                    if (!icf::moveNode(root, path, step)) break;
                    done = true;
                    if (g >= 0 && l >= 0) path.layer = *path.layer + step;
                    else if (g >= 0) path.group = *path.group + step;
                }
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

        if (cmd == "backdrop") {
            std::uint32_t w = 0, h = 0;
            std::size_t n = 0;
            in >> w >> h >> n;
            std::vector<std::uint8_t> bytes(n);
            std::cin.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(n));
            if (n != static_cast<std::size_t>(w) * h * 4) {
                fail("backdrop: n != w*h*4");
                continue;
            }
            backdrop = rb::ClearBackdrop{w, h, std::move(bytes)};
            reply("ok");
            continue;
        }

        if (cmd == "rects") {
            std::string appearance, idiom;
            in >> appearance >> idiom;
            if (!bundle) {
                fail("rects antes de open");
                continue;
            }
            icf::Context ctx;
            if (appearance != "-") {
                auto a = icf::appearanceFromString(appearance);
                if (!a) {
                    fail("aparencia desconhecida: " + appearance);
                    continue;
                }
                ctx.appearance = *a;
            }
            if (idiom != "-") {
                auto d = icf::idiomFromString(idiom);
                if (!d) {
                    fail("idioma desconhecido: " + idiom);
                    continue;
                }
                ctx.idiom = *d;
            }
            const std::string text = layerRects(*bundle, boxes, ctx);
            reply("json " + std::to_string(text.size()));
            std::fwrite(text.data(), 1, text.size(), stdout);
            std::fflush(stdout);
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
            // Tinted Dark: `tint r g b saturation` depois de `effects`.
            std::optional<rb::IconRenderOptions::TintRecolour> tint;
            std::optional<std::array<double, 3>> clear;   // o quadrado no fundo
            {
                std::string word;
                rb::IconRenderOptions::TintRecolour t;
                std::array<double, 3> sq{};
                if (in >> word) {
                    if (word == "tint" && in >> t.r >> t.g >> t.b >> t.saturation) tint = t;
                    else if (word == "clear" && in >> sq[0] >> sq[1] >> sq[2]) clear = sq;
                }
            }
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
            io.tint = tint;
            // O Clear tambem nao e `.color`: a sombra vira `neutral` (0x49F40).
            // Uma recoloracao identidade liga so esse portao.
            if (clear && !tint) io.tint = rb::IconRenderOptions::TintRecolour{};
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
            if (tint) rb::applyTintedDark(*icon, *tint);
            if (clear) {
                if (backdrop.width == 0) {
                    fail("clear sem backdrop");
                    continue;
                }
                rb::applyClearLightening(*icon, backdrop, (*clear)[0], (*clear)[1], (*clear)[2], size);
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
