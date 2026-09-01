# Arquitetura — desenho

2026-08-31. O que este repositório é, em que camadas, e por quê.

## O princípio

A camadagem é **copiada do alvo**, e o alvo foi medido, não suposto. Dos load
commands do `Icon Composer.app` (doc 00 §2):

```
Icon Composer.app          161 KB — só o shell
├── @rpath  (privado do app, viaja no bundle)
│   ├── IconComposerKit          a UI, em SwiftUI
│   └── IconComposerFoundation   o modelo do documento .icon
└── /System/…  (do sistema, caminho absoluto, compartilhado)
    ├── SwiftUI, AppKit          o toolkit de UI
    ├── IconRendering            o render do ÍCONE
    ├── RenderBox                o motor de render genérico
    ├── CoreSVG                  SVG
    └── QuartzCore, CoreImage, Metal, ImageIO, TextureIO
```

A Apple separa o app (fino) das libs reutilizáveis (do sistema) e do que é
genuinamente privado (duas frameworks). Este repositório faz o mesmo, com os
mesmos nomes — a mesma convenção de torres que o AquaKit usa em `Source/`.

**Os nomes são de papel, não de cópia.** Uma torre chamada `RenderBox` é a
*nossa* reimplementação limpa daquilo que o `RenderBox` da Apple faz, e vale a
disciplina de `docs/metodo-re.md` do AquaKit: o repo carrega o resultado
reimplementado e o selo que aponta para a origem, nunca o material da origem.

## As pastas

```
Icon-Composer/
├── CMakeLists.txt · CMakePresets.json
├── Docs/
│   ├── README.md                 índice e estado por peça
│   ├── NN-*.md                   um documento por camada decodada
│   ├── _confrontar/              o RE do repositório anterior — sem selo
│   ├── Specs/                    este arquivo, e os próximos
│   └── Plans/                    um plano por milestone
├── References/                   o corpo do delito, fora do git
│   ├── 2.0-125/                  o build alvo, extraído
│   └── corpus/                   .icon reais de terceiros
├── Source/
│   ├── IconComposerFoundation/   o modelo do .icon
│   ├── CoreSVG/                  parse e rasterização de SVG
│   ├── RenderBox/                o motor de render genérico
│   ├── IconRendering/            o render do ícone, sobre RenderBox
│   ├── IconComposerKit/          a UI: painéis, inspetor, canvas
│   ├── cli/                      os equivalentes de ictool e icrtool
│   └── app/                      o executável
├── Tests/
├── scripts/                      os gates, um por milestone
└── third_party/
```

## A ordem topológica

```
IconComposerFoundation  ->  nada. stdlib pura, sem GPU, sem UI.
CoreSVG                 ->  nada. stdlib pura.  [XML, path, geometria de pe]
RenderBox               ->  Vulkan
IconRendering           ->  RenderBox, CoreSVG, IconComposerFoundation
IconComposerKit         ->  IconRendering, IconComposerFoundation, Onyx
cli                     ->  IconComposerFoundation, IconRendering
app                     ->  IconComposerKit, Onyx
```

**O AquaKit não aparece nesse grafo, e isso é decisão fechada (2026-09-01).**
A UI deste projeto é do **OnyxSDK e de mais ninguém**. Hoje o AquaKit tem o
pipeline de material, um `Button` e o banner — é a prova de que o vidro funciona,
não uma biblioteca de componentes; uma UI inteira precisa de campo de texto,
lista, sidebar, popover, slider e toolbar, e nada disso existe. Declarar a
dependência seria vender uma interface que ninguém pode construir.

O que **não** muda: o AquaKit continua central neste projeto, só que **de fora do
grafo de link**. Ele é a caixa de engenharia reversa — `dsc_reader.py`,
`metallib_extract.py`, `udif.py`, `hfs.py`, `target.py`, a disciplina de selos —
e é dele que sai o material que alimenta o decode do `IconRendering`. Uma
dependência de *método*, não de *biblioteca*.

## As dependências externas

**OnyxSDK** é o toolkit de aplicação — o papel que AppKit e SwiftUI fazem no
alvo. Shell, janela, painéis, `ViewerRegistry`, e os serviços que ninguém quer
reescrever (`AppConfig`, `RecentFiles`, `TaskManager`, `Logger`, `Appearance`,
`SystemTheme`). Desde a v1.0.0 ele é **Vulkan** — Vulkan-Headers, volk, VMA,
glslang, `imgui_impl_vulkan`.

**AquaKit** não é dependência externa deste projeto — **é a bancada**. Ele
carrega a caixa de RE compartilhada pelos três repositórios (`References/scripts/`)
e o decode do QuartzCore contra o qual o `IconRendering` vai ser conferido. O
papel de *chrome* em Liquid Glass que este documento reservava para ele está
**cancelado nesta rodada**: a UI é do Onyx.

**AquaKit não é a engine do ícone** — mas os dois estão MUITO mais próximos do
que este documento afirmou quando foi escrito. A afirmação original era que o
vidro do ícone é "um subsistema separado do qual o AquaKit não tem uma linha",
apoiada em um grep por `RenderBox`/`IconRendering` na árvore dele.

O grep estava certo e a conclusão estava errada. O doc 03 §4 mediu: **quatro das
cinco funções de vidro do `RenderBox` — `glassBackground`, `glassForeground`,
`displacementMap`, `distanceGradient` — têm exatamente o nome das que o AquaKit
já transcreveu do metallib do QuartzCore**, e o `HighlightsSet` do
`IconRendering` (`keySharp`, `keyDiffuse`, `fillSharp`, `fillDiffuse`, `dark`,
`rim`) é campo a campo o vocabulário do `KeyFillHighlight` dele. O vidro do ícone
e o do controle falam a mesma língua.

Nome igual não é IR igual, e o diferencial é o que fecha isso. **Ele roda.**

> **Correção (2026-09-01).** Este parágrafo dizia que o diferencial não rodava
> porque *"o `References/` do AquaKit está vazio nesta máquina"*. Errado, e o
> erro era meu: o material do AquaKit mora **fora da worktree por regra
> registrada** — um diretório `.gitignore`d dentro de um repositório é
> exatamente o que o `git clean` existe para apagar, e já apagou duas vezes lá.
> `python References/scripts/target.py` responde onde ele está: os dois dyld
> shared caches **já extraídos**, 82 partes cada, em `G:\AquaKit-refs`. E o
> `TARGET` do AquaKit é **`26A5416b`** — o mesmo build de macOS que produziu
> este Icon Composer (`DTPlatformBuild = 26A5388g`, `ProjectName = IconStudio`,
> SDK `macosx27.0.internal`). As duas pontas do diferencial são do mesmo trem.

A torre `IconRendering` deste repositório pode não ser RE nova coisa nenhuma —
pode ser porte do que já está decodado. Isso é para medir antes de planejar.

## As três regras

Cada uma foi paga por um projeto irmão:

1. **`IconComposerFoundation` não linka GPU nem UI, e compila sem Vulkan.** É a
   camada que os testes martelam. Vem do `Source/core` do SF-Symbols, que também
   proíbe exceções e faz erro viajar como `Result<T>`.
2. **Só `app/` linka o Onyx.** Era o desenho do `sfsymview` — `sfsymview_lib` é
   ImGui puro, sem janela, sem GL, sem Onyx, e é o que os testes linkam. Com o
   Onyx em Vulkan a regra vale mais: um teste que espera o glslang compilar é um
   teste que ninguém roda.
3. **O corpus nunca entra no git.** Aponta-se por variável de ambiente, e um
   teste que precisa do corpus e não acha a variável **falha** — não pula.

## O que não está decidido

**Quando e como o AquaKit entra.** A pergunta não é de arquitetura, é de
maturidade: ele precisa de componentes antes de poder vestir um app. Quando
chegar lá, sobra decidir se ele disputa a janela com o `WindowDecorator` /
`Appearance` / `SystemTheme` do Onyx ou se entra como renderer de material dentro
do frame dele. Com os dois em Vulkan a segunda é a saída provável — mas isso é
hipótese, não decisão, e nada aqui depende dela hoje.

**O AquaKit ainda não é consumível.** Não há `install()` nem `export()` em
nenhum ponto da árvore dele, e nenhum alvo `AquaKit::`. As torres existem e a
ordem topológica existe (`cmake/Towers.cmake`); falta a embalagem.

~~**A API de render das torres `RenderBox`/`IconRendering` não está escolhida**~~
— **DECIDIDA em 2026-09-01: Vulkan.**

O critério que este parágrafo estabeleceu foi respeitado, não contornado: a
escolha esperou o decode, e o decode aconteceu. Doc 03 §7 mediu que o alvo
rasteriza path **na GPU**, subindo `CubicSegment` para um buffer de device e
expandindo num vertex shader — `path_edges_vertex`, com `shader_path.metal` e
`shader_stroke.metal` inteiros dedicados a isso.

Logo um rasterizador de CPU não seria um degrau; seria outro algoritmo, com
outro antialiasing. Vulkan também é o que o Onyx e o AquaKit falam, então não há
costura entre nenhum par de peças deste projeto.

A torre é **headless**: sem surface, sem swapchain, sem janela. A regra 2
continua intacta — ela fala de **Onyx**, não de Vulkan, e uma torre pode linkar
a API sem linkar o toolkit. A regra 1 também: `IconComposerFoundation` segue sem
GPU.

**O que a decisão NÃO resolve, e está registrado para não virar surpresa:** não
existe oráculo de pixel. A M1 teve dente porque o corpus trazia a saída do
encoder da Apple; o corpus não tem um único `.icns`, e os 60 PNG dele são
camadas de ENTRADA. Sem macOS não roda `icrtool`. O gate do render é, por
enquanto, invariante + mutação + oráculo de CPU nos pedaços que têm um.

## O nome — **IconStudio** (2026-09-01)

`[BIN]` A convenção da Apple, medida nos oito `version.plist` do bundle — o app,
os cinco frameworks e os dois appex:

| campo | valor | onde vive |
|---|---|---|
| `ProjectName` | **`IconStudio`** | `version.plist`, os oito, sem exceção |
| `CFBundleName` | `Icon Composer` | `Info.plist` |
| `CFBundleIdentifier` | `com.apple.IconComposer` | `Info.plist` |

`[BIN]` E a separação é limpa: `IconStudio` aparece **zero vezes** dentro de
qualquer executável ou framework. É nome do *projeto de build*, escrito pelo
trem de compilação; o código só conhece `IconComposer`.

**A decisão, e ela inverte o mapa da Apple de propósito.** O produto chama
**IconStudio**; o código continua `IconComposer*` (`icf::`,
`IconComposerFoundation`, `IconComposerKit`, `CoreSVG`, `RenderBox`).

A inversão é o ponto. Na Apple o nome interno é arbitrário e o do produto é o
real. Aqui é o contrário: os nomes internos **não são escolha nossa** — são
copiados dos `LC_ID_DYLIB` do alvo, e é essa correspondência literal que deixa
uma afirmação nossa ser conferida contra o binário dele. Renomear a torre
`IconComposerFoundation` para `IconStudioFoundation` custaria a única coisa que
esses nomes compram. Já o produto é nosso, e chamá-lo `Icon Composer` seria
vestir o nome de um app da Apple que lê o mesmo formato — confusão, não homenagem.

Então: **por dentro, o nome do alvo, porque ele nomeia o alvo. Por fora, o
nosso.** O easter egg sobrevive intacto, só que do lado certo do espelho.
