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
CoreSVG                 ->  nada. stdlib pura.
RenderBox               ->  Vulkan
IconRendering           ->  RenderBox, CoreSVG, IconComposerFoundation
IconComposerKit         ->  IconRendering, IconComposerFoundation, Onyx
cli                     ->  IconComposerFoundation, IconRendering
app                     ->  IconComposerKit, Onyx
```

**O AquaKit não entra ainda, e a razão é dele.** Hoje ele tem o pipeline de
material, um `Button` e o banner — é a prova de que o vidro funciona, não uma
biblioteca de componentes. Uma UI inteira precisa de campo de texto, lista,
sidebar, popover, slider, toolbar; nada disso existe. Declarar a dependência
agora seria vender uma interface que ninguém pode construir. Ele entra quando
tiver componentes, e o lugar dele já está reservado: o topo, ao lado do Onyx.

## As dependências externas

**OnyxSDK** é o toolkit de aplicação — o papel que AppKit e SwiftUI fazem no
alvo. Shell, janela, painéis, `ViewerRegistry`, e os serviços que ninguém quer
reescrever (`AppConfig`, `RecentFiles`, `TaskManager`, `Logger`, `Appearance`,
`SystemTheme`). Desde a v1.0.0 ele é **Vulkan** — Vulkan-Headers, volk, VMA,
glslang, `imgui_impl_vulkan`.

**AquaKit** é o material — o papel do QuartzCore e do DesignLibrary. O lugar dele
é o topo, dando o *chrome*: janela, sidebar e controles em Liquid Glass. **Não
entra nesta rodada**: ver a nota na ordem topológica acima. O que ele tem hoje é
o pipeline de vidro provado, um `Button` e o banner — material, não biblioteca de
componentes.

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

Nome igual não é IR igual, e o diferencial que fecharia isso não roda hoje (o
`References/` do AquaKit está vazio nesta máquina). Mas a torre `IconRendering`
deste repositório pode não ser RE nova coisa nenhuma — pode ser porte do que já
está decodado. Isso é para medir antes de planejar.

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

**A API de render das torres `RenderBox`/`IconRendering` não está escolhida**, e
não deve ser escolhida antes do decode: é o decode do `default.metallib` que diz
se o pipeline precisa de fp16, de compute, e em que espaço os dez blend modes
compõem. Escolher a API antes disso é escolher a coleira antes do cachorro.

## O nome

Em aberto. `IComposer` e `IconStudio` estão na mesa — e `IconStudio` tem a graça
de ser o `ProjectName` que a própria Apple carrega no `version.plist` do app.
