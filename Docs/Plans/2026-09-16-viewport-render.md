# Render de viewport — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Este plano NÃO é TDD (decidido em 15/09/2026).** Nenhuma task pede teste escrito antes do código. A ÚNICA rede que este plano escreve é o **gate do invariante**, e ela não é teste de rotina: é a medição que a spec define como critério de acerto do desenho. Sem ela, "a margem basta?" volta a ser aposta. O resto segue implementar → compilar → rodar o gate filtrado → commitar.

**Goal:** o zoom do canvas passa a renderizar o retângulo visível na resolução da tela, em vez de esticar uma textura de 512, e o resultado é idêntico, pixel a pixel, ao recorte de um render cheio.

**Architecture:** `IconRenderOptions` ganha um `IconViewport`. `renderIcon` planeja um buffer `(viewport ⊕ margem) ∩ canvas`, alinhado à escada do desfoque. Todo passo de CPU amostra em coordenada ABSOLUTA da grade de `size` e só desloca o índice. O único passo que translada geometria é o desenho SVG na GPU. O Kit pede um ladrilho quando o pan e o zoom param, e desenha a textura no retângulo que ela ocupa.

**Tech Stack:** C++23, CMake + Ninja, MinGW g++ 13 (preset `mingw`), Vulkan headless (`RenderBox`), Dear ImGui (Kit).

**Spec:** `Docs/Specs/2026-09-16-viewport-render.md`. Leia a spec inteira antes da primeira task. Cada task diz qual seção ela implementa.

## Global Constraints

- **O invariante:** um render de viewport é IGUAL ao recorte correspondente de um render cheio na mesma `size`, float a float, com tolerância ZERO. Nenhuma task afrouxa isso. Se o gate acusar, o relatório diz de que tipo é a diferença. A tolerância não se mexe.
- **Coordenada absoluta:** um passo de CPU avalia no ponto `(x + origem) + 0.5` da grade de `size`, com a geometria intocada, e só o índice no buffer é deslocado (`x_local = x_absoluto - origem`). Transladar geometria na CPU é proibido. A exceção é o `PathGlobals` do desenho SVG na GPU (spec, "O invariante").
- **O padrão reduz a hoje:** com `IconViewport{}`, o buffer é o canvas inteiro, origem 0, e todo sítio executa exatamente a aritmética de antes.
- `IconComposerFoundation` não linka GPU nem UI. Só `Source/app` linka Onyx.
- **Teste não é pré-requisito**, exceto o gate (acima). A suíte inteira NÃO roda por task (~48 s). Roda-se o gate filtrado.
- A varredura de mutação (`scripts/gate-m1.ps1`) não roda.
- Comandos, sempre da raiz do repo (Git Bash):
  - configurar: `cmake --preset mingw`
  - construir o app e as libs: `cmake --build --preset mingw`
  - construir a suíte: `cmake --build --preset mingw --target ic_tests`
  - rodar o gate: `IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe viewport` (o filtro é substring do nome do caso)
- Num worktree, o corpus não existe: use `IC_CORPUS_DIR=D:/CodingProjects/Icon-Composer/References/corpus`. Antes de começar, confira `git log --oneline -1` contra a `main`; se estiver atrás, rode `git merge --ff-only main`.
- Se `.git/index.lock` existir sem processo git vivo (`Get-CimInstance Win32_Process -Filter "Name='git.exe'"`), o lock é órfão e pode ser removido.
- Mensagens de commit em português, uma frase que diz o que mudou e por quê, **sem nenhuma atribuição a IA**.
- Comentários citam a spec e as medições, nunca este plano. Siga a densidade de comentário do arquivo que estiver editando.

---

## Estrutura de arquivos

| arquivo | responsabilidade |
|---|---|
| `Source/RenderBox/PixelGrid.h` | **criar**: `PixelGrid`, os três significados de `size` separados |
| `Source/RenderBox/ViewportPlan.h/.cpp` | **criar**: alcance do documento, alinhamento da escada, o buffer planejado, o teto |
| `Source/RenderBox/IconRenderer.h/.cpp` | **modificar**: `IconViewport`, o eco em `RenderedIcon`, a grade em todos os sítios, o recorte final |
| `Source/RenderBox/ChicletShape.h/.cpp` | **modificar**: `chicletCoverage`/`clipToChiclet` sobre uma `PixelGrid` |
| `Source/RenderBox/ChicletHighlights.h/.cpp` | **modificar**: `drawChicletHighlights` sobre uma `PixelGrid` |
| `Source/RenderBox/DistanceField.h/.cpp` | **modificar**: origem em `FieldOptions` e em `FieldImage`; varredura absoluta |
| `Source/RenderBox/GlassTranslucency.cpp` | **modificar**: `py` absoluto |
| `Source/RenderBox/DisplacementOracle.h/.cpp` | **modificar**: `SampledImage` com origem e extensão do buffer |
| `Source/RenderBox/GlassLayer.h/.cpp` | **modificar**: `glassOver` sobre uma `PixelGrid` |
| `Source/RenderBox/CMakeLists.txt` | **modificar**: `ViewportPlan.cpp` |
| `Tests/test_viewport_render.cpp` | **criar**: o gate |
| `Tests/CMakeLists.txt` | **modificar**: o gate entra na suíte |
| `Source/IconComposerKit/Tile.h` | **criar**: `TileRect` |
| `Source/IconComposerKit/Ports.h` | **modificar**: pedido e resultado carregam o ladrilho |
| `Source/IconComposerKit/Session.h` | **modificar**: `ViewContext` ganha o ladrilho corrente |
| `Source/IconComposerKit/RenderCoordinator.h/.cpp` | **modificar**: a chave ganha o ladrilho |
| `Source/IconComposerKit/Panels.h`, `PanelCanvas.cpp` | **modificar**: régua na `size` base, desenho no retângulo do ladrilho, pedido com o pan parado |
| `Source/app/OnyxPorts.cpp` | **modificar**: o job passa o viewport e cai para a base acima do teto |
| `Docs/Specs/2026-09-16-viewport-render.md` | **modificar**: a margem medida |

---

### Task 1: o contrato e o gate

Implementa a spec, "O que muda no contrato" e "O invariante". Nesta task, o viewport é honrado **da forma errada de propósito**: `renderIcon` desenha o canvas inteiro e recorta a extensão pedida a partir de `(0,0)`, ignorando a origem. É isso que faz o gate falhar pelo motivo certo (o recorte) e não por uma dimensão que não bate.

**Files:**
- Create: `Source/RenderBox/PixelGrid.h`
- Modify: `Source/RenderBox/IconRenderer.h` (structs em 106-203)
- Modify: `Source/RenderBox/IconRenderer.cpp:587-593` e o fim de `renderIcon` (1631-1641)
- Create: `Tests/test_viewport_render.cpp`
- Modify: `Tests/CMakeLists.txt` (lista de fontes, depois de `test_icon_render.cpp`)

**Interfaces:**
- Produces:
  - `struct rb::PixelGrid { uint32 size; int32 originX, originY; uint32 width, height; static PixelGrid full(uint32); size_t texels() const; bool isFull() const; }`
  - `struct rb::IconViewport { int32 originX, originY; uint32 width, height; }` (0 == `size`)
  - `IconRenderOptions::viewport`
  - `RenderedIcon::size`, `RenderedIcon::originX/originY` (onde `rgba` fica na grade), `RenderedIcon::buffer` (a `PixelGrid` em que o render rodou), `RenderedIcon::viewportRefused`
  - no teste: `ViewportDiff diffAgainstCrop(const RenderedIcon& full, const RenderedIcon& part)`

- [ ] **Step 1: criar `PixelGrid.h`**

```c++
#pragma once
// A grade de pixels em que um render roda.
//
// `IconRenderOptions::size` carregava TRES significados ao mesmo tempo: a
// resolucao do canvas (quantos pixels valem os 1024 pontos), a dimensao do
// buffer e a origem implicita em (0,0). Esta struct os separa (spec
// 2026-09-16, "O que muda no contrato"). Uma funcao que recebe uma `PixelGrid`
// e diz que usa extensao ou origem; uma que recebe o escalar `size` usa escala
// e nada mais -- e e o compilador quem enumera os sitios que mudaram.
#include <cstddef>
#include <cstdint>

namespace rb {

struct PixelGrid {
    std::uint32_t size = 0;                  // 1024 pontos de canvas valem `size` pixels
    std::int32_t originX = 0, originY = 0;   // canto do buffer, na grade de `size`
    std::uint32_t width = 0, height = 0;     // extensao do buffer

    static PixelGrid full(std::uint32_t s) { return PixelGrid{s, 0, 0, s, s}; }
    std::size_t texels() const { return static_cast<std::size_t>(width) * height; }
    bool isFull() const {
        return originX == 0 && originY == 0 && width == size && height == size;
    }
};

}  // namespace rb
```

- [ ] **Step 2: o contrato em `IconRenderer.h`**

Inclua `"Source/RenderBox/PixelGrid.h"`. Antes de `struct IconRenderOptions`, cole a struct da spec:

```c++
// O retangulo que o chamador quer ver, em pixels da grade que `size` define
// (spec 2026-09-16). O padrao e o canvas inteiro, e nesse caso todo sitio se
// reduz a aritmetica de antes.
//
// O INVARIANTE: um render de viewport e IGUAL, float a float, ao recorte
// correspondente de um render cheio na mesma `size`. `Tests/test_viewport_render.cpp`
// e quem cobra.
struct IconViewport {
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::uint32_t width = 0;    // 0 == `size`
    std::uint32_t height = 0;   // 0 == `size`
};
```

Em `IconRenderOptions`, depois de `sizeClass`:

```c++
    IconViewport viewport;
```

Em `RenderedIcon`, logo depois de `rgba`:

```c++
    // Onde `rgba` fica na grade: `width` x `height` pixels a partir de
    // (`originX`, `originY`), numa grade de `size`. Num render cheio, a origem e
    // zero e `width == height == size`.
    std::uint32_t size = 0;
    std::int32_t originX = 0, originY = 0;
    // O buffer em que o render RODOU, margem incluida. Existe para o
    // diagnostico do gate: uma diferenca colada numa borda interna deste
    // retangulo e margem curta.
    PixelGrid buffer;
    // O buffer planejado passou do teto de area (spec, "O teto de area"). Nada
    // foi desenhado; quem pediu decide se cai para a resolucao base.
    bool viewportRefused = false;
```

- [ ] **Step 3: o recorte errado de propósito em `renderIcon`**

Em `IconRenderer.cpp`, no começo de `renderIcon` (logo depois do teste de `size == 0`), resolva a extensão e valide:

```c++
    const std::uint32_t viewW = options.viewport.width ? options.viewport.width : options.size;
    const std::uint32_t viewH = options.viewport.height ? options.viewport.height : options.size;
    if (options.viewport.originX < 0 || options.viewport.originY < 0 ||
        static_cast<std::uint64_t>(options.viewport.originX) + viewW > options.size ||
        static_cast<std::uint64_t>(options.viewport.originY) + viewH > options.size) {
        return std::unexpected("viewport fora do canvas");
    }
```

Troque `out.width = out.height = options.size;` por:

```c++
    out.size = options.size;
    out.originX = options.viewport.originX;
    out.originY = options.viewport.originY;
    out.buffer = PixelGrid::full(options.size);
```

No fim, troque o laço de des-premultiplicação para recortar (Task 1: a origem do recorte é **(0,0)**, deliberadamente, e a Task 3 corrige):

```c++
    // TEMPORARIO (Task 1 do plano de 16/09): recorta a partir de (0,0) e
    // ignora a origem -- e o que faz o gate falhar pelo motivo certo.
    const std::int32_t cropX = 0, cropY = 0;
    out.width = viewW;
    out.height = viewH;
    out.rgba.assign(static_cast<std::size_t>(viewW) * viewH * 4, 0.0f);
    for (std::uint32_t y = 0; y < viewH; ++y) {
        for (std::uint32_t x = 0; x < viewW; ++x) {
            const std::size_t s =
                ((static_cast<std::size_t>(y) + cropY) * out.buffer.width + x + cropX) * 4;
            const std::size_t d = (static_cast<std::size_t>(y) * viewW + x) * 4;
            const float a = acc[s + 3];
            for (int k = 0; k < 3; ++k) out.rgba[d + k] = a > 0.0f ? acc[s + k] / a : 0.0f;
            out.rgba[d + 3] = a;
        }
    }
    return out;
```

O comentário "TEMPORARIO" é o único deste plano que cita o plano, e ele sai na Task 3.

- [ ] **Step 4: escrever o gate**

Crie `Tests/test_viewport_render.cpp`. Os helpers de `test_icon_render.cpp` ficam num namespace anônimo, então o gate tem os próprios, pequenos:

```c++
// O gate do render de viewport (spec 2026-09-16, "O invariante que governa o
// desenho"): um render de viewport e IGUAL ao recorte correspondente de um
// render cheio na mesma `size`, float a float, tolerancia ZERO.
//
// Quando nao e, o relatorio diz DE QUE TIPO e a diferenca: delta grande colado
// numa borda interna do buffer e margem curta; delta na ordem de ULP espalhado
// pelas bordas antialiasadas e aritmetica da GPU. So o primeiro e o desenho
// errado -- e nenhum dos dois se resolve afrouxando a tolerancia.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

Device& gpu() {
    static Device* d = [] {
        auto made = Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<Device*>(nullptr);
        }
        return new Device(std::move(*made));
    }();
    static Device dead;
    return d ? *d : dead;
}

struct ViewportDiff {
    std::size_t differing = 0;
    float maxAbs = 0.0f;
    std::uint32_t worstX = 0, worstY = 0;   // na grade de `size`
    // Distancia do pior pixel ate a borda do buffer mais proxima que NAO
    // coincide com a borda do canvas. Maximo quando o buffer e o canvas.
    std::uint32_t worstInnerEdge = std::numeric_limits<std::uint32_t>::max();
};

ViewportDiff diffAgainstCrop(const RenderedIcon& full, const RenderedIcon& part) {
    ViewportDiff d;
    const PixelGrid& b = part.buffer;
    for (std::uint32_t y = 0; y < part.height; ++y) {
        for (std::uint32_t x = 0; x < part.width; ++x) {
            const std::uint32_t gx = static_cast<std::uint32_t>(part.originX) + x;
            const std::uint32_t gy = static_cast<std::uint32_t>(part.originY) + y;
            const std::size_t pi = (static_cast<std::size_t>(y) * part.width + x) * 4;
            const std::size_t fi = (static_cast<std::size_t>(gy) * full.width + gx) * 4;
            float worst = 0.0f;
            for (int k = 0; k < 4; ++k) {
                worst = std::max(worst, std::fabs(part.rgba[pi + k] - full.rgba[fi + k]));
            }
            if (worst == 0.0f) continue;
            ++d.differing;
            if (worst <= d.maxAbs) continue;
            d.maxAbs = worst;
            d.worstX = gx;
            d.worstY = gy;
            std::uint32_t edge = std::numeric_limits<std::uint32_t>::max();
            const auto bx0 = static_cast<std::uint32_t>(b.originX);
            const auto by0 = static_cast<std::uint32_t>(b.originY);
            if (bx0 > 0) edge = std::min(edge, gx - bx0);
            if (by0 > 0) edge = std::min(edge, gy - by0);
            if (bx0 + b.width < part.size) edge = std::min(edge, bx0 + b.width - 1 - gx);
            if (by0 + b.height < part.size) edge = std::min(edge, by0 + b.height - 1 - gy);
            d.worstInnerEdge = edge;
        }
    }
    return d;
}

// Renderiza cheio e em cada viewport, compara, e imprime o relatorio de
// cada viewport que diferir. Devolve quantos viewports diferiram.
int compareViewports(const icf::IconBundle& bundle, std::uint32_t size,
                     const std::vector<IconViewport>& views, RenderedIcon* fullOut = nullptr) {
    Device& dev = gpu();
    IconRenderOptions o;
    o.size = size;
    auto full = renderIcon(dev, bundle, o);
    if (!full) {
        std::printf("  FAIL render cheio: %s\n", full.error().c_str());
        ++ictest::failures();
        return 1;
    }
    int bad = 0;
    for (const IconViewport& v : views) {
        IconRenderOptions vo = o;
        vo.viewport = v;
        auto part = renderIcon(dev, bundle, vo);
        if (!part) {
            std::printf("  FAIL viewport (%d,%d %ux%u): %s\n", v.originX, v.originY, v.width,
                        v.height, part.error().c_str());
            ++bad;
            continue;
        }
        const ViewportDiff d = diffAgainstCrop(*full, *part);
        if (d.differing == 0) continue;
        ++bad;
        std::printf("  viewport (%d,%d %ux%u) buffer (%d,%d %ux%u): %zu px diferentes, "
                    "max |d| = %.9g em (%u,%u), a %u px da borda interna\n",
                    v.originX, v.originY, v.width, v.height, part->buffer.originX,
                    part->buffer.originY, part->buffer.width, part->buffer.height, d.differing,
                    static_cast<double>(d.maxAbs), d.worstX, d.worstY, d.worstInnerEdge);
    }
    if (fullOut) *fullOut = std::move(*full);
    return bad;
}

// Os quatro viewports da spec, numa grade de `s`: cruzando a borda da forma,
// encostado no canvas, com origem que nao e multipla de 4, e abaixo da forma.
std::vector<IconViewport> gateViewports(std::uint32_t s) {
    const std::int32_t q = static_cast<std::int32_t>(s / 4);
    const std::uint32_t side = s / 3;
    return {
        IconViewport{q - 7, q - 7, side, side},                                    // borda da forma
        IconViewport{0, static_cast<std::int32_t>(s - side), side, side},          // canto do canvas
        IconViewport{q + 3, q * 2 + 1, side + 5, side - 3},                        // origem nao alinhada
        IconViewport{q, static_cast<std::int32_t>(s - s / 5), side, s / 5},        // abaixo da forma
    };
}

}  // namespace

TEST_CASE(viewport_default_is_the_whole_canvas) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Jellify-Music__App__teal-icon-composer");
    REQUIRE(bundle.has_value());
    CHECK_EQ(compareViewports(*bundle, 256, {IconViewport{}}), 0);
}

TEST_CASE(viewport_echoes_where_its_pixels_sit) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Jellify-Music__App__teal-icon-composer");
    REQUIRE(bundle.has_value());
    // O eco e o que o Kit usa para por a textura no lugar; sem ele o gate nao
    // sabe onde recortar o render cheio.
    IconRenderOptions o;
    o.size = 256;
    o.viewport = IconViewport{96, 96, 64, 64};
    auto part = renderIcon(d, *bundle, o);
    REQUIRE(part.has_value());
    CHECK_EQ(part->width, 64u);
    CHECK_EQ(part->originX, 96);
}
```

A prova de que o gate enxerga é o Step 6, rodado à mão.

Em `Tests/CMakeLists.txt`, acrescente `test_viewport_render.cpp` na lista, logo depois de `test_icon_render.cpp`.

- [ ] **Step 5: construir e rodar o gate**

Run: `cmake --build --preset mingw --target ic_tests && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe viewport`
Expected: os dois casos passam (zero `FAIL`).

- [ ] **Step 6: provar que o gate falha num viewport não trivial**

Acrescente **temporariamente**, no fim de `viewport_default_is_the_whole_canvas`:

```c++
    CHECK_EQ(compareViewports(*bundle, 256, gateViewports(256)), 0);
```

Run: o mesmo comando do Step 5.
Expected: `FAIL` com pelo menos três linhas `viewport (...) ... px diferentes`. O canto do canvas é o único que pode por acaso não bater, se o ícone for simétrico. Depois, **remova a linha**: ela volta na Task 3, com o recorte certo.

- [ ] **Step 7: commit**

```bash
git add Source/RenderBox/PixelGrid.h Source/RenderBox/IconRenderer.h Source/RenderBox/IconRenderer.cpp Tests/test_viewport_render.cpp Tests/CMakeLists.txt
git commit -m "render de viewport, passo 1: IconViewport no contrato, RenderedIcon ecoa onde os pixels ficam na grade, e o gate do invariante escrito antes de qualquer sitio mudar -- o recorte ainda ignora a origem de proposito, e o gate foi visto falhar por isso"
```

---

### Task 2: o plano do buffer

Implementa a spec, "A margem, e de onde vem o número": margem por documento com a soma encadeada, `∩ canvas`, alinhamento da escada e o teto. São funções puras, e esta task ainda não as liga no render.

**Files:**
- Create: `Source/RenderBox/ViewportPlan.h`
- Create: `Source/RenderBox/ViewportPlan.cpp`
- Modify: `Source/RenderBox/CMakeLists.txt` (acrescentar `ViewportPlan.cpp` em `add_library(RenderBox ...)`, ao lado de `IconRenderer.cpp`)

**Interfaces:**
- Consumes: `PixelGrid`, `IconViewport` (Task 1); `readGlassMaterial`, `glassMaterialFrom`, `denormaliseGlass` (`GlassMaterial.h`); `shadowDraws`, `shadowGeometry`, `ShadowInputs`, `kShadowBlurSigmaPerRadius` (`GlassShadow.h`); `documentAsksForSpecular` (`GlassSpecular.h`); `blurKernelHalfWidth`, `blurReduceFactorForVariance` (`BlurKernel.h`).
- Produces:
  - `struct rb::DocumentReach { double localPoints; double shadowSigmaPoints; double shadowShiftPoints; double chainedPoints; }`
  - `DocumentReach documentReach(const icf::IconDocument&, const icf::Context&, IconSizeClass)`
  - `std::uint32_t blurLadderAlignment(double sigmaPixels)`
  - `struct rb::ViewportPlan { PixelGrid buffer; PixelGrid crop; std::uint32_t marginPixels; std::uint32_t alignment; bool overCap; }`
  - `Result<ViewportPlan> planViewport(const IconViewport&, std::uint32_t size, const DocumentReach&)`
  - `inline constexpr std::size_t kViewportAreaCap = 16'000'000;`

- [ ] **Step 1: o cabeçalho**

`Source/RenderBox/ViewportPlan.h`:

```c++
#pragma once
// O buffer em que um render de viewport roda (spec 2026-09-16, "A margem").
//
// Tres decisoes, cada uma com o motivo medido:
//
//   1. A MARGEM E POR DOCUMENTO. O pior caso teorico (refracao, 640 pontos)
//      faria todo viewport virar o render cheio. `[ART]` So 2 dos 146
//      documentos do corpus refratam.
//   2. OS ALCANCES SE SOMAM NA CADEIA. A refracao le o backdrop, que contem a
//      sombra de grupos anteriores; um pixel do recorte so e exato com margem
//      >= a banda local MAIS a soma das refracoes.
//   3. O BUFFER E (viewport + margem) ∩ canvas, COM ORIGEM ALINHADA. O
//      desfoque, a reducao e a amostragem da refracao grampeiam na borda do
//      buffer -- que so pode ser a do canvas ou estar alem do alcance -- e a
//      escada do desfoque agrupa em caixas a partir da origem do buffer.
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/GlassTranslucency.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/PixelGrid.h"

#include <cstddef>
#include <cstdint>

namespace rb {

// Acima disto o refino nao acontece e o canvas estica a base (spec, "O teto
// de area").
inline constexpr std::size_t kViewportAreaCap = 16'000'000;

// A banda que o especular e a translucidez leem do campo, em pontos. `[OBS]`
// A spec mede ~24 para o especular; 32 e o arredondamento para cima, e o gate
// e quem diz se basta.
inline constexpr double kLocalFieldBandPoints = 32.0;

struct DocumentReach {
    double localPoints = 0.0;         // maior banda local que nao e sombra
    double shadowSigmaPoints = 0.0;   // maior sigma de sombra
    double shadowShiftPoints = 0.0;   // maior deslocamento de sombra (+ anel)
    double chainedPoints = 0.0;       // SOMA dos alcances de refracao
};

DocumentReach documentReach(const icf::IconDocument& doc, const icf::Context& ctx,
                            IconSizeClass sizeClass);

// O produto dos fatores de reducao que `blurLadder` vai usar para este sigma,
// nivel a nivel. A origem do buffer tem que ser multipla dele.
std::uint32_t blurLadderAlignment(double sigmaPixels);

struct ViewportPlan {
    PixelGrid buffer;
    PixelGrid crop;
    std::uint32_t marginPixels = 0;
    std::uint32_t alignment = 1;
    bool overCap = false;
};

Result<ViewportPlan> planViewport(const IconViewport& viewport, std::uint32_t size,
                                  const DocumentReach& reach);

}  // namespace rb
```

`IconRenderer.h` NÃO inclui `ViewportPlan.h` (isso seria um ciclo). Quem inclui é o `.cpp`.

- [ ] **Step 2: a implementação**

`Source/RenderBox/ViewportPlan.cpp`:

```c++
#include "Source/RenderBox/ViewportPlan.h"

#include "Source/RenderBox/BlurKernel.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GlassSpecular.h"

#include <algorithm>
#include <cmath>

namespace rb {

namespace {

// As duas variancias que a reducao da escada ja carrega
// (`BlurKernel.cpp:80-83`). Duplicadas aqui porque la sao internas; se uma
// mudar, `blurLadderAlignment` passa a mentir, e o gate acusa.
constexpr double kReduce4Variance = 0.47265625;
constexpr double kReduce2Variance = 0.765625;
constexpr std::uint32_t kMaxAlignment = 1u << 12;

std::int64_t alignDown(std::int64_t v, std::uint32_t a) { return v - (v % a); }

}  // namespace

DocumentReach documentReach(const icf::IconDocument& doc, const icf::Context& ctx,
                            IconSizeClass sizeClass) {
    DocumentReach r;
    const ShadowGeometry unit = shadowGeometry(static_cast<std::uint32_t>(kCanvasPoints), sizeClass);
    for (const icf::Group& group : doc.groups()) {
        const std::optional<GlassMaterialDocument> m = readGlassMaterial(group, ctx);
        if (!m) continue;
        const DenormalisedGlass g = denormaliseGlass(glassMaterialFrom(*m));
        // Conservador: `layerOpacity == 1` e todo grupo tratado como vidro. Uma
        // margem larga demais so custa area; uma curta custa o invariante.
        const ShadowInputs in{g.shadowStyle, g.shadowOpacity, 1.0, sizeClass};
        if (shadowDraws(in)) {
            r.shadowSigmaPoints = std::max(r.shadowSigmaPoints,
                                           unit.blurRadius * kShadowBlurSigmaPerRadius);
            r.shadowShiftPoints = std::max(
                r.shadowShiftPoints,
                std::hypot(unit.offsetX, unit.offsetY) + unit.ringWidth.value_or(0.0));
        }
        if (g.refractionStrengthPoints != 0.0) {
            r.chainedPoints +=
                std::max(std::fabs(g.refractionStrengthPoints), g.refractionHeightPoints);
        }
        const bool translucent = g.translucencyEnabled && g.translucency != 0.0;
        if (documentAsksForSpecular(g) || translucent) {
            r.localPoints = std::max(r.localPoints, kLocalFieldBandPoints);
        }
    }
    return r;
}

std::uint32_t blurLadderAlignment(double sigmaPixels) {
    std::uint32_t a = 1;
    double variance = sigmaPixels * sigmaPixels;
    while (variance > 0.0 && a < kMaxAlignment) {
        const int f = blurReduceFactorForVariance(variance);
        if (f <= 1) break;
        const double residual =
            variance / (f * f) - (f == 4 ? kReduce4Variance : kReduce2Variance);
        if (!(residual > 0.0)) break;
        a *= static_cast<std::uint32_t>(f);
        variance = residual;
    }
    return a;
}

Result<ViewportPlan> planViewport(const IconViewport& v, std::uint32_t size,
                                  const DocumentReach& reach) {
    ViewportPlan p;
    const std::uint32_t w = v.width ? v.width : size;
    const std::uint32_t h = v.height ? v.height : size;
    if (v.originX < 0 || v.originY < 0 ||
        static_cast<std::uint64_t>(v.originX) + w > size ||
        static_cast<std::uint64_t>(v.originY) + h > size) {
        return std::unexpected("viewport fora do canvas");
    }
    p.crop = PixelGrid{size, v.originX, v.originY, w, h};

    const double k = static_cast<double>(size) / kCanvasPoints;
    const double sigmaPx = reach.shadowSigmaPoints * k;
    p.alignment = blurLadderAlignment(sigmaPx);
    const double shadowPx =
        sigmaPx > 0.0 ? blurKernelHalfWidth(sigmaPx) + std::ceil(reach.shadowShiftPoints * k) : 0.0;
    const double localPx = std::max(std::ceil(reach.localPoints * k), shadowPx);
    const double marginPx = localPx + std::ceil(reach.chainedPoints * k) + 2.0 * p.alignment + 2.0;
    p.marginPixels = static_cast<std::uint32_t>(std::min(marginPx, static_cast<double>(size)));

    const std::int64_t m = p.marginPixels;
    const std::int64_t x0 = alignDown(std::max<std::int64_t>(0, v.originX - m), p.alignment);
    const std::int64_t y0 = alignDown(std::max<std::int64_t>(0, v.originY - m), p.alignment);
    const std::int64_t x1 = std::min<std::int64_t>(size, static_cast<std::int64_t>(v.originX) + w + m);
    const std::int64_t y1 = std::min<std::int64_t>(size, static_cast<std::int64_t>(v.originY) + h + m);
    p.buffer = PixelGrid{size, static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                         static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
    p.overCap = !p.buffer.isFull() && p.buffer.texels() > kViewportAreaCap;
    return p;
}

}  // namespace rb
```

Confira, antes de compilar:
- a assinatura real de `readGlassMaterial` (`GlassMaterial.h:191`), que recebe `icf::Context` por valor;
- a ordem dos campos de `ShadowInputs` (`GlassShadow.h:369`), que é a mesma que `IconRenderer.cpp` usa ao montar `shadowIn`;
- o tipo de `IconDocument::groups()`.

Ajuste a chamada ao que estiver lá, sem mudar a semântica.

- [ ] **Step 3: compilar**

Run: `cmake --build --preset mingw`
Expected: compila sem erro.

- [ ] **Step 4: commit**

```bash
git add Source/RenderBox/ViewportPlan.h Source/RenderBox/ViewportPlan.cpp Source/RenderBox/CMakeLists.txt
git commit -m "render de viewport, passo 2: o plano do buffer -- margem por documento com as refracoes somadas na cadeia, intersecao com o canvas, origem alinhada aos fatores da escada do desfoque e o teto de 16 Mpx"
```

---

### Task 3: a grade em todo sítio, e fundo, pastilha e arte

Implementa a spec, "Os sítios" (a extensão de todos), e os dois primeiros grupos de origem: fundo e pastilha, e colocação de arte (raster na CPU, SVG na GPU). O vidro e a sombra ainda não andam num buffer parcial, e fazem isso **dizendo**: a camada de vidro é pulada com um gap enquanto o buffer não é o canvas. Com isso a Task 3 é segura em memória e o gate julga só o que ela converteu.

**Files:**
- Modify: `Source/RenderBox/IconRenderer.h:~240-275` (`placeOnCanvas`, `artPlacementRect`, `placeRaster`/`rasterPlacementRect` se declarados)
- Modify: `Source/RenderBox/IconRenderer.cpp` (`placeRaster` 154, `rasterPlacementRect` 216, `paintBackground` 278, `artPlacementRect` 473, `placeOnCanvas` 562, `renderIcon` 587-fim)
- Modify: `Source/RenderBox/ChicletShape.h:115-121`, `ChicletShape.cpp:170-230`
- Modify: `Source/RenderBox/ChicletHighlights.h:284`, `ChicletHighlights.cpp:208-290`
- Modify: `Source/RenderBox/DistanceField.h` (`struct FieldOptions`, `struct FieldImage`; as linhas andaram com o comentário de `e78eca7`), `DistanceField.cpp` (`rasteriseContours`, `addSpan`, `coverageFromContours`, `generateFieldFromContours`, `generateFieldFromAlpha`) (só o necessário para a pastilha: origem em `FieldOptions`, ver Step 4)
- Modify: `Tests/test_viewport_render.cpp`

**Interfaces:**
- Consumes: `planViewport`, `documentReach` (Task 2).
- Produces:
  - `PathGlobals placeOnCanvas(const icf::svg::ViewBox&, const LayerPlacement&, const PixelGrid&)`, com `m2` já menos a origem e `twoOverSize`/`urx` sobre a extensão
  - `PlacementRect artPlacementRect(const icf::svg::ViewBox&, const LayerPlacement&, const PixelGrid&)`: retângulo em coordenada do BUFFER
  - `std::vector<float> placeRaster(const icf::DecodedPng&, const LayerPlacement&, const PixelGrid&)`: amostragem absoluta
  - `PlacementRect rasterPlacementRect(uint32, uint32, const LayerPlacement&, std::uint32_t size)`: continua ABSOLUTO (recebe o escalar)
  - `void paintBackground(std::vector<float>&, const PixelGrid&, const FillOverride&)`
  - `std::vector<float> chicletCoverage(const PixelGrid&)`, `void clipToChiclet(std::vector<float>&, const PixelGrid&)`
  - `std::size_t drawChicletHighlights(std::vector<float>&, const PixelGrid&, const SpecularArguments&)`
  - `FieldOptions::originX/originY` e `FieldImage::originX/originY` (`int32`, em pixels da grade, antes do supersample)

- [ ] **Step 1: `renderIcon` passa a usar o plano**

No começo de `renderIcon`, troque a validação da Task 1 pelo plano (a leitura do documento sobe para cá; remova a declaração repetida mais abaixo):

```c++
    const icf::IconDocument doc = bundle.document();
    const std::vector<icf::Group> groups = doc.groups();

    auto planned = planViewport(options.viewport, options.size,
                                documentReach(doc, options.context, options.sizeClass));
    if (!planned) return std::unexpected(planned.error());
    const PixelGrid grid = planned->buffer;
    const PixelGrid canvasGrid = PixelGrid::full(options.size);

    RenderedIcon out;
    out.size = options.size;
    out.originX = planned->crop.originX;
    out.originY = planned->crop.originY;
    out.buffer = grid;
    if (planned->overCap) {
        out.viewportRefused = true;
        note(out.notes, "viewport acima do teto de area: nada desenhado (spec 2026-09-16, "
                        "\"O teto de area\")");
        return out;
    }
    const std::size_t texels = grid.texels();
```

Inclua `"Source/RenderBox/ViewportPlan.h"`. No recorte final, apague o comentário TEMPORARIO e use a origem de verdade:

```c++
    const std::int32_t cropX = planned->crop.originX - grid.originX;
    const std::int32_t cropY = planned->crop.originY - grid.originY;
    const std::uint32_t viewW = planned->crop.width;
    const std::uint32_t viewH = planned->crop.height;
```

(o resto do laço da Task 1 fica como está: ele já indexa por `out.buffer.width`).

- [ ] **Step 2: o compilador enumera os sítios**

Mude as assinaturas listadas em **Produces** e recompile. Cada erro é um sítio. Trate cada um assim:

| linha (HEAD `f9f57ea`) | vira |
|---|---|
| 626-627 `canvas{0,0,size,size}` | **fica** (o fundo é absoluto) |
| 633 `paintBackground(acc, options.size, paint)` | `paintBackground(acc, grid, paint)` |
| 639 `clipToChiclet(acc, options.size)` | `clipToChiclet(acc, grid)` |
| 669 `drawChicletHighlights(acc, options.size, ...)` | `drawChicletHighlights(acc, grid, ...)` |
| 745 `glassRefractionFor(glassNumbers, options.size)` | **fica** (escala) |
| 791 `pixelsPerPoint` | **fica** (escala) |
| 828-829 `blurMaterialSurface(..., options.size, options.size)` | **fica**, com o comentário abaixo |
| 862-863 `groupAcc.assign(texels * 4, ...)` | já usa `texels` da grade |
| 1160 `placeRaster(png, lp, options.size)` | `placeRaster(png, lp, grid)` |
| 1170 `artPlacementRect(svg->viewBox, lp, options.size)` (paint) | `artPlacementRect(svg->viewBox, lp, grid)`: o gradiente é avaliado pela GPU em coordenada do buffer |
| 1273 `placeOnCanvas(...)` para contornos | `placeOnCanvas(svg->viewBox, lp, canvasGrid)`: contornos ABSOLUTOS |
| 1301 `generateFieldFromContours(..., options.size, options.size, ...)` | Task 4 |
| 1317 `generateFieldFromAlpha(*rasterPlaced, options.size, options.size)` | Task 4 |
| 1351 `glassOver(target, options.size, options.size, ...)` | Task 4 |
| 1372 `artPlacementRect(...)` (bounds da translucidez) | `artPlacementRect(svg->viewBox, lp, canvasGrid)`: ABSOLUTO |
| 1373 `rasterPlacementRect(..., options.size)` | **fica** (absoluto) |
| 1428 `shadowGeometry(options.size, ...)` | **fica** (escala) |
| 1429 `shadowImage(artRgba, options.size, options.size, ...)` | `shadowImage(artRgba, grid.width, grid.height, ...)` |
| 1437-1438 `shadowOverdrawImage(..., options.size, options.size, ...)` | `grid.width, grid.height` |
| 1464 `ro.width = ro.height = options.size` | `ro.width = grid.width; ro.height = grid.height;` |
| 1468 `placeOnCanvas(svg->viewBox, lp, options.size)` | `placeOnCanvas(svg->viewBox, lp, grid)` |

Por enquanto, em 1301, 1317 e 1351, passe `grid.width, grid.height`: a Task 4 corrige a origem. Por isso o vidro fica bloqueado no Step 6.

Acima de 828, o comentário:

```c++
        // A ESCALA DESTA CHAMADA SAI DA EXTENSAO: `blurMaterialSurface` faz
        // `min(w, h) / 1024` (`BlurKernel.cpp:279`). Por isso ela recebe o
        // CANVAS e nao o buffer -- um buffer de viewport aqui mudaria a escala
        // sem erro nenhum (spec 2026-09-16, "Os sitios"). O desenho esta
        // desligado; a superficie so vai para o relatorio.
```

- [ ] **Step 3: fundo, `placeOnCanvas`, `artPlacementRect`, `placeRaster`**

`paintBackground`, com o laço sobre o buffer e a avaliação absoluta:

```c++
void paintBackground(std::vector<float>& acc, const PixelGrid& grid,
                     const FillOverride& paint) {
    for (std::uint32_t y = 0; y < grid.height; ++y) {
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            float colour[4] = {paint.colour[0], paint.colour[1], paint.colour[2],
                               paint.colour[3]};
            if (paint.kind == FillOverride::Kind::Ramp) {
                // ABSOLUTO: o mesmo ponto que o render cheio avalia, e a
                // soma inteira antes do `+ 0.5` e exata.
                const double px = static_cast<double>(static_cast<std::int64_t>(x) + grid.originX) + 0.5;
                const double py = static_cast<double>(static_cast<std::int64_t>(y) + grid.originY) + 0.5;
                const double t = paint.m[0] * px + paint.m[1] * py + paint.m[2];
                if (paint.smooth) {
                    rampSmoothAtPositions(paint.stops, static_cast<float>(t), colour);
                } else {
                    rampAtPositions(paint.stops, static_cast<float>(t), colour);
                }
            }
            const std::size_t i = (static_cast<std::size_t>(y) * grid.width + x) * 4;
            for (int k = 0; k < 3; ++k) acc[i + k] = colour[k] * colour[3];
            acc[i + 3] = colour[3];
        }
    }
}
```

`placeOnCanvas`: a escala sai de `grid.size`, e a origem é subtraída **depois** do cast (float menos inteiro, que é a forma com menos arredondamento extra):

```c++
PathGlobals placeOnCanvas(const icf::svg::ViewBox& box, const LayerPlacement& p,
                          const PixelGrid& grid) {
    PathGlobals g;
    const double k = static_cast<double>(grid.size) / kCanvasPoints;
    ... (o corpo de hoje, sem mudar, ate m2) ...
    // O UNICO SITIO QUE TRANSLADA GEOMETRIA (spec 2026-09-16, "O invariante"):
    // a GPU desenha no buffer, e o buffer comeca na origem. A subtracao e feita
    // em float, depois do cast, para que a origem zero deixe o valor de hoje
    // intocado.
    g.m2[0] = static_cast<float>(left * k - s * box.x) - static_cast<float>(grid.originX);
    g.m2[1] = static_cast<float>(top * k - s * box.y) - static_cast<float>(grid.originY);
    g.twoOverSize[0] = 2.0f / static_cast<float>(grid.width);
    g.twoOverSize[1] = 2.0f / static_cast<float>(grid.height);
    g.urx = static_cast<float>(grid.width);
    return g;
}
```

`artPlacementRect` só troca o tipo do parâmetro e repassa a `grid` para `placeOnCanvas`. O retângulo sai em coordenada do buffer da grade recebida.

`placeRaster`: a saída passa a ter `grid.texels()`, o laço vai sobre `grid.height` × `grid.width`, e os centros ficam absolutos:

```c++
    const double k = static_cast<double>(grid.size) / kCanvasPoints;
    ...
    for (std::uint32_t y = 0; y < grid.height; ++y) {
        const double gy = static_cast<double>(static_cast<std::int64_t>(y) + grid.originY);
        const double cy = ((gy + 0.5) / k - top) / p.scale - 0.5;
        ...
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            const double gx = static_cast<double>(static_cast<std::int64_t>(x) + grid.originX);
            const double cx = ((gx + 0.5) / k - left) / p.scale - 0.5;
            ...
            const std::size_t d = (static_cast<std::size_t>(y) * grid.width + x) * 4;
```

- [ ] **Step 4: a pastilha**

`DistanceField.h`, em `FieldOptions`, depois de `aaWidth`:

```c++
    // A origem da grade amostrada, em pixels (antes do supersample). A
    // amostragem e ABSOLUTA -- o centro da linha `y` e `y + originY + 0.5` -- e
    // so o indice no resultado e deslocado (spec 2026-09-16).
    std::int32_t originX = 0;
    std::int32_t originY = 0;
```

Em `FieldImage`, depois de `height`: `std::int32_t originX = 0, originY = 0;`

`DistanceField.cpp`: `rasteriseContours` e `coverageFromContours` ganham `int ox, int oy`, **já multiplicados pelo supersample**. O chamador (`generateFieldFromContours`) passa `options.originX * ss` e `options.originY * ss`. Dentro de `rasteriseContours`:

```c++
            int y0 = static_cast<int>(std::ceil(lo - 0.5)) - oy;
            int y1 = static_cast<int>(std::ceil(hi - 0.5)) - oy;   // exclusive
            if (y0 < 0) y0 = 0;
            if (y1 > h) y1 = h;
            const double invDy = 1.0 / (by - ay);
            for (int y = y0; y < y1; ++y) {
                const double cy = static_cast<double>(y + oy) + 0.5;
```

e, no preenchimento, `const double px = static_cast<double>(x + ox) + 0.5;`. O `while` já acumula os cruzamentos à esquerda do buffer, porque começa em `k = 0`.

Dentro de `coverageFromContours`:

```c++
            int k0 = static_cast<int>(std::ceil(lo * n - 0.5)) - oy * n;
            int k1 = static_cast<int>(std::ceil(hi * n - 0.5)) - oy * n;
            ...
                const double cy = (static_cast<double>(k + oy * n) + 0.5) / n;
```

`addSpan` ganha `int ox`: grampeia em `[ox, ox + w)` e indexa `x - ox`. A chamada final `addSpan(acc, w, start, static_cast<double>(w), wt)` vira `addSpan(acc, ox, w, start, static_cast<double>(ox + w), wt)`.

Em `generateFieldFromContours` e `generateFieldFromAlpha`, depois de montar a imagem: `img.originX = options.originX; img.originY = options.originY;` (no caminho de alpha, o raster já chega em coordenada do buffer, então só a origem é copiada).

`ChicletShape.cpp`, em `chicletCoverage(const PixelGrid& g)`: o contorno continua sendo `continuousRoundedRect(0, 0, g.size, g.size, r, r)`, com `r = chicletCornerRadius(g.size)`, ou seja, ABSOLUTO. A saída tem `g.width * g.height` posições. O laço de linhas fica assim:

```c++
    for (std::uint32_t ly = 0; ly < g.height; ++ly) {
        const std::int64_t py = static_cast<std::int64_t>(ly) + g.originY;
        float* row = cov.data() + static_cast<std::size_t>(ly) * g.width;
        for (int s = 0; s < kSubRows; ++s) {
            const double sy = static_cast<double>(py) + (s + 0.5) * w;
            ... (cruzamentos iguais) ...
            const double x0 = static_cast<double>(g.originX);
            const double x1 = x0 + g.width;
            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                const double lo = std::max(xs[k], std::max(0.0, x0));
                const double hi = std::min(xs[k + 1], std::min(static_cast<double>(g.size), x1));
                if (hi <= lo) continue;
                const auto first = static_cast<std::int64_t>(lo);
                const auto last = static_cast<std::int64_t>(std::ceil(hi) - 1.0);
                for (std::int64_t px = first; px <= last; ++px) {
                    const double overlap =
                        std::min(hi, px + 1.0) - std::max(lo, static_cast<double>(px));
                    if (overlap > 0.0) row[px - g.originX] += static_cast<float>(overlap * w);
                }
            }
        }
    }
```

`lo` e `hi` precisam ser os MESMOS números do render cheio dentro do buffer. O grampo em `x0` só corta o span, e o `overlap` de um pixel dentro do buffer não muda. `clipToChiclet(acc, g)` usa `g.texels()`. Mantenha uma sobrecarga `chicletCoverage(std::uint32_t size)` que chama a nova com `PixelGrid::full(size)`, se algum teste a usar (`Tests/test_chiclet_shape.cpp`).

`ChicletHighlights.cpp`, em `drawChicletHighlights(rgba, const PixelGrid& g, args)`: `n = g.texels()`, `chicletContours(g.size)` (absoluto), e:

```c++
    FieldOptions fo;
    fo.originX = g.originX;
    fo.originY = g.originY;
    const FieldImage field = generateFieldFromContours(contours, g.width, g.height, fo);
```

Os laços vão sobre `g.height` × `g.width`, e o índice `(y * g.width + x)` substitui `(y * size + x)`. Confira a chamada existente de `generateFieldFromContours` ali, que pode passar opções: preserve as que já existem e só acrescente a origem. Mantenha uma sobrecarga com `std::uint32_t size` se `Tests/test_chiclet_highlights.cpp` a usar.

- [ ] **Step 5: o vidro e a sombra, bloqueados com aviso**

Em `renderIcon`, logo depois de `const bool isGlass = ...`:

```c++
            // O vidro e a sombra ainda nao andam num buffer parcial (o campo, a
            // refracao e a mascara amostram relativo). Dito, e nao desenhado
            // errado. Sai quando o campo e a sombra forem convertidos.
            if (isGlass && !grid.isFull()) {
                skip("vidro em viewport: ainda nao convertido");
                continue;
            }
```

- [ ] **Step 6: fixtures do gate para fundo e arte**

Em `Tests/test_viewport_render.cpp`, acrescente um `TempBundle` mínimo (no namespace anônimo), com uma arte vetor, uma raster e um fundo em gradiente, **sem vidro**:

```c++
class TempBundle {
public:
    explicit TempBundle(const std::string& name, const std::string& document) {
        dir_ = fs::temp_directory_path() / ("ic-viewport-" + name);
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);
        // Um circulo com gradiente proprio: borda curva para o antialias, e uma
        // rampa que mede o retangulo.
        write(dir_ / "Assets" / "disc.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<circle cx=\"256\" cy=\"256\" r=\"200\" fill=\"#3080ff\"/></svg>");
        // Um raster com borda dura e um degrade em x, que denuncia amostragem
        // deslocada meio texel.
        const std::uint32_t side = 48;
        std::vector<float> px(static_cast<std::size_t>(side) * side * 4, 0.0f);
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * side + x) * 4;
                const bool in = (x - 24.0) * (x - 24.0) + (y - 24.0) * (y - 24.0) < 400.0;
                px[i + 0] = static_cast<float>(x) / side;
                px[i + 1] = 0.5f;
                px[i + 2] = static_cast<float>(y) / side;
                px[i + 3] = in ? 1.0f : 0.0f;
            }
        }
        const std::vector<std::uint8_t> png = icf::encodePng(px, side, side);
        std::FILE* f = std::fopen((dir_ / "Assets" / "dot.png").string().c_str(), "wb");
        if (f) {
            std::fwrite(png.data(), 1, png.size(), f);
            std::fclose(f);
        }
    }
    ~TempBundle() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

private:
    static void write(const fs::path& p, const std::string& text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    fs::path dir_;
};

// Fundo em gradiente + um disco vetor + um raster ampliado, os dois sem vidro.
const char* const kPlainDocument = R"({
  "fill" : { "linear-gradient" : [ "display-p3:0.9,0.2,0.3,1", "display-p3:0.1,0.3,0.9,1" ] },
  "groups" : [
    { "layers" : [
      { "glass" : false, "image-name" : "disc.svg", "name" : "disc" },
      { "glass" : false, "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 6, "translation-in-points" : [ 120, -90 ] } }
    ] }
  ]
})";
```

Antes de usar, confira com `grep -rh '"linear-gradient"' References/corpus/*/icon.json | head` o formato real de `linear-gradient` e de uma cor no corpus, e copie um valor dali. O JSON acima é o formato esperado, mas o leitor é quem manda.

O caso:

```c++
TEST_CASE(viewport_background_chiclet_and_art_match_the_full_render) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("plain", kPlainDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    RenderedIcon full;
    CHECK_EQ(compareViewports(*bundle, 512, gateViewports(512), &full), 0);
    CHECK(full.backgroundPainted);
    CHECK_EQ(full.drawn, 2u);
}
```

- [ ] **Step 7: construir e rodar o gate**

Run: `cmake --build --preset mingw --target ic_tests && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe viewport`
Expected: zero `FAIL`.

Se `viewport_background_chiclet_and_art_match_the_full_render` falhar, leia a linha do relatório:
- `max |d|` na ordem de 1e-7 a 1e-5, espalhado: é o desenho SVG na GPU (o único sítio que translada). Confirme trocando a arte vetor por `glass:false` num documento só com o raster. Se o raster sozinho passar, o resíduo é da GPU, e o próximo passo é o `m2`: teste `static_cast<float>(left * k - s * box.x - grid.originX)` contra a forma atual e fique com a que zera. **Não afrouxe a tolerância.**
- `max |d|` grande: um sítio da tabela do Step 2 ficou com a coordenada errada. A posição do pior pixel diz qual camada é.

- [ ] **Step 8: commit**

```bash
git add Source/RenderBox Tests/test_viewport_render.cpp
git commit -m "render de viewport, passo 3: renderIcon roda no buffer planejado, a grade chega a todo sitio que usa extensao, e fundo, pastilha, raster e SVG passam a amostrar em coordenada absoluta -- o gate fecha em zero para um documento sem vidro, e o vidro fica pulado com aviso num buffer parcial ate o campo ser convertido"
```

---

### Task 4: campo, translucidez, especular e refração

Implementa a spec, grupo "campo e vidro". Remove o bloqueio da Task 3 para o vidro, **mas mantém a sombra bloqueada**.

**Files:**
- Modify: `Source/RenderBox/IconRenderer.cpp` (1301, 1317, 1351, e o bloqueio da Task 3)
- Modify: `Source/RenderBox/GlassTranslucency.cpp:96-116`
- Modify: `Source/RenderBox/DisplacementOracle.h:130-134`, `DisplacementOracle.cpp:152-175`
- Modify: `Source/RenderBox/GlassLayer.h:250`, `GlassLayer.cpp:206-265`
- Modify: `Tests/test_viewport_render.cpp`

**Interfaces:**
- Consumes: `FieldOptions::originX/Y`, `FieldImage::originX/Y` (Task 3), `PixelGrid`.
- Produces:
  - `SampledImage { int width, height; const float* rgba; int originX = 0, originY = 0; int bufferWidth = 0, bufferHeight = 0; }`: `width`/`height` são a grade UV (o canvas), e `bufferWidth == 0` significa "o buffer é a grade"
  - `void glassOver(std::vector<float>& acc, const PixelGrid& grid, const DisplacementImage& map, const GlassRefraction& r)`

- [ ] **Step 1: o campo com origem**

Em `IconRenderer.cpp`, 1301:

```c++
                        FieldOptions fo;
                        fo.rule = shape.rule;
                        fo.originX = grid.originX;
                        fo.originY = grid.originY;
                        FieldImage fromShape = generateFieldFromContours(
                            shape.contours, grid.width, grid.height, fo, kFieldSuperSample);
```

Em 1317:

```c++
                    FieldOptions fo;
                    fo.originX = grid.originX;
                    fo.originY = grid.originY;
                    FieldImage fromAlpha =
                        generateFieldFromAlpha(*rasterPlaced, grid.width, grid.height, fo);
```

Confira a assinatura de `generateFieldFromAlpha` (`DistanceField.h:398`). Se ela já tiver `FieldOptions` com padrão, basta passar `fo`.

- [ ] **Step 2: a translucidez mede `py` absoluto**

`GlassTranslucency.cpp:103`:

```c++
        // ABSOLUTO: `bounds` chega na grade de `size`, nao na do buffer (spec
        // 2026-09-16).
        const float py =
            static_cast<float>(static_cast<std::int64_t>(y) + field.originY) + 0.5f;
```

- [ ] **Step 3: `SampledImage` com origem**

`DisplacementOracle.h:130`:

```c++
struct SampledImage {
    int width = 0;               // a grade das UV (o canvas inteiro)
    int height = 0;
    const float* rgba = nullptr; // bufferWidth * bufferHeight * 4, row major
    // O buffer cobre [originX, originX + bufferWidth) da grade. Zero = a grade
    // inteira, que e o caso de todo chamador que nao e um viewport. O grampo
    // continua sendo o da GRADE: e o que faz a borda do canvas se comportar
    // igual nos dois renders (spec 2026-09-16, "A margem").
    int originX = 0, originY = 0;
    int bufferWidth = 0, bufferHeight = 0;
};
```

`sampleBilinear`, depois do cálculo de `x0, x1, y0, y1` (grampeados na grade):

```c++
    const int bw = image.bufferWidth ? image.bufferWidth : image.width;
    const int bh = image.bufferHeight ? image.bufferHeight : image.height;
    auto at = [&](int x, int y, int k) {
        // Uma leitura fora do buffer e margem curta. Grampear aqui esconderia
        // isso; o gate acusa a diferenca e o relatorio aponta a borda.
        const int lx = clampi(x - image.originX, 0, bw - 1);
        const int ly = clampi(y - image.originY, 0, bh - 1);
        return image.rgba[(ly * bw + lx) * 4 + k];
    };
    for (int k = 0; k < 4; ++k) {
        const float c00 = at(x0, y0, k);
        const float c10 = at(x1, y0, k);
        const float c01 = at(x0, y1, k);
        const float c11 = at(x1, y1, k);
```

O grampo em `lx` evita ler fora da memória. Se ele disparar, é porque a margem é curta, e o gate mostra isso como diferença colada na borda interna. O resto do corpo fica igual.

- [ ] **Step 4: `glassOver` absoluto**

`GlassLayer.h:250` e `GlassLayer.cpp:206`:

```c++
void glassOver(std::vector<float>& acc, const PixelGrid& grid, const DisplacementImage& map,
               const GlassRefraction& r) {
    if (map.width != grid.width || map.height != grid.height) return;
    if (acc.size() < grid.texels() * 4) return;
    const std::vector<float> backdrop = acc;

    DisplacementParams p;
    p.scale = r.scalePixels;
    // UV SOBRE O CANVAS, nao sobre o buffer: `x * (1/size)` e o mesmo numero
    // que o render cheio calcula (spec 2026-09-16).
    const float ix = 1.0f / static_cast<float>(grid.size);
    const float iy = 1.0f / static_cast<float>(grid.size);
    ... (a mesma montagem das duas camadas) ...

    SampledImage source;
    source.width = static_cast<int>(grid.size);
    source.height = static_cast<int>(grid.size);
    source.rgba = backdrop.data();
    source.originX = grid.originX;
    source.originY = grid.originY;
    source.bufferWidth = static_cast<int>(grid.width);
    source.bufferHeight = static_cast<int>(grid.height);
    SampledImage mapped = source;
    mapped.rgba = map.rgba.data();

    ...
    for (std::uint32_t y = 0; y < grid.height; ++y) {
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * grid.width + x) * 4;
            const float mask = map.rgba[i + 3];
            if (mask <= 0.0f) continue;
            float refracted[4];
            const float px = static_cast<float>(static_cast<std::int64_t>(x) + grid.originX) + 0.5f;
            const float py = static_cast<float>(static_cast<std::int64_t>(y) + grid.originY) + 0.5f;
            displacementMap(p, r.variant, px, py, dpdx, dpdy, source, mapped, refracted);
            ...
```

Inclua `"Source/RenderBox/PixelGrid.h"` em `GlassLayer.h`. Os chamadores em teste (`Tests/test_glass_layer.cpp`, se houver) passam `PixelGrid::full(side)`. Mantenha uma sobrecarga `glassOver(acc, w, h, map, r)` que chama a nova com `PixelGrid{w, 0, 0, w, h}` apenas se `w == h`. Se algum teste usar `w != h`, ajuste o teste.

Em `IconRenderer.cpp:1351`: `glassOver(target, grid, glassDisplacementMap(*field, refraction), refraction);`

- [ ] **Step 5: o bloqueio só para a sombra**

Troque o bloqueio da Task 3 por:

```c++
            // A sombra ainda nao anda num buffer parcial: a escada do desfoque
            // precisa da origem alinhada, e isso so e conferido quando ela
            // entra. Dito, e nao desenhado errado.
            const bool shadowBlocked = !grid.isFull();
```

e, em `castsShadow`, `const bool castsShadow = isGlass && !shadowBlocked && shadowDraws(shadowIn);`. Quando `isGlass && shadowBlocked && shadowDraws(shadowIn)`, chame `note(out.notes, "sombra em viewport: ainda nao convertida")`.

- [ ] **Step 6: fixture de vidro sem sombra**

```c++
// Vidro com translucidez, especular e refracao, e sombra "none": o campo e a
// refracao sao julgados sem a escada do desfoque no caminho.
const char* const kGlassDocument = R"({
  "fill" : { "linear-gradient" : [ "display-p3:0.9,0.2,0.3,1", "display-p3:0.1,0.3,0.9,1" ] },
  "groups" : [
    { "layers" : [ { "image-name" : "disc.svg", "name" : "disc" } ],
      "refractivity" : { "depth" : 0.5, "enabled" : true, "strength" : -0.53 },
      "shadow" : { "kind" : "none", "opacity" : 0.5 },
      "specular" : true,
      "translucency" : { "enabled" : true, "value" : 0.5 } },
    { "layers" : [ { "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 6, "translation-in-points" : [ 120, -90 ] } } ],
      "shadow" : { "kind" : "none", "opacity" : 0.5 },
      "specular" : true,
      "translucency" : { "enabled" : true, "value" : 0.5 } }
  ]
})";

TEST_CASE(viewport_field_translucency_specular_and_refraction_match) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("glass", kGlassDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    RenderedIcon full;
    CHECK_EQ(compareViewports(*bundle, 512, gateViewports(512), &full), 0);
    CHECK(full.glassRefracted > 0);
    CHECK(full.glassTranslucent > 0);
    CHECK(full.glassSpecular > 0);
    CHECK_EQ(full.skipped.size(), 0u);
}
```

Se `full.skipped` não vier vazio, o documento não lê. A razão está em `skipped[i].why`: imprima e ajuste o JSON (as chaves e os formatos saem de `GlassMaterial.cpp:40-100`).

- [ ] **Step 7: construir e rodar o gate**

Run: `cmake --build --preset mingw --target ic_tests && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe viewport`
Expected: zero `FAIL`.

Leitura do relatório, se falhar:
- diferença colada na borda interna, só em viewports com refração: margem curta. Confira `chainedPoints` em `documentReach`.
- diferença longe da borda, dentro da forma: a hipótese da banda do campo caiu (a `[OBS]` da spec). Pare e registre na spec antes de mexer.
- diferença de ULP: GPU, como na Task 3.

- [ ] **Step 8: commit**

```bash
git add Source/RenderBox Tests/test_viewport_render.cpp
git commit -m "render de viewport, passo 4: o campo amostra linhas absolutas com a origem em FieldOptions, a translucidez mede py absoluto e a refracao le o backdrop com UV sobre o canvas e indice deslocado -- o gate fecha em zero com vidro, translucidez, especular e refracao, e so a sombra segue bloqueada num buffer parcial"
```

---

### Task 5: a sombra

Implementa a spec, grupo "sombra", e remove o último bloqueio. A exatidão da sombra depende de o plano alinhar a origem (Task 2). Nada muda dentro de `GlassShadow.cpp`.

**Files:**
- Modify: `Source/RenderBox/IconRenderer.cpp` (o bloqueio da Task 4)
- Modify: `Tests/test_viewport_render.cpp`

- [ ] **Step 1: tirar o bloqueio**

Apague `shadowBlocked`, a nota e a condição em `castsShadow`, que volta a ser `isGlass && shadowDraws(shadowIn)`.

- [ ] **Step 2: fixture com sombra, numa `size` em que a escada reduz**

A escada reduz quando `ceil(σ² / 5,25² - 0,001) >= 3`, ou seja, `σ ≥ ~9,1 px`. Em `size = 512`, o `σ` da sombra é `0,5 × 64 × radius`. Com `radius = 1` (o default grande), dá 32 px: reduz por 4 e depois por 2. Por isso a fixture usa sombra `neutral` e `layer-color`.

```c++
const char* const kShadowDocument = R"({
  "groups" : [
    { "layers" : [ { "image-name" : "disc.svg", "name" : "disc",
        "position" : { "scale" : 0.6, "translation-in-points" : [ -140, -160 ] } } ],
      "shadow" : { "kind" : "neutral", "opacity" : 0.8 } },
    { "layers" : [ { "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 5, "translation-in-points" : [ 150, 120 ] } } ],
      "refractivity" : { "depth" : 0.4, "enabled" : true, "strength" : -0.36 },
      "shadow" : { "kind" : "layer-color", "opacity" : 0.6 } }
  ]
})";

TEST_CASE(viewport_shadow_matches_including_the_blur_ladder) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("shadow", kShadowDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    RenderedIcon full;
    CHECK_EQ(compareViewports(*bundle, 512, gateViewports(512), &full), 0);
    CHECK(full.glassShadowed >= 2);
    CHECK(full.glassRefracted > 0);   // refracao sobre a sombra do grupo de tras: a cadeia
    CHECK(blurLadderAlignment(32.0) > 1);   // a fixture exercita a escada
}
```

Inclua `"Source/RenderBox/ViewportPlan.h"` no teste.

- [ ] **Step 3: provar que o alinhamento é necessário**

Em `planViewport`, troque **temporariamente** `alignDown(..., p.alignment)` por `alignDown(..., 1)` nas duas linhas, e rode o gate.
Expected: `viewport_shadow_matches_including_the_blur_ladder` falha no viewport de origem não alinhada, com diferença **longe da borda**. Esse é o sintoma da escada; se não falhar, a fixture não exercita a escada e precisa de um `σ` maior. Depois **desfaça a troca**.

- [ ] **Step 4: rodar o gate**

Run: `cmake --build --preset mingw --target ic_tests && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe viewport`
Expected: zero `FAIL`.

- [ ] **Step 5: commit**

```bash
git add Source/RenderBox/IconRenderer.cpp Tests/test_viewport_render.cpp
git commit -m "render de viewport, passo 5: a sombra anda num buffer parcial -- o gate fecha em zero com sombra neutral e layer-color numa size em que a escada reduz por 4 e por 2, e foi visto falhar longe da borda quando o alinhamento da origem e desligado"
```

---

### Task 6: o corpus, e a margem medida

Implementa a spec, "A margem ... o número final entra aqui como medida".

**Files:**
- Modify: `Tests/test_viewport_render.cpp`
- Modify: `Docs/Specs/2026-09-16-viewport-render.md`

- [ ] **Step 1: o caso do corpus**

```c++
// Os dois documentos da spec: `swmpc` (refracao 337 pontos, raster,
// translucidez, sombra, especular, fundo) e `Jellify-Music` (vetor, o mesmo
// sem refracao). Em 2048 -- 400% sobre 512, o caso que motivou a spec.
TEST_CASE(viewport_corpus_documents_match_at_2048) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    for (const char* name : {"CamilleScholtz__swmpc__swmpc",
                             "Jellify-Music__App__teal-icon-composer"}) {
        auto bundle = icf::IconBundle::open(fs::path(dir) / name);
        REQUIRE(bundle.has_value());
        RenderedIcon full;
        const int bad = compareViewports(*bundle, 2048, gateViewports(2048), &full);
        if (bad) std::printf("  %s: %d viewports diferentes\n", name, bad);
        CHECK_EQ(bad, 0);
        CHECK(full.glassShadowed > 0);
        CHECK(full.backgroundPainted);
        if (std::string(name).find("swmpc") != std::string::npos) {
            CHECK(full.glassRefracted > 0);   // o documento que refrata tem que refratar
        }
        const auto reach = documentReach(bundle->document(), IconRenderOptions{}.context,
                                         IconSizeClass::Large);
        auto plan = planViewport(gateViewports(2048)[0], 2048, reach);
        REQUIRE(plan.has_value());
        std::printf("  %s: margem %u px em 2048 (alinhamento %u), buffer %ux%u\n", name,
                    plan->marginPixels, plan->alignment, plan->buffer.width, plan->buffer.height);
    }
}
```

- [ ] **Step 2: rodar o gate uma vez**

Run: `IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe viewport_corpus`
Expected: zero `FAIL`, e as duas linhas de margem impressas. É **uma** execução, e ela pode levar dezenas de segundos (render cheio de 2048 duas vezes). Não repita.

Se falhar, aplique a leitura do relatório das Tasks 3 e 4.

- [ ] **Step 3: medir a margem mínima**

A margem da Task 2 é uma conta. A spec pede o número medido: o menor que zera o diferencial. Faça uma medição, não um laço de execuções. Acrescente **temporariamente** em `planViewport` um fator de ambiente:

```c++
    if (const char* f = std::getenv("IC_MARGIN_SCALE")) marginPx *= std::atof(f);
```

(`marginPx` passa de `const` para `double` mutável.) Rode uma vez com `IC_MARGIN_SCALE=0.5` e anote o que o relatório diz: se falha, e a que distância da borda. Depois **remova o fator**. O objetivo não é afinar a margem: é registrar que ela não está folgada por acaso, ou que está.

- [ ] **Step 4: escrever o número na spec**

Em `Docs/Specs/2026-09-16-viewport-render.md`, logo depois do parágrafo "Os alcances se SOMAM", acrescente:

```markdown
`[ART]` **Medido em <data>.** Em `size = 2048`, a margem calculada é <N> px para
`Jellify-Music` e <M> px para `swmpc`, e o gate fecha em zero com ela. Com a
metade, <o que o relatório disse>. <Uma frase: a conta está justa, ou está
folgada e por quanto.>
```

Preencha os `<>` com o que as duas execuções imprimiram. Não deixe nenhum `<>` no arquivo.

- [ ] **Step 5: commit**

```bash
git add Tests/test_viewport_render.cpp Docs/Specs/2026-09-16-viewport-render.md
git commit -m "render de viewport, passo 6: o gate fecha em zero sobre swmpc e Jellify-Music em 2048, e a margem medida entra na spec"
```

---

### Task 7: o job e o teto

Implementa a spec, "O teto de área", do lado de quem pede.

**Files:**
- Create: `Source/IconComposerKit/Tile.h`
- Modify: `Source/IconComposerKit/Ports.h:13-29`
- Modify: `Source/app/OnyxPorts.cpp:22-56`

**Interfaces:**
- Produces:
  - `struct ick::TileRect { int32 x, y; uint32 w, h; operator== }`
  - `RenderRequest::tile`, `RenderRequest::fallbackSize`
  - `RenderResult::size` (eco do pedido), `RenderResult::tile` (eco), `RenderResult::gridSize`, `RenderResult::originX/originY`, `RenderResult::refined`

- [ ] **Step 1: `Tile.h`**

```c++
#pragma once
// O pedaco da grade de `size` que o canvas quer ver (spec 2026-09-16, "O que o
// Kit faz"). `w == 0` e o canvas inteiro -- o pedido de sempre.
//
// Um header proprio porque `Session.h` (o modelo, sem ImGui) e `Ports.h` (a
// fronteira com a janela, com ImGui) precisam do mesmo tipo.
#include <cstdint>

namespace ick {

struct TileRect {
    std::int32_t x = 0, y = 0;
    std::uint32_t w = 0, h = 0;
    bool operator==(const TileRect&) const = default;
};

}  // namespace ick
```

- [ ] **Step 2: `Ports.h`**

Inclua `Tile.h`. Os campos novos entram **no fim** de `RenderRequest`, porque ele é um agregado inicializado em ordem (`RenderCoordinator.cpp:17`):

```c++
    // O ladrilho, na grade de `size`. `w == 0`: o canvas inteiro.
    TileRect tile;
    // Se o buffer do ladrilho passar do teto, o job renderiza o canvas inteiro
    // nesta resolucao. Zero: nao ha para onde cair.
    std::uint32_t fallbackSize = 0;
```

Em `RenderResult`, depois de `context`:

```c++
    // O ECO do pedido, que e o que o coordenador compara: `size` e `tile`
    // como foram PEDIDOS, mesmo quando o job caiu para a base.
    std::uint32_t size = 0;
    TileRect tile;
    // O que os pixels SAO: `width` x `height` a partir de (`originX`,
    // `originY`) numa grade de `gridSize`.
    std::uint32_t gridSize = 0;
    std::int32_t originX = 0, originY = 0;
    bool refined = true;   // false: caiu para a base por causa do teto
```

- [ ] **Step 3: o job**

`OnyxPorts.cpp`, em `renderNow`: troque o eco `out.width = r.size;` por `out.size = r.size; out.tile = r.tile;` (e ajuste o comentário "THE ECHO", que agora fala de `size` e `tile`, não de `width`). O render:

```c++
    rb::IconRenderOptions io;
    io.size = r.size;
    io.context = r.context;
    io.viewport = rb::IconViewport{r.tile.x, r.tile.y, r.tile.w, r.tile.h};
    auto icon = rb::renderIcon(device, r.bundle, io);
    if (icon && icon->viewportRefused && r.fallbackSize > 0) {
        // O teto (spec 2026-09-16): o canvas inteiro na resolucao base, e o
        // painel estica -- o que ele fazia antes desta frente.
        io.size = r.fallbackSize;
        io.viewport = rb::IconViewport{};
        icon = rb::renderIcon(device, r.bundle, io);
        out.refined = false;
    }
    if (!icon) { ... como hoje ... }
    out.width = icon->width;
    out.height = icon->height;
    out.gridSize = icon->size;
    out.originX = icon->originX;
    out.originY = icon->originY;
```

- [ ] **Step 4: compilar**

Run: `cmake --build --preset mingw`
Expected: falha em `RenderCoordinator.cpp`, onde a chave respondida ainda usa `result->width`. Corrija só essa linha, que a Task 8 reescreve de qualquer jeito:

```c++
        const Key answered{result->version, result->context, result->size};
```

Compile de novo. Expected: compila.

- [ ] **Step 5: commit**

```bash
git add Source/IconComposerKit/Tile.h Source/IconComposerKit/Ports.h Source/IconComposerKit/RenderCoordinator.cpp Source/app/OnyxPorts.cpp
git commit -m "render de viewport, passo 7: pedido e resultado carregam o ladrilho, o resultado ecoa o que foi pedido e diz o que os pixels sao, e o job cai para o canvas inteiro na base quando o buffer passa do teto"
```

---

### Task 8: o Kit

Implementa a spec, "O que o Kit faz": a chave, a régua na base, o desenho no retângulo do ladrilho, o ladrilho velho durante a espera e o pedido só com o pan parado.

**Files:**
- Modify: `Source/IconComposerKit/Session.h:57-81` (`ViewContext`)
- Modify: `Source/IconComposerKit/Panels.h` (`RenderView`, e duas funções puras ao lado de `canvasImageRect`)
- Modify: `Source/IconComposerKit/RenderCoordinator.h/.cpp`
- Modify: `Source/IconComposerKit/PanelCanvas.cpp` (315, 415-440, e as funções puras perto de 123)
- Modify: `Tests/test_kit_canvas.cpp:66` (a régua deixou de ser `view.width`)

**Interfaces:**
- Consumes: `TileRect`, os campos novos de `RenderRequest`/`RenderResult` (Task 7).
- Produces:
  - `ViewContext::tileSize` (`uint32`, 0 = base), `ViewContext::tile` (`TileRect`), `ViewContext::settledSeconds` (`float`)
  - `RenderView::gridSize`, `RenderView::originX/originY`, `RenderView::refined`
  - `std::uint32_t canvasTileSize(std::uint32_t baseSize, float zoom)`
  - `TileRect canvasTileFor(CanvasRect painted, CanvasVec imageTopLeft, std::uint32_t baseSize, float zoom)`
  - `inline constexpr float kTileSettleSeconds = 0.15f;`

- [ ] **Step 1: o estado**

`Session.h`: inclua `Tile.h`. Em `ViewContext`, depois de `fitRequest`:

```c++
    // O LADRILHO (spec 2026-09-16). O canvas o escreve quando o pan e o zoom
    // ficam parados por `kTileSettleSeconds`, e o coordenador pede o que estiver
    // aqui. Enquanto a pessoa arrasta, estes dois NAO mudam -- e e isso que
    // impede um pedido por quadro. `tileSize == 0` e a resolucao base (`size`)
    // com o canvas inteiro, o pedido de sempre.
    std::uint32_t tileSize = 0;
    TileRect tile;
    float settledSeconds = 0.0f;
```

`Panels.h`, em `RenderView`, depois de `width, height`:

```c++
    // Onde a textura fica: `width` x `height` a partir de (`originX`, `originY`)
    // numa grade de `gridSize`. Zero = a textura e o canvas inteiro em `width`.
    std::uint32_t gridSize = 0;
    std::int32_t originX = 0, originY = 0;
    bool refined = true;
```

Ao lado de `canvasImageRect`:

```c++
inline constexpr float kTileSettleSeconds = 0.15f;
// A resolucao do ladrilho para um zoom: zero (a base) ate 100%, e a base
// ampliada acima disso.
std::uint32_t canvasTileSize(std::uint32_t baseSize, float zoom);
// O retangulo pintado, levado ao espaco de um ladrilho de `canvasTileSize`.
// Vazio (`w == 0`) quando nao ha nada pintado.
TileRect canvasTileFor(CanvasRect painted, CanvasVec imageTopLeft, std::uint32_t baseSize,
                       float zoom);
```

- [ ] **Step 2: as funções puras**

`PanelCanvas.cpp`, depois de `canvasIntersect`:

```c++
std::uint32_t canvasTileSize(std::uint32_t baseSize, float zoom) {
    if (!(zoom > 1.0f) || baseSize == 0) return 0;
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(baseSize) * zoom));
}

TileRect canvasTileFor(CanvasRect painted, CanvasVec imageTopLeft, std::uint32_t baseSize,
                       float zoom) {
    const std::uint32_t t = canvasTileSize(baseSize, zoom);
    if (t == 0 || painted.empty()) return TileRect{};
    // Tela -> ladrilho. O lado na tela e `base * zoom` e o ladrilho tem `t`
    // pixels, entao a razao e ~1 e so absorve o arredondamento de `t`.
    const double k = static_cast<double>(t) / (static_cast<double>(baseSize) * zoom);
    const std::int64_t tMax = static_cast<std::int64_t>(t);
    auto lo = [&](float v, float o) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor((v - o) * k)), 0, tMax);
    };
    auto hi = [&](float v, float o) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil((v - o) * k)), 0, tMax);
    };
    const std::int64_t x0 = lo(painted.x0, imageTopLeft.x), x1 = hi(painted.x1, imageTopLeft.x);
    const std::int64_t y0 = lo(painted.y0, imageTopLeft.y), y1 = hi(painted.y1, imageTopLeft.y);
    if (x1 <= x0 || y1 <= y0) return TileRect{};
    return TileRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                    static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
}
```

- [ ] **Step 3: a régua é a base**

`PanelCanvas.cpp:315`: a régua deixa de ser a textura, porque a textura agora pode ser um ladrilho.

```c++
    // The ruler for every number below is the PREVIEW SIZE, not the texture:
    // since the viewport render (spec 2026-09-16) the texture may be a tile of
    // a larger grid, and measuring the canvas by it would move the icon every
    // time a tile landed.
    const float sidePx = static_cast<float>(v.size);
```

Em `Tests/test_kit_canvas.cpp:66`, onde `v.width = side` fazia o papel da régua, passe a escrever também `s.view.size = side` (ou só isso), para o teste medir a mesma coisa que media.

- [ ] **Step 4: o ladrilho pedido com o pan parado, e o desenho no retângulo dele**

Depois de `st.painted = canvasIntersect(st.image, st.clip);` (~419):

```c++
    // O ladrilho: so quando o pan e o zoom pararam (spec 2026-09-16). O
    // coordenador pede o que estiver em `v.tile`, e enquanto a pessoa arrasta
    // isto nao muda.
    const bool settled = v.zoom == v.zoomTarget && v.panX == v.panTargetX && v.panY == v.panTargetY;
    v.settledSeconds = settled ? v.settledSeconds + io.DeltaTime : 0.0f;
    if (v.settledSeconds >= kTileSettleSeconds) {
        const TileRect want = canvasTileFor(st.painted, CanvasVec{tl.x, tl.y}, v.size, v.zoom);
        v.tileSize = want.w ? canvasTileSize(v.size, v.zoom) : 0;
        v.tile = want;
    }
```

O desenho (433), com a textura posta no retângulo que ela cobre. Durante a espera, o ladrilho velho continua no retângulo dele, e um zoom o estica até o novo chegar:

```c++
    const float fullSide = sidePx * v.zoom;
    if (view.texture != ImTextureID_Invalid && view.width > 0) {
        const float grid = view.gridSize > 0 ? static_cast<float>(view.gridSize) : static_cast<float>(view.width);
        const float k = fullSide / grid;
        const ImVec2 a(tl.x + view.originX * k, tl.y + view.originY * k);
        const ImVec2 b(a.x + view.width * k, a.y + view.height * k);
        dl->AddImage(view.texture, a, b);
        st.textured = true;

        if (s.selection && s.selection->layer) drawSelectionOverlay(s, dl, tl, fullSide);
    }
```

Confira que `drawSelectionOverlay` recebe o lado do CANVAS inteiro na tela, que é o que ele recebia quando a textura era o canvas.

- [ ] **Step 5: o coordenador**

`RenderCoordinator.h`, em `Key`: acrescente `TileRect tile;` depois de `size`.

`RenderCoordinator.cpp`, em `tick`:

```c++
    const ViewContext& v = s.view;
    const bool tiled = v.tileSize > 0 && v.tile.w > 0;
    const Key now{s.version(), v.context, tiled ? v.tileSize : v.size,
                  tiled ? v.tile : TileRect{}};
    if (!everRequested_ || !(now == requested_)) {
        RenderRequest r{now.version, s.bundle().clone(), now.context, now.size};
        r.tile = now.tile;
        r.fallbackSize = v.size;
        ...
```

Na resposta:

```c++
        const Key answered{result->version, result->context, result->size, result->tile};
```

e, ao aceitar, copie `view_.gridSize = result->gridSize; view_.originX = result->originX; view_.originY = result->originY; view_.refined = result->refined;` junto com `width`/`height`, **nos dois ramos** (update e create). Se a textura é recriada quando `width`/`height` mudam, isso continua valendo.

Atualize o comentário acima de `answered`: a chave inteira agora inclui o ladrilho, e o `width` do resultado deixou de ser a `size`.

- [ ] **Step 6: compilar e ver andando**

Run: `cmake --build --preset mingw`
Expected: compila.

Abra o app (`build/mingw/Source/app/iconcomposer.exe`) com `References/corpus/Jellify-Music__App__teal-icon-composer` e faça:
1. zoom de 400% com a roda, e pare: em ~150 ms aparece "rendering…", e depois a borda fica nítida;
2. arraste: o ladrilho velho acompanha o pan, a área fora dele mostra o fundo do painel, e nenhum render sai enquanto o arraste dura (o marcador âmbar só acende quando você solta);
3. volte a 100%: o canvas inteiro na base.

Anote o que viu. Se algo divergir, o comportamento esperado está na spec, "O que o Kit faz".

- [ ] **Step 7: commit**

```bash
git add Source/IconComposerKit Tests/test_kit_canvas.cpp
git commit -m "render de viewport, passo 8: o canvas pede o ladrilho do retangulo visivel quando o pan e o zoom param por 150 ms, a chave do coordenador ganha o ladrilho, a regua passa a ser a size base e a textura e posta no retangulo que ela cobre -- o ladrilho velho acompanha o pan durante a espera"
```

---

### Task 9: fechar a spec

**Files:**
- Modify: `Docs/Specs/2026-09-16-viewport-render.md`

- [ ] **Step 1: o que foi medido e o que ficou aberto**

No fim da spec, acrescente uma seção `## Estado em <data>` com:
- os commits dos passos 1-8;
- a margem medida (Task 6);
- se o gate precisou tratar resíduo de GPU na Task 3, e o que decidiu;
- o que o teste à mão da Task 8 mostrou;
- `[OBS]` o que ficou aberto: HiDPI (o ladrilho usa pixel lógico, como a textura de hoje), e o tempo de render de um ladrilho num ícone pesado, que não foi medido.

- [ ] **Step 2: commit**

```bash
git add Docs/Specs/2026-09-16-viewport-render.md
git commit -m "spec do viewport: o estado depois da implementacao, com a margem medida e o que ficou aberto"
```

---

## Se precisar de rede

- A suíte inteira (`IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe`, ~48 s) é o que pega regressão fora do viewport: `test_icon_render`, `test_chiclet_*`, `test_glass_layer` e `test_rb_field` tocam as assinaturas que este plano mudou. Rode **uma vez**, no fim, se o Jean pedir.
- O caso `viewport_default_is_the_whole_canvas` é o que diz que o padrão reduz a hoje. Se ele falhar, pare tudo: algum sítio mudou a aritmética do render cheio.
