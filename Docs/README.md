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
| [03 — O motor de render](03-o-motor-de-render.md) | o mapa do `IconRendering` sobre o `RenderBox`: os dois metallib, as peças do vidro, e o vocabulário que bate com o do AquaKit |
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
| CLI do `ictool` / `icrtool` | **levantada** — doc 01 §11: comandos, gramática, e o `icrtool` como auxiliar interno |
| o nosso `ictool` | **passa o gate** — 1.100 árvores renderizadas, nenhuma caindo em JSON cru |
| a UI do app | **não levantada** — e não há nib: o app é SwiftUI |
| `IconRendering` + `RenderBox` + os dois `default.metallib` | **mapeados** — doc 03: 123 módulos, 82 entry points, 12 funções stitchable, 197 descritores Swift. **Nada decodado.** |

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
3. **Uma varredura de mutação obrigatória.** Vinte e quatro defeitos entram um a um e cada
   um TEM que avermelhar a suíte, aplicados em árvore limpa e restaurados de um
   backup conferido por SHA-256 — nunca por comando de git.

**gate-m1 passou em 2026-08-31** — 145 documentos, 135 byte-exatos, 145 de 145
totalmente compreendidos, 1.740 de 1.740 especializações alcançáveis, 61.537
valores decodados sem uma falha, 55 bundles sem arquivo morto, 1.100 árvores
renderizadas sem cair em JSON cru, e **24 de 24 mutações pegas**.

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

## Ferramentas

Os scripts de RE não moram aqui: vivem em `AquaKit/References/scripts/`, a caixa
compartilhada dos três projetos (AquaKit, SF-Symbols, Icon-Composer). A
disciplina de selos é a do AquaKit, em `docs/metodo-re.md` de lá.

O pipeline do DMG ao bundle está em `References/README.md`.
