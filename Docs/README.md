# Icon Composer, decodificado

Engenharia reversa do Icon Composer da Apple — o formato que ele edita, o motor
que ele desenha e a interface que ele apresenta — com o objetivo de suporte
completo fora do macOS.

Tudo aqui foi medido de binários e dados da Apple. Nada foi adivinhado, e onde
uma afirmação não é medida ela está marcada como pergunta aberta em vez de
preenchida com uma resposta plausível.

**Alvo:** Icon Composer **2.0 (125)**, beta 6. Proveniência em
`References/README.md`.

## Os documentos

| doc | responde |
|---|---|
| [00 — Levantamento](00-levantamento.md) | o que existe, o que falta, e as decisões que travavam o resto. **Comece aqui.** |
| [01 — O formato `.icon`](01-o-formato-icon.md) | o documento que o app edita: chaves, vocabulários, a gramática dos valores, e o que ainda não fechou |
| [02 — O bundle `.icon`](02-o-bundle-icon.md) | a pasta em volta do documento: `Assets/`, o que mora nele, e a referência que nem sempre resolve |
| [04 — O SVG](04-o-svg.md) | o que os assets contêm, o que o CoreSVG da Apple lê, e três filtros que ela NÃO lê |
| [03 — O motor de render](03-o-motor-de-render.md) | o mapa do `IconRendering` sobre o `RenderBox`: os dois metallib, as peças do vidro, e o vocabulário que bate com o do AquaKit |
| [_confrontar/](_confrontar/README.md) | material do projeto anterior, **sem selo** — lista de perguntas, nunca fonte |
| [Specs/arquitetura](Specs/2026-08-31-arquitetura-design.md) | as camadas deste repositório, copiadas das do alvo, e onde Onyx e AquaKit entram |

## A versão de um parágrafo

O `.icon` é um bundle cujo `icon.json` descreve uma composição em **grupos de
camadas**, cada camada apontando para um SVG. Toda propriedade de aparência vem
em par — o valor e uma lista de **especializações** que o sobrescrevem conforme
`idiom`, `appearance`, `localization` e `language-direction`; entre as que casam
o contexto vence a mais específica, e a sem predicado é o default. O desenho não
está no arquivo: ele sai do `IconRendering.framework` sobre o `RenderBox`, com os
shaders no `default.metallib`, e o `EffectsRenderMode` escolhe entre duas
gerações do efeito Liquid Glass. Quatro das cinco funções de vidro do `RenderBox`
têm o mesmo nome das que o AquaKit já transcreveu do QuartzCore (doc 03 §4).

Medido no corpus: **271 grupos e 437 camadas** em 145 documentos, com **1.740
especializações** — 1.040 chaveadas por `appearance`, 616 sem predicado, 82 por
`idiom` e 2 pelos dois.

## Estado

| peça | estado |
|---|---|
| container `.dmg` (UDIF) | **decodado** — `udif.py`, checksums conferidos |
| volume HFS+ e decmpfs | **decodado** — `hfs.py`, extração provada contra a assinatura da Apple |
| chaves do `.icon` | **decodadas** — e conferidas contra 145 documentos reais |
| vocabulários (`Appearance`, `BlendMode`, `Fill.Kind`, …) | **decodados e fechados** — zero valores órfãos em 145 documentos |
| gramática dos valores do `.icon` | **decodada para o que o corpus usa** — sete pontas abertas em doc 01 §10 |
| leitor e escritor de `icon.json` | **passa o gate** — 145 lidos, 135 byte-exatos contra o encoder da Apple |
| modelo do documento (`IconDocument`) | **passa o gate** — 145 de 145 sem uma chave desconhecida |
| resolução de especialização | **passa o gate** — 1.740 de 1.740 alcançáveis; a regra está medida, doc 01 §5 |
| leitura tipada dos valores (cores, enums, geometria) | **passa o gate** — 61.537 valores decodados, 0 falhas |
| o bundle (`Assets/`, a referência das imagens) | **passa o gate** — 55 bundles, 0 arquivos mortos, 2 referências penduradas conhecidas |
| SVG: XML, path, geometria, pintura, gradientes, folha de estilo | **passa o gate** — 149/149 lidos, **128 totalmente compreendidos**, 477 formas, 6.564 segmentos |
| SVG: filtro, máscara, recorte, padrão | **não começados** — e cada um é reportado por arquivo |
| CLI do `ictool` / `icrtool` | **levantada** — doc 01 §11: comandos, gramática, e o `icrtool` como auxiliar interno |
| o nosso `ictool` | **passa o gate** — 1.100 árvores renderizadas, nenhuma caindo em JSON cru |
| a UI do app | **não levantada** — e não há nib: o app é SwiftUI |
| `IconRendering` + `RenderBox` + os dois `default.metallib` | **mapeados** — doc 03: 123 módulos, 82 entry points, 12 funções stitchable, 197 descritores Swift. **Nada decodado.** |
| o diferencial IR do vidro (`glassBackground_v1` × QuartzCore) | **medido** — doc 03 §4.1: 12 constantes em comum, **11 com multiplicidade idêntica**. Mesmo fonte. |
| M2·P0 — a torre `RenderBox`, Vulkan headless | **passa o gate** — device, buffer, submit e leitura de volta em GPU real |
| M2·P1 — o buffer de path no layout do alvo | **passa o gate** — as 4 convenções do `path_edges_vertex`, medidas no IR (doc 03 §7) |
| M2·P2 — o passe `edges`, transcrito | **passa o gate** — GPU × oráculo de CPU, **bit a bit** (doc 03 §8) |
| M2·P3 — os passes `interior` e `exterior` | **passa o gate** — o leque a partir do `origin` e a cobertura analítica (doc 03 §9) |
| o gate sobrevive a um crash | **corrigido** — backup em disco com marcador, e as 66 âncoras conferidas antes do primeiro build (doc 03 §9.5, §9.6) |
| M2·P4 — o traço | **levantado, não transcrito** — os 3 pares, os 3 layouts e a indexação medidos; a geometria ramifica em bits do `RenderState` não decodados (doc 03 §10) |
| o `RenderState` do `RB::Shader` | **layout medido; 4 bits nomeados** — é um `uint4`, 37 máscaras; `extended_color`/`floating_point_color`/`reads_dest`/`reads_coverage` resolvidos, mas os campos que travam o traço seguem sem semântica (doc 03 §11) |
| M2·P5 — alvo de render, o pixel | **não começado** |

## Build

C++23, CMake, sem terceiros. `IconComposerFoundation` não linka GPU nem UI, e o
corpus é apontado por variável de ambiente — um teste que precisa dele e não a
acha **falha**, não pula.

```powershell
cmake --preset mingw          # ou msvc, mingw-asan, release
cmake --build --preset mingw

$env:IC_CORPUS_DIR = "D:\CodingProjects\Icon-Composer\References\corpus"
build\mingw\Tests\ic_tests.exe

build\mingw\Source\cli\ictool.exe References\corpus\<bundle>
build\mingw\Source\cli\ictool.exe --tree References\corpus\<bundle> --appearance dark
build\mingw\Source\cli\ictool.exe --assets References\corpus\<bundle>
```

Do Git Bash o executável sai com **127** — descasamento de loader do MinGW, o
mesmo que o SF-Symbols registra. Rode do PowerShell.

### O gate da M1

`scripts/gate-m1.ps1` é o teste de aceitação do leitor, do escritor e do modelo
de `.icon`. São três coisas, e a terceira é a que carrega o peso:

1. A suíte, em árvore limpa, contra **145 documentos `.icon` reais** de 145
   projetos públicos.
2. **Dois diferenciais.** O do *escritor*: 135 documentos voltam byte a byte
   idênticos ao que o `JSONEncoder` da Apple escreveu — os outros 10 tiveram um
   formatador passado por cima no repositório de origem, e a idempotência prova
   que o conteúdo deles atravessa intacto. O do *modelo*: **nenhuma chave** dos
   145 documentos pode ser desconhecida, e **nenhuma das 1.740 especializações**
   pode ser inalcançável pelo resolver.
3. **Uma varredura de mutação obrigatória.** Sessenta e seis defeitos entram um a um e cada
   um TEM que avermelhar a suíte, aplicados em árvore limpa e restaurados de um
   backup conferido por SHA-256 — nunca por comando de git.

**gate-m1 passou em 2026-08-31** — 145 documentos, 135 byte-exatos, 145 de 145
totalmente compreendidos, 1.740 de 1.740 especializações alcançáveis, 61.537
valores decodados sem uma falha, 55 bundles sem arquivo morto, 1.100 árvores
renderizadas sem cair em JSON cru, 149 SVGs lidos em geometria com 128 deles
totalmente compreendidos, e **66 de 66 mutações pegas**.

| # | mutação | asserções avermelhadas |
|---|---|---|
| 1 | número decodado para `double` em vez de guardado como lexema | 13 |
| 2 | espaço em volta dos dois-pontos removido | 3 |
| 3 | chaves deixam de ser ordenadas | **1** |
| 4 | lixo no fim do arquivo aceito | 2 |
| 5 | array aberto sem a quebra de linha | 2 |
| 6 | `null` escrito errado | 3 |
| 7 | vocabulário de `appearance` perde um caso | 2 |
| 8 | predicado de `appearance` comparado ao contrário | 10 |
| 9 | especificidade ignorada, a primeira que casa vence | 4 |
| 10 | a propriedade simples passa na frente das especializações | **1** |
| 11 | uma forma aninhada aceita qualquer chave | **1** |
| 12 | camadas conferidas contra o conjunto de chaves do documento | 4 |
| 13 | cinza lido com quatro componentes em vez de dois | 2 |
| 14 | componente aceito como mero prefixo do número | 2 |
| 15 | `BlendMode` perde um caso | 2 |
| 16 | `ShadowKind` grafado à Swift, não como no disco | 2 |
| 17 | um gradiente linear perde a orientação | **1** |
| 18 | imagens coletadas só no contexto base | 3 |
| 19 | asset sem referência nunca reportado | **1** |
| 20 | qualquer pasta com um `icon.json` tomada por bundle | **1** |
| 25 | cúbica suave sempre reflete, mesmo depois de uma reta | **1** |
| 26 | moveto repetido continua moveto em vez de virar reta | **1** |
| 27 | linha horizontal esquece o y corrente | **1** |
| 28 | tag de fechamento não precisa casar com o que fecha | **1** |
| 29 | dados de caractere descartados | 2 |
| 30 | lista de transform composta na ordem errada | **1** |
| 31 | o que o `defs` define nunca é reportado | 2 |
| 32 | pintura descartada em silêncio em vez de nomeada | 3 |
| 33 | hex de três dígitos escalado por 16 em vez de 17 | **1** |
| 34 | cor display-p3 reportada como sRGB | 2 |
| 35 | referência url() mantém o # | 3 |
| 36 | o atributo de apresentação passa na frente do style | 2 |
| 37 | pintura deixa de herdar pela árvore | **1** |
| 38 | rect arredondado perde o raio do canto | **1** |
| 39 | seletor de classe mantém o ponto, e nada casa | 10 |
| 40 | as folhas de estilo nunca são coletadas | 5 |
| 41 | regra de classe nunca chega ao elemento | 5 |
| 21 | uma sombra despejada como JSON cru | 3 |
| 22 | camada de vidro deixa de ser marcada | **1** |
| 23 | a árvore ignora o contexto que recebeu | **1** |
| 24 | referência pendurada reportada como sã | **1** |

> **A largura importa mais que o booleano.** Seis mutações são pegas por **uma**
> asserção só, e sempre a de um teste unitário — o corpus não as alcança. A nº3
> porque os 145 documentos já vêm com as chaves ordenadas pela Apple; a nº10 e a
> nº11 porque nenhum documento tem a propriedade simples contradizendo uma
> especialização, nem chave estranha em forma aninhada; a nº17, nº19 e nº20 pela
> mesma razão. Sem esses testes, os defeitos passariam pelo corpus inteiro sem
> uma queixa.
>
> A nº18 é o contra-exemplo instrutivo: nasceu pega por **1** asserção, e virou
> **3** quando o corpus de bundles passou a incluir documentos que especializam
> o `image-name` (doc 02 §4). Cobertura de corpus é uma escolha, não um dado.
>
> **E a varredura reprova quem passa por acidente.** A nº39 nasceu reportada como
> "pega com ZERO asserções" — o que é a forma de um *crash*, não de um teste
> reparando: o teste usava `map::at`, que lança numa chave ausente. Um processo
> que morre esconde todo caso depois dele. O gate passou a exigir que a mutação
> avermelhe uma ASSERÇÃO, e trata um crash como defeito **do teste**; corrigido,
> a nº39 é pega por 10.

## Ferramentas

Os scripts de RE não moram aqui: vivem em `AquaKit/References/scripts/`, a caixa
compartilhada dos três projetos (AquaKit, SF-Symbols, Icon-Composer). A
disciplina de selos é a do AquaKit, em `docs/metodo-re.md` de lá.

O pipeline do DMG ao bundle está em `References/README.md`.
