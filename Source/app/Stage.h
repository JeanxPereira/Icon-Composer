#pragma once
// A `StageSource` do Kit: a grade do icone e as imagens de fundo do palco, dos
// mesmos arquivos que o Tauri usa (ui/public/apple/custom/appicongrid.*.svg e
// ui/public/apple/backgrounds/*.jpeg).
//
// Tudo na `JobQueue`, como os simbolos: a grade usa o `rb::Device`, e na fila
// ela nunca o divide com um render do canvas. Ate o `done` rodar, `grid` e
// `background` devolvem invalido e o canvas desenha a cor chapada.
//
// Os JPEG sao lidos pelo WIC, entao as imagens de fundo sao so do Windows por
// enquanto; nos outros a lista fica vazia e o palco tem as cores.
#include "Source/IconComposerKit/Widgets.h"
#include "Source/RenderBox/Device.h"
#include "Source/app/JobQueue.h"
#include "Source/app/StageCompositor.h"
#include "Source/app/TexturePool.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace icapp {

class AppStage : public ick::StageSource {
public:
    // `gpu` e o dispositivo da TELA: e nele que o Mono e composto, por quadro
    // (StageCompositor.h).
    AppStage(JobQueue& jobs, rb::Device& device, TexturePool& pool, const Gpu& gpu)
        : jobs_(jobs), device_(device), pool_(pool), compositor_(gpu, pool) {}

    // Anota a grade e as imagens de `appleDir/backgrounds`, em ordem de nome.
    // Nada e lido aqui: cada uma entra na fila quando e pedida pela primeira vez.
    void schedule(const std::filesystem::path& appleDir);
    // "Add Background...": acrescenta o arquivo a lista e devolve o indice dele.
    // A imagem chega depois, pela fila; se nao abrir, o motivo vai para o stderr
    // e a entrada fica sem textura (o canvas mostra a cor).
    int add(const std::filesystem::path& file);

    bool composes() override { return compositor_.ready(); }
    bool composeMono(ImDrawList* dl, const ick::MonoStageDraw& d) override;
    ImTextureID grid(bool watch) override;
    int backgrounds() override { return static_cast<int>(images_.size()); }
    ick::ArtThumb background(int index) override;
    std::string backgroundName(int index) override;
    std::shared_ptr<const ick::StagePixels> pixels(int index) override;

private:
    struct Image {
        std::string name;
        std::filesystem::path file;
        ick::ArtThumb thumb;
        std::shared_ptr<const ick::StagePixels> pixels;
        bool requested = false;
    };
    void want(int index);

    JobQueue& jobs_;
    rb::Device& device_;
    TexturePool& pool_;
    StageCompositor compositor_;
    ImTextureID grid_[2] = {ImTextureID_Invalid, ImTextureID_Invalid};
    std::filesystem::path gridFiles_[2];
    bool gridRequested_[2] = {false, false};
    std::vector<Image> images_;
};

}  // namespace icapp
