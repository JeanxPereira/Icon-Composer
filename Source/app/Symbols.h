#pragma once
// A `SymbolSource` do Kit, feita dos SVG que o Tauri usa (ui/public/apple/
// symbols e custom), rasterizados pela RenderBox.
//
// A rasterizacao e UM job na `JobQueue`, e nao trabalho da thread principal:
// ela usa o `rb::Device`, e na fila ela nunca divide o aparelho com um render
// do canvas (a fila e de uma thread so). O `done` sobe as texturas; ate ele
// rodar, `symbol` devolve invalido e os widgets desenham o fallback.
#include "Source/IconComposerKit/Widgets.h"
#include "Source/RenderBox/Device.h"
#include "Source/app/JobQueue.h"
#include "Source/app/TexturePool.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace icapp {

class AppSymbols : public ick::SymbolSource {
public:
    // Enfileira a rasterizacao de tudo que houver em `appleDir`. `px` e o lado
    // do raster: o maior simbolo desenhado, com folga para encolher liso.
    void schedule(JobQueue& jobs, rb::Device& device, TexturePool& pool, const std::filesystem::path& appleDir,
                  std::uint32_t px);
    ImTextureID symbol(std::string_view name, bool custom) override;
    std::size_t loaded() const { return map_.size(); }

private:
    std::unordered_map<std::string, ImTextureID> map_;
};

// A `ArtSource` do Kit: a arte de cada camada, pedida pela sidebar. Tambem na
// fila de trabalho, um arquivo por job, e conferida pela data do arquivo: uma
// arte reimportada ou editada fora do editor volta nova.
class AppArt : public ick::ArtSource {
public:
    AppArt(JobQueue& jobs, rb::Device& device, TexturePool& pool) : jobs_(jobs), device_(device), pool_(pool) {}
    ick::ArtThumb art(const std::filesystem::path& file) override;

private:
    struct Entry {
        ick::ArtThumb thumb;
        std::filesystem::file_time_type stamp{};
        std::chrono::steady_clock::time_point checked{};
        bool pending = false;
    };
    JobQueue& jobs_;
    rb::Device& device_;
    TexturePool& pool_;
    std::unordered_map<std::string, Entry> map_;
};

}  // namespace icapp
