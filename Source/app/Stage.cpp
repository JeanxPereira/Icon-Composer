#include "Source/app/Stage.h"

#include "Source/CoreSVG/Document.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/SvgRenderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Os GUID do WIC definidos NESTA unidade (`selectany`), em vez de depender de
// qual biblioteca de importacao do toolchain os carrega.
#include <initguid.h>
#include <wincodec.h>
#endif

namespace icapp {
namespace {

namespace fs = std::filesystem;

// O lado maior de uma imagem de fundo na memoria. As seis do alvo sao 2048 x
// 2048, 16 MB de textura cada; sao degrades suaves, e a 1024 nao se ve a
// diferenca atras de um icone.
constexpr std::uint32_t kMaxSide = 1024;
// A grade e uma mascara sobre o quadrado do icone: 1024, o tamanho do canvas
// em pontos, para as linhas de 4/1028 ficarem finas tambem com zoom.
constexpr std::uint32_t kGridSide = 1024;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "1 - sine-purple-orange.jpeg" -> "sine-purple-orange", como o `title` do
// Canvas.tsx (`replace(/^\d - /, "")`).
std::string displayName(const fs::path& file) {
    std::string stem = file.stem().string();
    std::size_t i = 0;
    while (i < stem.size() && std::isdigit(static_cast<unsigned char>(stem[i]))) ++i;
    if (i > 0 && stem.compare(i, 3, " - ") == 0) stem.erase(0, i + 3);
    return stem;
}

#if defined(_WIN32)

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Qualquer formato que o WIC leia (jpeg, png, bmp, tiff, heic com o codec),
// reduzido para caber em `kMaxSide` e convertido para R8G8B8A8 opaco.
std::shared_ptr<ick::StagePixels> decode(const fs::path& file, std::string* why) {
    // Na thread de trabalho nao ha apartamento; na principal ja ha um de outro
    // tipo (`RPC_E_CHANGED_MODE`), e ele serve -- so nao e nosso para fechar.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool balance = SUCCEEDED(init);

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    auto out = std::make_shared<ick::StagePixels>();
    const char* step = "the WIC factory could not be created";

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                  reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr)) {
        step = "not an image WIC can open";
        hr = factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                &decoder);
    }
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&w, &h);
    if (SUCCEEDED(hr) && (w == 0 || h == 0)) hr = E_FAIL;

    IWICBitmapSource* source = frame;
    if (SUCCEEDED(hr) && std::max(w, h) > kMaxSide) {
        step = "the image could not be scaled";
        const double k = static_cast<double>(kMaxSide) / static_cast<double>(std::max(w, h));
        const UINT sw = static_cast<UINT>(std::max(1L, std::lround(w * k)));
        const UINT sh = static_cast<UINT>(std::max(1L, std::lround(h * k)));
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) hr = scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant);
        if (SUCCEEDED(hr)) {
            source = scaler;
            w = sw;
            h = sh;
        }
    }
    if (SUCCEEDED(hr)) {
        step = "the image could not be converted to RGBA";
        hr = factory->CreateFormatConverter(&converter);
    }
    // BGRA e o formato que todo WIC tem; a troca para RGBA e feita abaixo.
    if (SUCCEEDED(hr))
        hr = converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        out->rgba.resize(static_cast<std::size_t>(w) * h * 4);
        hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(out->rgba.size()), out->rgba.data());
    }
    if (SUCCEEDED(hr)) {
        out->width = w;
        out->height = h;
        for (std::size_t i = 0; i < out->rgba.size(); i += 4) {
            std::swap(out->rgba[i], out->rgba[i + 2]);
            // Um fundo e opaco: o alfa de um PNG nao deixa a janela aparecer.
            out->rgba[i + 3] = 255;
        }
    } else {
        out.reset();
        if (why) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s (HRESULT 0x%08lX)", step, static_cast<unsigned long>(hr));
            *why = buf;
        }
    }

    release(converter);
    release(scaler);
    release(frame);
    release(decoder);
    release(factory);
    if (balance) CoUninitialize();
    return out;
}

#else

// Sem WIC: so o PNG, que a Foundation le.
std::shared_ptr<ick::StagePixels> decode(const fs::path& file, std::string* why) {
    if (lower(file.extension().string()) != ".png") {
        if (why) *why = "only png backgrounds are read on this system";
        return nullptr;
    }
    const icf::DecodedPng png = icf::readPng(file.string());
    if (!png.error.empty() || png.width == 0) {
        if (why) *why = png.error.empty() ? "empty png" : png.error;
        return nullptr;
    }
    auto out = std::make_shared<ick::StagePixels>();
    const std::uint32_t step = std::max<std::uint32_t>(1, (std::max(png.width, png.height) + kMaxSide - 1) / kMaxSide);
    out->width = std::max<std::uint32_t>(1, png.width / step);
    out->height = std::max<std::uint32_t>(1, png.height / step);
    out->rgba.resize(static_cast<std::size_t>(out->width) * out->height * 4);
    for (std::uint32_t y = 0; y < out->height; ++y)
        for (std::uint32_t x = 0; x < out->width; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y) * step * png.width + x * step) * 4;
            std::uint8_t* dst = &out->rgba[(static_cast<std::size_t>(y) * out->width + x) * 4];
            for (int c = 0; c < 3; ++c)
                dst[c] = static_cast<std::uint8_t>(std::lround(std::clamp(png.rgba[src + c], 0.0f, 1.0f) * 255.0f));
            dst[3] = 255;
        }
    return out;
}

#endif

}  // namespace

void AppStage::schedule(const fs::path& appleDir) {
    if (appleDir.empty()) return;
    gridFiles_[0] = appleDir / "custom" / "appicongrid.ios.svg";
    gridFiles_[1] = appleDir / "custom" / "appicongrid.watchos.svg";

    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(appleDir / "backgrounds", ec)) {
        const std::string ext = lower(e.path().extension().string());
        if (ext == ".jpeg" || ext == ".jpg" || ext == ".png") files.push_back(e.path());
    }
    // "1 - ...", "2 - ...": a ordem do nome e a ordem do alvo.
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) images_.push_back(Image{displayName(f), f, {}, nullptr, false});
}

int AppStage::add(const fs::path& file) {
    images_.push_back(Image{displayName(file), file, {}, nullptr, false});
    return static_cast<int>(images_.size()) - 1;
}

// SO QUANDO ALGUEM PEDE. Ler as seis na partida seria meia duzia de JPEG de
// 2048 px na fila ANTES do primeiro render do canvas; assim a primeira (a da
// amostra da barra) entra atras dele, e as outras quando o popover abre.
void AppStage::want(int index) {
    if (index < 0 || index >= static_cast<int>(images_.size())) return;
    Image& image = images_[static_cast<std::size_t>(index)];
    if (image.requested) return;
    image.requested = true;
    struct Out {
        std::shared_ptr<ick::StagePixels> pixels;
        std::string why;
    };
    auto out = std::make_shared<Out>();
    const fs::path file = image.file;
    jobs_.submit([out, file] { out->pixels = decode(file, &out->why); },
                 [this, index, out, file] {
                     if (!out->pixels) {
                         std::fprintf(stderr, "IconComposer: background %s: %s\n", file.string().c_str(),
                                      out->why.c_str());
                         return;
                     }
                     Image& done = images_[static_cast<std::size_t>(index)];
                     done.thumb.texture =
                         pool_.create(out->pixels->width, out->pixels->height, out->pixels->rgba.data(), nullptr);
                     done.thumb.width = static_cast<float>(out->pixels->width);
                     done.thumb.height = static_cast<float>(out->pixels->height);
                     done.pixels = std::move(out->pixels);
                     // E para o compositor do palco, com os mips do desfoque.
                     compositor_.setBackdrop(index, done.pixels);
                 });
}

bool AppStage::composeMono(ImDrawList* dl, const ick::MonoStageDraw& d) {
    // A imagem de fundo entra na fila aqui tambem: o canvas a pede para pintar
    // o palco, mas o compositor nao pode depender da ordem dessa chamada.
    if (d.background.kind == ick::StageBackground::Kind::Image) want(d.background.image);
    return compositor_.compose(dl, d);
}

ick::ArtThumb AppStage::background(int index) {
    if (index < 0 || index >= static_cast<int>(images_.size())) return {};
    want(index);
    return images_[static_cast<std::size_t>(index)].thumb;
}

std::string AppStage::backgroundName(int index) {
    if (index < 0 || index >= static_cast<int>(images_.size())) return {};
    return images_[static_cast<std::size_t>(index)].name;
}

std::shared_ptr<const ick::StagePixels> AppStage::pixels(int index) {
    if (index < 0 || index >= static_cast<int>(images_.size())) return nullptr;
    want(index);
    return images_[static_cast<std::size_t>(index)].pixels;
}

ImTextureID AppStage::grid(bool watch) {
    const int which = watch ? 1 : 0;
    if (!gridRequested_[which] && !gridFiles_[which].empty()) {
        gridRequested_[which] = true;
        struct Out {
            std::uint32_t w = 0, h = 0;
            std::vector<std::uint8_t> rgba;
        };
        auto out = std::make_shared<Out>();
        const fs::path file = gridFiles_[which];
        jobs_.submit(
            [out, file, this] {
                std::ifstream f(file, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                auto doc = icf::svg::SvgDocument::parse(text);
                if (!doc) return;
                rb::RenderOptions o;
                o.width = kGridSide;
                o.height = kGridSide;
                auto img = rb::renderSvg(device_, *doc, o);
                if (!img || img->width == 0) return;
                out->w = img->width;
                out->h = img->height;
                // MASCARA BRANCA, como os simbolos: a cor (preta, ou branca no
                // estilo Light) entra como tinta no canvas.
                out->rgba.resize(static_cast<std::size_t>(img->width) * img->height * 4);
                for (std::size_t i = 0, n = out->rgba.size() / 4; i < n; ++i) {
                    out->rgba[i * 4 + 0] = 255;
                    out->rgba[i * 4 + 1] = 255;
                    out->rgba[i * 4 + 2] = 255;
                    out->rgba[i * 4 + 3] = static_cast<std::uint8_t>(
                        std::lround(std::clamp(img->rgba[i * 4 + 3], 0.0f, 1.0f) * 255.0f));
                }
            },
            [this, which, out, file] {
                if (out->rgba.empty()) {
                    std::fprintf(stderr, "IconComposer: grid %s could not be rasterised\n", file.string().c_str());
                    return;
                }
                grid_[which] = pool_.create(out->w, out->h, out->rgba.data(), nullptr);
            });
    }
    return grid_[which];
}

}  // namespace icapp
