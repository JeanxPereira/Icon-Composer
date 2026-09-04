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
| o `RenderState` do `RB::Shader` | **layout medido; 8 bits/campos nomeados** — é um `uint4`, 37 máscaras; `extended_color`/`floating_point_color`/`reads_dest`/`reads_coverage` resolvidos, mas os campos que travam o traço seguem sem semântica (doc 03 §11) |
| M2·P4b — os três fragments de cobertura | **passa o gate** — winding, área exata do pixel e distância, GPU × CPU (doc 03 §12) |
| M2·P5 — alvo de render, o primeiro pixel | **passa o gate** — cobertura conferida contra a **forma fechada** da área do retângulo (doc 03 §13) |
| o resolve — cobertura assinada vira alpha | **passa o gate** — `accumulator_shape`, e a regra de preenchimento é **um bit** (doc 03 §14) |
| a mescla | **levantada** — 56 casos, seletor em `(palavra1 >> 16) & 16383`; fecha a pergunta 5 do doc 01, **6 de 56 decodados** (doc 03 §15) |
| a cor (`composite`, caminho chapado) | **passa o gate** — premultiplicação, inversão e broadcast, GPU × CPU bit a bit (doc 03 §16) |
| `IconRendering` — a ponte documento↔motor | **mapeada** — `Icon`, `FinalizedIcon`, os 18 modos de mescla, `Fill.Contents`, `GlobalConfiguration` (doc 03 §17) |
| o pipeline `Icon → FinalizedIcon → imagem` | **levantado pelos seletores** — as duas etapas, as três famílias de saída, e os parâmetros de render; responde a pergunta 4 do doc 01. **Nenhum corpo desmontado** (doc 03 §18) |
| os defaults do render | **16 campos lidos e atribuídos** — fecha a pergunta 4 do doc 01; 126 escritas medidas no construtor global, **110 sem nome ganho** (doc 03 §19) |
| o conversor — SVG desenhado em pixels | **passa o gate** — as peças compostas, conferidas contra a forma fechada pela cadeia inteira. Alcance medido: **22 de 194 camadas**, **0 de 55 documentos** (doc 03 §20) |
| `icrender` e o PNG | **passam o gate** — binário separado do `ictool`, que segue sem GPU (doc 03 §20.6, §20.7) |
| os ícones do sistema (Fotos, App Store) | **localizados, não abertos** — não são `.icon`: compilam para `Assets.car` e o motor os lê via CoreUI. Segunda porta de entrada (doc 03 §20.8) |
| **o traço pintado** | **desenha** — `StrokeRender` achata, emite o fluxo de pontos que a CPU do alvo emite (os três sentinelas, o layout `k+2` aberto e `k+3` fechado) e avalia a cobertura do fragment. **O `miterlimit` é resolvido na CPU**, por ponto, e o campo `join` chega à GPU já degenerado. Os 7 caps e os 3 joins transcritos; `[ART]` o corpus exercita **um** par, `butt`+`miter`, porque nenhum dos 35 SVGs nomeia cap ou join (doc 03 §31, §33) |
| **a mescla, ligada** | **desenha** — por camada e por **grupo**, e o grupo ganha alvo próprio antes de ser misturado. A tradução são **dois saltos**, porque a ponte fala `CGBlendMode`: 18 posições conferindo com um cabeçalho público da Apple, o que resolve os dois modos que o casamento por fórmula deixara ambíguos — `plus-lighter` é 43 e `plus-darker` é 44. Os 56 casos ganham nome (doc 03 §32) |
| **o alcance, hoje** | `[ART]` **169 de 194 camadas (87,1%)** e **40 de 55 documentos (72,7%)**. O spec prometia 178 e 47, e o déficit é EXATAMENTE uma recusa nomeada: `169+9` e `40+7` são os 9 camadas e 7 documentos de um grupo que mescla **e** carrega vidro, onde o fundo que a refração amostra nunca foi lido (spec §8) |
| **o `fill` do documento — os 7 casos e os DOIS conversores** | **desenha** — e `automatic` é **duas operações com o mesmo nome**: no fundo é a rampa de chiclet, com um terceiro braço que ninguém previu (`IconColor.clear` sob `tinted`); na camada **não é cor nenhuma**, é herança de especialização do slot `light`. O `automatic-gradient` e o `linear-gradient` sem `orientation` vieram junto, pelo eixo default `(0,0)→(0,1)`. A régua foi de 111 para 145 camadas e de 1 para 33 documentos completos — e a mesma medição diz que **10 dos 18 documentos novos são variantes de cor de um único desenho** (doc 03 §30, spec §7) |
| **o vidro do ícone** | **desenha** — e o achado é que o ícone NÃO usa o `glassBackground_v1`: o `IconRendering` importa **um** dos cinco shaders de sistema, o `_RBSystemShaderDisplacementMap`. A régua vai de 79 para **111 camadas**, e a própria régua imprime que **32 delas desenham sem o material que o documento pede** (doc 03 §29, spec §9.1) |
| **a ponte dos uniforms do vidro** | **lida** — os 83 nomes de chave do `RenderBox` ligados a byte, largura, default e transformação; 54 chaves, 138 dos 256 bytes, com controle cruzando o empacotador ARM64 e o IR do shader (doc 03 §27) |
| os 56 modos de mescla | **lidos inteiros** — 56 de 56 blocos, 39 casados, as bandas nomeadas; 16 dos 18 nomes do formato com candidato único. **Corrige duas atribuições desta documentação** (doc 03 §26) |
| o gradiente, ligado no renderizador | **desenha** — `url(#id)` do SVG, linear e radial, `userSpaceOnUse` e `objectBoundingBox`, `gradientTransform`; e o `fill` da camada retinge a arte. Alcance **22 → 79 de 194 camadas** (doc 03 §25) |
| a regra do `automatic-gradient` | **lida e transcrita** — Rec.709, quatro faixas de fronteira **fixa**, boost e duas paradas ordenadas; fecha a pergunta 4 do doc 01 por inteiro (doc 03 §24) |
| o gradiente do `RenderBox` | **levantado** — o campo de 4 bits é **4 geometrias × 4 spreads**, decodado por duas funções independentes que concordam; mais o tipo de rampa e o gama (doc 03 §23) |
| leitura de PNG (`inflate` + decodificador) | **passa o gate** — **58 de 60** do corpus, 55,3 M pixels; fixtures do zlib do Python como produtor independente. Adam7 é lacuna nomeada, 2 de 60 (doc 03 §21) |

## Build

C++23, CMake, sem terceiros. `IconComposerFoundation` não linka GPU nem UI, e o
corpus é apontado por variável de ambiente — um teste que precisa dele e não a
acha **falha**, não pula.

A varredura pode ser rodada em **fatias**, e uma fatia **nunca** imprime
`gate-m1 passed`:

```powershell
powershell -File scripts\gate-m1.ps1                 # o gate
powershell -File scripts\gate-m1.ps1 -From 1 -To 40  # uma fatia, veredito PARCIAL

powershell -File scripts\gate-worktree.ps1           # o gate, num worktree isolado
```

**Prefira a terceira linha.** A varredura muta os fontes um a um durante uma hora
e quarenta, e nesse intervalo a árvore não é segura para tocar — uma edição feita
ao lado ou é confundida com mutação, ou é restaurada por cima. O runner dá à
varredura um checkout e um build próprios (`git worktree add --detach
D:/CodingProjects/Icon-Composer-gate main`, uma vez), e aponta o corpus — que é
quase todo gitignored — para a árvore principal, onde é lido e nunca escrito.

Isso também **contém o dano**: quando uma execução é morta no meio, o arquivo
mutado fica em disco, porque um `kill` não roda a limpeza. Num worktree esse
arquivo é um checkout descartável, e não a árvore em que se trabalha.

```powershell
cmake --preset mingw          # ou msvc, mingw-asan, release
cmake --build --preset mingw

$env:IC_CORPUS_DIR = "D:\CodingProjects\Icon-Composer\References\corpus"
build\mingw\Tests\ic_tests.exe

build\mingw\Source\cli\icrender.exe <arquivo.svg> --out saida.png --size 512
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
3. **Uma varredura de mutação obrigatória.** Duzentas e sessenta e sete mutações
   entram uma a uma, e cada uma TEM que avermelhar a suíte — aplicada em árvore
   limpa e restaurada de um backup conferido por SHA-256, nunca por comando de
   git. Desde 2026-09-03 a varredura roda num **worktree dedicado**
   (`scripts/gate-worktree.ps1`), para parar de tomar a árvore de trabalho como
   refém por duas horas.

**gate-m1 passou em 2026-09-03** — 145 documentos, 135 byte-exatos, 145 de 145
totalmente compreendidos, 1.740 de 1.740 especializações alcançáveis, 61.537
valores decodados sem uma falha, 55 bundles sem arquivo morto, 1.100 árvores
renderizadas sem cair em JSON cru, 149 SVGs lidos em geometria com 128 deles
totalmente compreendidos, 58 dos 60 PNGs decodados, e **231 de 231 mutações
pegas**.

A primeira passagem foi em **2026-09-01, com 119 mutações**. A lista cresceu
119 → 160 → 209 → 231, e cada crescimento é uma frente nova ganhando guardas —
o raster, o gradiente, o vidro, o `fill`. As execuções anteriores estão
registradas abaixo, com as suas datas, porque um número de mutação só quer dizer
alguma coisa junto do código que ele mordia.

### 2026-09-03: `gate-m1 passed`, 231 de 231, e desta vez num worktree

`[ART]` **A varredura inteira rodou de uma vez, e pela primeira vez sem tomar a
árvore de trabalho como refém.**

```
writer: 145 documents, 135 byte-exact against Apple's encoder
model:  145 of 145 fully understood, 1740 of 1740 specializations reachable
values: 61537 decoded, 0 failed
report: 1100 trees rendered, 0 fell back to raw JSON
svg:    149 of 149 read into geometry, 0 refused
sweep:  231 of 231 mutations caught

VERDICT: gate-m1 passed
```

**478 casos, 0 falhas. Zero sobreviventes.** E o fim foi limpo: zero restaurações
e zero divergência de hash no log, o marcador de varredura-em-progresso removido,
e nenhum arquivo mutado em disco.

**O que mudou não foi o gate, foi onde ele roda.** As 22 mutações novas são do
`fill` (doc 03 §30) e as duas tentativas anteriores de rodar a lista inteira
foram **mortas** — uma na mutação ~123, outra aos 6 minutos —, e cada morte
deixou um arquivo mutado na árvore em que se trabalha. `scripts/gate-worktree.ps1`
dá à varredura um checkout e um build próprios; a árvore principal ficou livre
durante as duas horas, e a documentação desta seção foi escrita e commitada nela
**enquanto a varredura corria**.

> **Duas ressalvas, ditas em vez de arredondadas.**
>
> A varredura testou o commit `6e46d1f`, ao qual o worktree se sincronizou no
> início. O commit `1fcc8e5`, que veio depois, é **só documentação** — nenhum
> arquivo que a varredura muta foi tocado —, então o veredito vale para o código
> destas duas revisões. Se ele tivesse tocado código, não valeria, e a régua seria
> a data e não o commit.
>
> A restauração manual **ainda foi necessária uma vez neste dia**, e foi a nona
> do projeto: a fatia que rodava na árvore principal antes do worktree existir
> teve de ser morta, e deixou o `IconBundle.cpp` com o filtro de `unusedAssets`
> forçado a `if (false)`. Backup conferido contra o `HEAD` antes de qualquer
> escrita. É a última ocorrência com essa causa: a partir daqui a varredura não
> roda mais onde se edita.

### 2026-09-02: `gate-m1 passed`, 209 de 209, numa execução única

`[ART]` **A varredura inteira rodou de uma vez e o script imprimiu a frase.**

```
writer: 145 documents, 135 byte-exact against Apple's encoder
model:  145 of 145 fully understood, 1740 of 1740 specializations reachable
values: 61537 decoded, 0 failed
report: 1100 trees rendered, 0 fell back to raw JSON
svg:    149 of 149 read into geometry, 0 refused
sweep:  209 of 209 mutations caught

VERDICT: gate-m1 passed
```

**417 casos, 0 falhas. Zero sobreviventes.** E o fim foi limpo, não só o veredito:
zero restaurações e zero divergência de hash no log, o marcador de
varredura-em-progresso removido, e nenhum arquivo mutado em disco — o que
aconteceu seis vezes ao longo do dia anterior.

> **Por que esta linha vale mais do que a de baixo.** A seção seguinte registra
> 160 de 160 em cinco fatias, e diz que a união das fatias **não é a mesma
> afirmação**. Era verdade e continua sendo. O que mudou é que agora existe a
> execução única, e é dela que a frase vem.

**A pré-checagem de âncora ganhou o dia antes de a varredura começar.** A primeira
tentativa parou em milissegundos: a mutação *"uma camada de vidro é desenhada em
vez de nomeada"* apontava para um `skip` que a ligação do vidro tinha acabado de
remover. O código andou e a mutação não. Foi substituída por duas que mordem o
comportamento novo.

### A varredura anterior: 160 de 160, em cinco fatias

`[ART]` **Em 2026-09-02 as 160 mutações foram aplicadas e todas foram pegas** —
40, 40, 20, 20, 20 e 20 —, com a suíte pristine conferida no início de cada
fatia.

**E mesmo assim esta linha não diz `gate-m1 passed`.** O script reserva essa
frase para uma execução única sobre a lista inteira, e nenhuma completou: a
varredura passou de uma hora e foi cortada **quatro vezes** nesta máquina. O
fatiamento existe por causa disso.

> **Por que a distinção é mantida em vez de arredondada.** A união das fatias
> cobre as mesmas 160 mutações e verifica a mesma suíte, e é evidência forte. Mas
> "todas as fatias passaram" e "a varredura passou" são afirmações diferentes, e
> o valor deste gate vem inteiramente de os números dele serem o que dizem ser. O
> script recusa a frase; esta documentação também.

**O custo de não ter isso:** a varredura cortada deixou arquivo mutado em disco
em **seis** ocasiões — `Png.cpp`, `PathBuffer.cpp`, `IconRenderer.cpp`,
`Inflate.cpp`, `PathComposite.glsl` e antes deles o `PathBuffer.cpp` do reboot.
Todos restaurados do backup conferido por SHA-256, e nenhum chegou a um commit.
É a rede que nasceu quando a máquina reiniciou, e ela pagou seis vezes.

> **As cinco que a varredura cobrou.** A primeira execução com as 16 mutações do
> raster devolveu **114 de 119**: complemento do bloco *stored*, tabela Huffman
> sobre-inscrita, empate do Paeth, comprimento contra o `IHDR` e `IEND` ausente —
> cinco guardas escritas sem o teste que as fizesse morder.
>
> A quinta é a instrutiva: o teste **existia** e media outra coisa. Ele truncava
> o arquivo em 40 bytes, o que levava o `IDAT` junto, então o erro vinha do zlib
> vazio e não da guarda do `IEND` — e como ele só checava "algum erro", passava
> verde provando nada. Um teste que não nomeia o motivo não distingue a guarda
> que ele acha que exerce da que realmente respondeu.

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
> **E ela reprova o TESTE, não só o código.** A rodada do conversor parou em
> **102 de 103**: *"o ajuste do viewBox esquece de centralizar"* sobreviveu. Os
> três casos escritos para esse ajuste — quadrado, largo, e com origem deslocada
> — têm todos o eixo x preenchendo o alvo, então o termo de centralização
> horizontal vale zero nos três, e jogá-lo fora inteiro deixava a suíte verde. O
> mesmo formato do ponto cego do doc 03 §8. Reparado com um viewBox alto, e o
> RED conferido à mão antes de aceitar.
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

**Duas exceções, em `scripts/`:** `thinlit.py` e `ctormap.py` leem um Mach-O
**fino e solto** — o `IconRendering.framework` vem dentro do bundle do Icon
Composer, não do shared cache que os instrumentos da caixa compartilhada
assumem. Ficam aqui porque o input é deste projeto (doc 03 §19.7).

O pipeline do DMG ao bundle está em `References/README.md`.
