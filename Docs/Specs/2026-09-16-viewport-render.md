# Render de viewport — desenho

2026-09-16. Por que o zoom do canvas hoje perde resolução, o que precisa mudar
para que ele deixe de perder, e qual medida decide se a mudança está certa.

## O problema, medido

O zoom do canvas **não renderiza — ele amplia**. Três leituras o estabelecem:

- `RenderCoordinator.cpp:12` monta a chave do render como
  `{version, context, size}`. Zoom não está nela, então mexer na roda não pede
  render nenhum.
- `size` só assume 512 ou 1024 (`PanelCanvas.cpp:210-211`, `MenuBar.cpp:144-145`).
- O canvas desenha com `AddImage` sobre um lado de `sidePx * zoom`
  (`PanelCanvas.cpp:433`), e o zoom vai até 16× (`Panels.h:136`).

Em 400% o que aparece é uma textura de 512 esticada sobre 2048 pixels de tela:
cada texel vira um bloco de 4×4, filtrado. A borda fica mole por construção, e
qualquer resíduo de um pixel do render vira um borrão de quatro.

Isto não é uma limitação do renderizador. A pipeline é resolution-independent de
verdade — a arte é vetor, o campo de distância é gerado na resolução de destino,
e os parâmetros do alvo são unidades de canvas multiplicadas por pixels por
ponto. Renderizar a 2048 dá 2048 de detalhe REAL. O que falta é o Kit saber
pedir isso.

## O que muda no contrato

`IconRenderOptions::size` significa três coisas ao mesmo tempo hoje, e é a
confusão entre elas que trava tudo:

1. a **resolução do canvas** — quantos pixels valem os 1024 pontos de canvas, e
   portanto a escala de todo parâmetro em unidades de canvas;
2. a **dimensão do buffer** de saída;
3. a **origem**, implicitamente `(0,0)`.

O desenho separa os três. `size` fica sendo apenas (1), e entram origem e
extensão de saída:

```c++
struct IconViewport {
    // Em pixels da grade que `size` define. O padrão é o canvas inteiro, e
    // nesse caso todo sítio abaixo se reduz à aritmética de hoje.
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::uint32_t width = 0;    // 0 == `size`
    std::uint32_t height = 0;   // 0 == `size`
};
```

Zoom de 400% sobre um canvas de 512 vira `size = 2048` com um viewport da região
visível. O custo passa a ser a área do viewport e não `size²`.

## O invariante que governa o desenho

> Um render de viewport é IGUAL ao recorte correspondente de um render cheio na
> mesma escala, pixel a pixel.

Esta é a única afirmação que o resto da spec precisa provar, e ela é a razão de
o desenho ser viável de gatear: qualquer sítio que esqueça a origem, qualquer
efeito cuja margem seja curta demais, qualquer gradiente medido no retângulo
errado aparece como diferença contra o render cheio. A pergunta "a margem é
suficiente?" deixa de ser uma aposta e vira uma medição.

O gate é um caso de teste que renderiza o mesmo documento duas vezes — cheio a
`size`, e em viewports que cobrem quadrantes sobrepostos — e compara
`RenderedIcon::rgba` float a float. A tolerância é ZERO.

Os viewports não são escolhidos a esmo; cada um existe para derrubar uma
hipótese específica:

- um que cruza a borda da forma (campo, especular, translucidez);
- um encostado na borda do canvas (a interseção com o canvas, abaixo);
- um com origem que NÃO é múltipla de 4 (o alinhamento da escada do desfoque,
  abaixo);
- um logo abaixo da forma, onde só a sombra deslocada chega.

Os documentos saem do corpus pelos contadores que o próprio render já devolve
(`glassShadowed`, `glassRefracted`, `glassTranslucent`, `glassSpecular`,
`backgroundPainted`, e um com arte raster), numa `size` em que a escada do
desfoque reduz de verdade.

**A exatidão vem de amostrar em coordenada ABSOLUTA.** Todo passo de CPU
(fundo, pastilha, raster, campo, máscara de translucidez, refração) continua
avaliando no ponto `(x + origem) + 0,5` da grade de `size`, com a geometria
intocada. Só o ÍNDICE no buffer é deslocado, por um inteiro. Transladar a
geometria arredondaria diferente, e o gate acusaria. Os passos que só olham
vizinhança (desfoque, translação da sombra, especular) são invariantes por
construção, desde que a escada esteja alinhada.

**Quando o diferencial não for zero, o relatório diz de que tipo ele é.** A
arte vetor passa pela GPU (`Device.h`). Lá a translação é inevitável: o
`PathGlobals` recebe a origem subtraída em `m2`, e `twoOverSize` passa a usar a
extensão do buffer. Uma coordenada transladada não tem garantia de arredondar
igual à original, e este é o único sítio onde o risco existe. Então o gate imprime o `max |Δ|` e a
distância do pior pixel até a borda do buffer. Uma margem curta aparece com Δ
grande e colada na borda. Isto vale para uma banda que DECAI com a distância
(desfoque, sombra, campo, especular). A refração não decai — ela desloca a
leitura em `|strength| × pixels-por-ponto`, um valor FIXO —, então uma margem
curta nela produz um PLATÔ de Δ que alcança até essa distância inteira da
borda, não um pico colado nela (medido em 18/09, abaixo: "A margem, e de onde
vem o número"). Resíduo de aritmética aparece com Δ na ordem de ULP,
espalhado pelas bordas antialiasadas. Só o primeiro caso é o desenho errado. O
segundo pede que a translação seja feita de outro jeito (subtração inteira
depois da colocação, e não dobrada dentro da matriz), e a tolerância continua
sendo zero.

## A margem, e de onde vem o número

Um pixel dentro do viewport pode depender de geometria fora dele. Os alcances
são conhecidos e medidos, em unidades de canvas (o canvas tem 1024 —
`IconRenderer.h`):

> **Correção de 16/09 (revisão).** A primeira versão desta tabela dava 64 para
> os desfoques e fechava a margem em 128. As duas coisas estavam erradas. O
> "raio" é o **sigma** da gaussiana (`BlurKernel.h`), e o kernel vai até
> `ceil(2,8 × sigma)` (`blurKernelHalfWidth`, `BlurKernel.cpp:44`). Além disso,
> a refração lê o backdrop em coordenada deslocada, e ela nem aparecia na
> tabela. As linhas abaixo são as corrigidas.

| efeito | alcance em pontos de canvas | sítio |
| --- | --- | --- |
| sombra: desfoque | `2,8 × 64 × clamp(radius)`, teto da fórmula **179,2** (só alcançável com um `ShadowParameters` não-default) | `GlassShadow.cpp:164`, `BlurKernel.cpp:44` |
| sombra: deslocamento | **32** para baixo, somado ao de cima | `GlassShadow.h:274`, `GlassShadow.cpp:277` |
| sombra: anel | **16** | `GlassShadow.h` (`Shadow.ringWidth`) |
| refração: deslocamento | `|refractionStrength|`, teto **640** | `GlassMaterial.h` (`refractionStrengthMax`), `GlassLayer.cpp:146` |
| refração: banda do campo | `refractionHeight`, teto **256** | `GlassMaterial.h` (`refractionHeightMax`) |
| especular: banda do campo | `inset + height`, ~**24** | `GlassSpecular.h` |
| pastilha: realces | `inset + height ≤` **40**, de `distance` no máximo (os dois slots difusos), com `inset = 0` fixo nos sete | `ChicletHighlights.cpp:27-40,60,65`, `GlassSpecular.cpp:137-153` |
| desfoque do material (desenho DESLIGADO hoje) | `2,8 × min(b,1) × 64`, máx **179,2**, +1 px de outset | `BlurKernel.h`, `IconRenderer.cpp:828` |

`[BIN]` **A sombra estava superestimada em 3,33×.** A linha acima é o teto da
FÓRMULA, e ele só é alcançável se algum sítio passar um `ShadowParameters`
diferente do default a `sizeBasedValue(p.radius, sizeClass)`
(`GlassShadow.cpp:162`). Nenhum passa: `radius` é `SizeBasedValue{{0.3, 0.3,
0.3, 0.3}}` com os quatro slots IGUAIS (`GlassShadow.h:285`, então a classe de
tamanho não move nada), `blurStrengthMax = 64,0` (`GlassMaterial.h:237`) é
igualmente sempre o default, `ShadowParameters` é sempre `kShadow`, e o grupo
do documento só alcança `kind` e `opacity` (`Coverage.cpp:50`, `Values.h:67-70`
— não existe uma chave `legacy-*` de sombra). O alcance ALCANÇÁVEL é portanto
`2,8 × (64 × 0,3) = 2,8 × 19,2 =` **53,76** pontos, e `GlassShadow.h:442` já diz
isto em palavras: "the shadow sits 32 px down under a 19.2 px blur radius".

**A margem é calculada por documento, não é uma constante.** O pior caso
teórico é 640 pontos, mais de meio canvas. Com uma constante desse tamanho, o
viewport vira o render cheio em todo ícone. A margem então sai dos parâmetros
já denormalizados dos grupos que de fato desenham cada efeito.

**Os alcances se SOMAM ao longo da cadeia.** Um passo só lê pixels certos se o
passo anterior os deixou certos. A sombra de um grupo é exata até
`2,8 × σ + deslocamento + anel` da borda do buffer. A refração de um grupo seguinte lê
esse backdrop até `|strength|` de distância. Então um pixel do recorte só sai
exato com margem ≥ os dois somados. Por lado, a margem é:

- a maior banda local (sombra `2,8 × σ + deslocamento + anel`, especular e
  translucidez), **mais**
- a SOMA de `max(|strength|, height)` sobre os grupos que refratam, porque cada
  refração lê o resultado da anterior.

`[ART]` **Medido em 18/09.** Em `size = 2048`, a margem calculada é **222** px
para `Jellify-Music` (vetor, sem refração) e **897** px para `swmpc` (o
documento que mais refrata do corpus, força −0,527), e o gate fecha em zero com
ela nos dois, nos quatro viewports. Com a metade — um fator `IC_MARGIN_SCALE`
acrescentado TEMPORARIAMENTE a `planViewport` só para esta medição, e removido
logo depois (111 px e 448 px) —, `swmpc` FALHA em 2 dos 4 viewports: 238 e 1371
pixels diferentes, `max |Δ|` 0,0536 e 0,184, a 606 e 481 px da borda interna do
buffer. Esta NÃO é a assinatura "Δ colado na borda" que a seção anterior
descreve para uma banda que decai — é o PLATÔ que a mesma seção agora qualifica
para a refração: `|strength| × pixels-por-ponto` alcança 674,56 px nesta size
(337,3 pontos × `k = 2`), e 606 e 481 caem dentro dele, não além. A margem
cheia não é folga para este documento: é o que o invariante exige.
`Jellify-Music` continua em zero. Isto NÃO prova que a margem dela está duas
vezes maior do que precisa: um "zero" é fraco sobre região plana, porque o
grampo da borda do buffer substitui ali exatamente o valor que o render cheio
também leria (o mesmo aviso que `diffAgainstCrop` carrega desde a Task 4). A
conta está justa para `swmpc`; para `Jellify-Music`, que DESENHA sombra (o
`alinhamento 8` que o caso imprime só acontece com `sigmaPixels > 0` —
`blurLadderAlignment` devolve 1 no caso contrário, `ViewportPlan.cpp:62-75` —
então a escada de fato reduz nela) mas não refração, esta medição não decide
se há folga: a banda da sombra DECAI com a distância e a leitura acima já
qualifica esse caso como "colado na borda", não "platô", e a hipótese mais
provável continua sendo a região plana perto da borda, já apontada acima — só
que, se houver folga, não é neste documento que ela aparece.

O arredondamento vai para cima, em pixels, com uma folga de duas vezes o
alinhamento da escada (abaixo): a redução e a expansão bilinear alcançam alguns
pixels além do kernel.
`[ART]` **Medido em 16/09.** Só 3 dos 146 documentos do corpus têm
`refractivity`, e só **2** deles com força diferente de zero:

| documento | força | alcance em pontos |
| --- | --- | --- |
| `CamilleScholtz__swmpc__swmpc` | −0,527 | **337,3** |
| `StikDebug__StikPair__StikPair` | −0,359 | 229,9 |

Nos dois, a altura denormalizada (134,5 e 105,3) fica abaixo da força. O
terceiro, `videolan__vlc-ios__VLC26`, tem `enabled: false` e força 0, ou seja,
identidade. Então, em 144 dos 146 documentos, quem manda na margem é a sombra
— 101,76 pontos, CONSTANTE entre documentos, porque `radius` é sempre o mesmo
default ("A sombra estava superestimada", acima; o valor usado aqui já é o
corrigido, não o teto da fórmula). Nos dois que refratam, a soma leva a margem
a até 337 + 101,76 ≈ 439 pontos por lado, quase 43% do canvas de 1024. Nesses
dois o viewport
praticamente vira o render cheio até zooms altos, onde o teto de área assume.
Isso está aceito: são 2 documentos, e o resultado continua exato. A varredura é um passeio
Python pelas chaves `refractivity` de `References/corpus/*/icon.json`, com a
mesma denormalização de `GlassMaterial.cpp:208-234`.

**O buffer é `(viewport ⊕ margem) ∩ canvas`.** Não basta estender o viewport. O
desfoque (`blurPass`), o `reduceBox`, o `expandBilinear` e a amostragem da
refração (`DisplacementOracle.cpp:162`) **grampeiam na borda do buffer**. No
render cheio, essa borda é a borda do canvas. Um buffer que passe do canvas
grampeia num lugar em que o render cheio não grampeia. Por isso as duas bordas
precisam coincidir sempre que o viewport encosta no canvas. A interseção tem
mais uma vantagem: ela mantém a origem do buffer ≥ 0, e isso deixa a subtração
da origem exata em ponto flutuante.

**A origem do buffer é alinhada à escada do desfoque.** Um sigma grande não é
pago com um kernel largo. `blurLadder` (`BlurKernel.cpp:200`) reduz a imagem por
2 ou 4, recursivamente, com caixas que começam na origem do BUFFER. Com uma
origem fora da grade, as caixas são outras e o resultado muda em todo pixel, não
só na borda. A origem então é arredondada para baixo até um múltiplo do produto
dos fatores de redução que a escada vai usar para o maior sigma do documento.
Esse produto sai de `blurReduceFactorForVariance`, aplicado nível a nível, e não
de uma estimativa. A condição `kBlurMinReducedSide` também depende das
dimensões do buffer. Com o lado mínimo de 8, ela não muda nada em buffers reais,
mas fica anotada aqui.

`[OBS]` O campo de distância é o único passo cujo alcance não é uma constante: a
distância de um pixel fundo dentro da forma pode medir até a borda mais distante
do canvas. O que salva é que nenhum consumidor lê o campo além da sua própria
banda — o especular clampa em `inset + height`, a sombra em `ringWidth`, a
refração em `refractionHeight` (até 256, por isso ela entra na margem acima), a
translucidez na própria rampa (a leitura dessa banda está em
`GlassTranslucency.h:206` e é a primeira coisa a conferir se o gate acusar).

`[OBS]` **Se o desfoque do gerador for ligado, a margem muda.**
`Docs/Laudos/2026-09-16-coreui-gerador-de-sdf.md` leu no gerador do alvo um
desfoque gaussiano do campo, com `σ = min(0,005 × min(W,H), 3,5√2)` px. Ele foi
medido como neutro e ficou DESLIGADO. Ligado, ele seria mais um passo de
vizinhança sobre o campo: ~`2,8 × σ` px a mais na banda local, e mais uma
escada de desfoque cuja origem precisa estar alinhada. A grade do alvo também
tem uma moldura de um texel (`W+2`, `H+2`). Nada disso muda o desenho, só a
conta de `documentReach`, e o gate acusa se alguém ligar o desfoque sem ajustar
a conta. O invariante é o que prova isso; se ele acusar
diferença longe da borda, esta hipótese caiu e o campo precisa de tratamento
próprio.

`[OBS]` **O `<filter>` de SVG é RECUSADO num buffer parcial, não margeado.**
`applySvgFilter` grampeia o desfoque de `feGaussianBlur` na borda do BUFFER
(`SvgFilter.cpp`), do mesmo jeito que os outros passos — mas o alcance dele não
sai de um parâmetro do documento: sai do `stdDeviation` de cada primitiva, isto
é, do CONTEÚDO do SVG. `documentReach` não enxerga isso: ela percorre os
grupos do documento e nunca abre a arte. Dar a ele uma margem exigiria abrir
cada SVG referenciado dentro de `documentReach`, o que é tarefa própria — então,
em vez de desenhar errado, `SvgRenderer.cpp` (`isPartialTarget`) recusa por
NOME todo `<filter>` num alvo parcial, do mesmo jeito que o vidro foi recusado
enquanto não convertido — a mesma disciplina de "O invariante que governa o
desenho", acima. A recusa
aparece em `RenderedIcon::shapeGaps`, com o motivo por escrito; o gate
(`viewport_refuses_an_svg_filter_by_name_instead_of_clamping_it`) procura essa
recusa por nome em vez de comparar pixel. `[ART]` **Medido em 18/09**
(`grep -rl` sobre `References/corpus/**/*.svg`): a recusa é por NOME de
`<filter>` (`SvgRenderer.cpp:371`), e é esse o predicado que decide a
exposição dela — **9** SVGs do corpus têm `<filter>`, em três documentos
(`Bunn__PiStats__pistats`, `PDF-Archiver__PDF-Archiver__AppIcon`,
`rileytestut__Delta__MicrochipIcon`); **5**, todos dentro dos mesmos três
documentos, têm `feGaussianBlur` especificamente. `[INF]` Que os nove seriam
recusados do mesmo jeito é inferência da leitura do predicado, não uma medição
de execução: nenhum SVG do corpus passou de fato por este caminho em nenhuma
execução até aqui — o caso do gate usa `kFilteredDocument`, sintético, não um
asset do corpus.

## O teto de área

Todo passo roda na resolução da tela, margem cheia — não há efeito calculado na
resolução base e amostrado para dentro. Em troca existe um teto: se
`(viewport + margem)` passar de **16 Mpx**, o refino não acontece e o canvas
volta a esticar a textura da resolução base.

O teto é explícito porque a margem escala com o zoom e o retângulo visível não.
Com o canvas de 512 em 1600% (`size = 8192`, 8 px por ponto), os 211,2 pontos
da fórmula (179,2 do desfoque + 32 do deslocamento — o teto, não o alcançável)
virariam ~1690 pixels por lado; com o `radius` que o documento de fato alcança
(acima, "A sombra estava superestimada"), o número é 85,76 pontos (53,76 + 32)
sem contar o anel, ou **101,76** contando-o (+16), o que dá ~814 pixels por
lado em 8192 — e o buffer ainda é cortado pelo canvas de qualquer forma. O
ladrilho é constante; a margem não é. Consequência: o teto dispara justamente
nos zooms mais altos e nos documentos com refração forte, e é lá que o canvas
volta a esticar. Isso está aceito, e o motivo é este.

**O teto olha o VIEWPORT PEDIDO, e não o buffer planejado.** A expressão é
`!crop.isFull() && buffer.texels() > kViewportAreaCap` (`ViewportPlan.cpp`), e
os dois lados dela medem coisas diferentes de propósito. A ÁREA que pesa é a
do buffer, porque é o buffer que é alocado e percorrido. Mas quem tem direito
à ISENÇÃO é o recorte, porque a isenção existe para um caso só: **quem pediu o
canvas inteiro não tem para onde cair.** A queda de um ladrilho recusado é
justamente o canvas inteiro na resolução base (`RenderRequest::fallbackSize`,
nunca zero), então recusar também esse pedido deixaria a queda sem destino.
`crop` é o pedido; `buffer` é a consequência do pedido mais a margem — e só o
primeiro sabe o que o chamador quis.

O que vem a seguir tem duas metades com marcações diferentes, de propósito: o
que foi EXECUTADO aqui é `[ART]`; o alcance do defeito pela UI e o custo de
memória dele são DERIVADOS de leitura de código e de aritmética, nunca
executados, e são `[INF]`.

`[ART]` **A primeira versão testava `!buffer.isFull()`, e por isso o teto não
disparava exatamente no caso que ele existe para cobrir.** Corrigido em 18/09.
Um LADRILHO cuja margem cresce até `(viewport ⊕ margem) ∩ canvas` cobrir o
canvas inteiro responde "sim" a `buffer.isFull()` sem nunca ter pedido o
canvas: ficava indistinguível de um render cheio e escapava do teto por
inteiro. `viewport_plan_refuses_a_tile_whose_margin_saturates_the_canvas`
(`Tests/test_viewport_render.cpp`) prende o caso, e foi VISTO falhando contra
a linha antiga — `overCap` saindo `false` onde tem de ser `true` — antes de a
nova entrar. E o gate do corpus a 2048 px foi RODADO com a troca no lugar e
não se moveu: o buffer de `swmpc` ali é 2048×2048 = 4,19 Mpx, abaixo do teto,
que portanto nem chega a ser consultado — exatamente a forma "buffer cheio,
recorte não cheio" que a correção passa a enxergar, e que continua aceita.

`[INF]` **O alcance e o custo do defeito, derivados e não executados.** É
alcançável pela UI, nos números desta mesma seção: com refração forte a margem
é ~439 pontos por lado, o buffer satura assim que a área visível do canvas
passa de `0,143 × size`, e em `size = 8192` (a base de 512 a 1600%) isso são
1168 px por eixo — uma janela maximizada num monitor 1440p, sobre
`CamilleScholtz__swmpc__swmpc`, que está no corpus e é o pior caso desta spec.
O que viria em seguida é um render de 8192×8192: `acc` sozinho seria 1,07 GB e
o armazenamento de float no pico da ordem de 7 GB; e a lambda do job
(`OnyxPorts.cpp:145`) não tem `try`/`catch`, de modo que um `bad_alloc`
escapando deixaria `working_` verdadeiro e `JobScheduler::~JobScheduler`
esperando para sempre. Nada disso foi executado — nenhum render de 8192 foi
tentado nesta máquina; os números saem da aritmética de `texels × 4 × 4 B` e
da leitura de `OnyxPorts.cpp`. Na mesma classe está a prova de que a troca é
segura: `crop.isFull()` implica `buffer.isFull()` (a margem sobre o canvas
inteiro volta ao canvas inteiro), logo ela só pode recusar MAIS do que a linha
antiga, nunca menos. Com `crop`, o mesmo pedido cai para a base — que é o que
o parágrafo acima sempre disse que aconteceria nos zooms altos com refração
forte.

## O que o Kit faz

A chave do `RenderCoordinator` ganha o viewport, e o canvas passa a mostrar
**apenas o ladrilho corrente** — não há composição com o render cheio por baixo.

O custo disto está aceito e registrado aqui para que ninguém o redescubra como
defeito: **cada pan e cada zoom tem um intervalo sem imagem nítida**, e em um
ícone pesado esse intervalo é longo. O `RenderedIcon` já carrega `pending` e
`pendingSeconds`, e o painel já os mostra; é por ali que o intervalo se explica
ao usuário.

O retângulo visível sai do que `PanelCanvas` já calcula: `st.painted =
canvasIntersect(st.image, st.clip)` (`PanelCanvas.cpp:419`), levado de volta ao
espaço da imagem. A `size` pedida é `round(view.size × zoom)`, e o renderizador
aceita qualquer valor diferente de zero (`IconRenderer.cpp:589`). A restrição a
512/1024 é só dos seletores de UI. Com zoom ≤ 1 nada muda: vale o caminho de
hoje.

Três coisas que a primeira versão deixava implícitas:

- **O desenho usa o retângulo do ladrilho, e não `view.width × zoom`.** O
  `AddImage` de `PanelCanvas.cpp:433` assume que a textura é o canvas inteiro.
  Com o ladrilho, o `RenderedIcon` precisa trazer a origem, a extensão e a
  `size` com que foi pedido, e o painel posiciona a textura por esses números.
- **O que aparece durante a espera.** O ladrilho velho continua sendo desenhado
  no retângulo que é dele, no espaço da imagem. Por estar ancorado na imagem,
  ele acompanha o pan corretamente. Fora dele fica o fundo do painel. Um zoom
  estica o ladrilho velho até o novo chegar.
- **Não há pedido por quadro.** Durante um arrasto, a chave mudaria a cada
  quadro. O pedido sai quando o pan e o zoom ficam parados por um intervalo
  curto. Sem isso, o agendador só descarta trabalho, e o intervalo sem imagem
  nítida vira o arrasto inteiro.

## Os sítios

> **Correção de 16/09 (revisão).** A lista original tinha 15 sítios e errava
> para os dois lados. Incluía `glassRefractionFor` (745) e `shadowGeometry`
> (1428), que usam `size` só como escala (`GlassLayer.cpp:143`,
> `GlassShadow.cpp:152`) e não mudam. E deixava de fora quatro que mudam. A
> lista abaixo é a corrigida, contada contra o HEAD `fa21d98`.

`options.size` aparece 32 vezes em `IconRenderer.cpp`. Os que carregam extensão
ou origem, e portanto mudam:

- a extensão do próprio buffer (592-593);
- o retângulo do canvas que o gradiente do fundo mede (626-627). Esse fica
  COMO ESTÁ: o fundo é pintado na CPU em coordenada absoluta, então o retângulo
  absoluto é o certo. Está na lista para que ninguém o "corrija";
- `paintBackground` (633), `clipToChiclet` (639) e `drawChicletHighlights`
  (669);
- `placeRaster` (1160);
- `artPlacementRect` (1170, 1372) e `rasterPlacementRect` (1373);
- `placeOnCanvas` (1273, 1468) e `ro.width/height` (1464);
- `generateFieldFromContours` (1301) e `generateFieldFromAlpha` (1317);
- `glassOver` (1351);
- `shadowImage` (1429) e `shadowOverdrawImage` (1437-1438).

**Uma armadilha que mistura os dois papéis.** Em `blurMaterialSurface` (829), a
extensão entra e a escala é tirada dela: `min(w,h) / 1024`
(`BlurKernel.cpp:279`). Hoje isso está certo porque a extensão é igual a
`size`. Um buffer de viewport, passado ali, muda a escala sem dar nenhum erro.

**Sítios que ficam FORA deste arquivo e também mudam:**

- `glassOver` (`GlassLayer.cpp:206`) passa a avaliar em coordenada absoluta,
  com UV sobre o canvas;
- o `SampledImage` de `sampleBilinear` (`DisplacementOracle.cpp:152`) ganha a
  origem e a extensão do buffer. O grampo continua sendo o do canvas, e a
  leitura é deslocada por um inteiro;
- `rasteriseContours` e `coverageFromContours` (`DistanceField.cpp:778, 867`)
  passam a amostrar linhas absolutas, via uma origem em `FieldOptions`, e
  `FieldImage` passa a carregar essa origem;
- `glassOpacityMask` (`GlassTranslucency.cpp:96`) passa a medir `py` absoluto;
- `chicletCoverage` e `drawChicletHighlights` passam a varrer só o buffer;
- o alinhamento da escada do desfoque (acima).

**A lista é conferida pelo compilador, e não por número de linha.** Número de
linha escorrega no primeiro commit. As funções que hoje recebem
`(size, size)` passam a receber uma única struct de grade, com escala, origem e
extensão. Cada chamada antiga deixa de compilar, e é o compilador que enumera
os sítios. Os que usam `size` só como escala continuam recebendo o escalar.
Conseguir separar os dois grupos continua sendo o argumento de que isto é
refatoração dirigida, e não varredura cega.

## O que isto NÃO compra

Nitidez no zoom. **Não** aproximação da Apple.

`IconRenderer.cpp:443-455` registra uma medição anterior: `kFieldSuperSample`
foi testado contra o gabarito e **o gabarito arbitrou contra** — campo mais
preciso não ficou mais parecido com o alvo. As duas correções do campo de
15/09 concordam com ela: moveram o erro médio por canal contra
`apple-512.png` de 8,93/10,26/9,89 para 8,85/10,18/9,85, ou seja, quase nada.

O que separa este renderizador da Apple em nível de cor está anotado `[OBS]` em
outros sítios: o blur-material desligado, o grampo `plusLighter` desligado e o
overdraw da sombra. Um viewport não toca em nenhum dos três.

## Ordem de execução

1. `IconViewport` no contrato, com o padrão que reduz à aritmética de hoje, e o
   gate do invariante escrito ANTES de qualquer sítio mudar. Ele tem que passar
   trivialmente no padrão e falhar em qualquer viewport não trivial, e o motivo
   da falha precisa ser o recorte, não uma dimensão de buffer que não bate.
2. A struct de grade, com o compilador enumerando os sítios.
3. Os sítios, em grupos que o gate consegue julgar um a um: fundo e chiclet;
   colocação de arte; campo e vidro; sombra.
4. A margem por documento, a interseção com o canvas e o alinhamento da escada.
5. O teto de área.
6. O Kit: chave, retângulo do ladrilho, pedido só com o pan parado, e o estado
   de espera que já existe.

## Estado em 2026-09-18

Os oito passos da "Ordem de execução" acima landaram, um a um, entre
`e45b301` (HEAD antes desta frente) e `03a043f`:

| passo | commit(s) | o que mudou |
| --- | --- | --- |
| 1 | `be77824` | `IconViewport` no contrato, `RenderedIcon` ecoando a grade, e o gate do invariante escrito ANTES de qualquer sítio mudar (visto falhar por desconto de origem, de propósito). |
| 2 | `1e9ec1a` | O plano do buffer: margem por documento com a cadeia de refrações somada, interseção com o canvas, origem alinhada aos fatores da escada do desfoque, teto de 16 Mpx. |
| 3 | `0a6463e`, `926fe94`, `bc89160` | Os sítios de CPU (fundo, pastilha, raster, SVG) passam a amostrar em coordenada absoluta; a translação da arte vetor sai da matriz e vira offset de viewport do Vulkan (abaixo, "O resíduo de GPU"); a fatia SVG que o primeiro commit do passo dava por convertida tinha o traço e a região de máscara ainda na grade errada, corrigido no terceiro. |
| 4 | `7ad503e`, `50bec5b` | O campo, a translucidez e a refração passam a ler o buffer em coordenada absoluta; o gate roda pela primeira vez com a origem HORIZONTAL do buffer de fato não-nula, expondo (e corrigindo) um `"position": {"scale": 2}` que não tinha efeito nenhum sem `translation-in-points`. |
| 5 | `3ca664a`, `de43f56` | A sombra volta a andar num buffer parcial (`shadowBlocked` cai); o gate ganha uma fixture em que a escada do desfoque de fato reduz, e a asserção de `blurLadderAlignment` passa a depender dela, não de um literal solto. |
| 6 | `73ea8bd`, `b92b19e` | O gate fecha em zero sobre o corpus a 2048 px, a margem medida entra na spec, e três dívidas de precisão da tabela de margem são pagas (sombra superestimada em 3,33×, a banda da pastilha, a afirmação falsa sobre a sombra de `Jellify-Music`). |
| 7 | `8e7319a`, `f971028` | Pedido e resultado passam a carregar o ladrilho; a queda para a base cobre também o teto de APARELHO (`CoveragePass::draw`, erro, não recusa); a nota do teto de área deixa de desaparecer atrás de um fallback bem-sucedido. |
| 8 | `03a043f` | O Kit: o canvas pede o ladrilho quando o pan e o zoom param por 150 ms, o coordenador ganha o ladrilho na chave, a régua vira a `size` base, e a textura é posta no retângulo que ela cobre. |
| revisão | `789ce9b`, `fc979d1`, e a correção documental **deste mesmo commit** | A revisão da branch inteira achou uma Critical e três Important. O teto de área passa a olhar o viewport PEDIDO (`crop`) e não o buffer planejado; o assentamento do canvas passa a comparar também o ladrilho deste quadro; `renderSvg` enquadra pela projeção; e `planViewport`, que não tinha teste nenhum, ganha cinco casos — o do ladrilho que satura o buffer foi VISTO falhando contra a linha antiga. Abaixo, "O plano copiado sem ser examinado". |

A margem medida (Task 6) já está registrada em "A margem, e de onde vem o
número", acima, e não é repetida aqui: **897 px** para `swmpc` (o documento que
mais refrata do corpus) e **222 px** para `Jellify-Music`, os dois em
`size = 2048`, gate em zero nos quatro viewports de cada. A trava
(`CHECK_EQ(marginPixels, ...)` em `Tests/test_viewport_render.cpp`) veio no
mesmo passo, para que uma margem futura mais larga não passe pelo gate (que só
confere zero diferenças) em silêncio.

### O resíduo de GPU na Task 3, e o que foi decidido

O plano original pedia a origem do buffer dobrada dentro de `m2`, com
`twoOverSize`/`urx` sobre a extensão do buffer em vez do canvas. `[ART]`
Medido: isso deixava 11 e 15 pixels da borda antialiasada do disco do gate
diferindo por exatamente `0,00048828125` — `2⁻¹¹`, **um ULP de `float16`**
(o anexo de cobertura é `VK_FORMAT_R16G16_SFLOAT`, `Image.cpp:115`). Um
viewport de origem zero e extensão menor fechava em zero na mesma medida, o
que isolou a subtração da origem como a única causa — não a extensão.

O remédio já estava escrito na própria spec ("subtração inteira depois da
colocação, e não dobrada dentro da matriz"), e foi aplicado como um **offset
de viewport do Vulkan**: `viewport.x = -originX`, a projeção segue sobre o
canvas inteiro, e o scissor fica no buffer (`CoveragePass::draw`). **E a
sondagem achou um segundo termo que o diagnóstico não tinha previsto:** o
offset sozinho, sem mais nada, piorou o caso — 16080 pixels diferentes,
`max |Δ| = 0,847` —, porque `path_exterior.frag` lê as varyings da aresta em
coordenada de MUNDO enquanto `gl_FragCoord` já tinha virado `mundo − origem`:
o fragmento estava comparando duas grades. O conserto foi o fragmento somar a
mesma origem de volta (`IconRenderer.cpp:608-612`) — **somar um inteiro a um
`x.5` é exato, que é justamente o que subtrair dentro de `m2` não era.**

Consequência de forma, não só de correção: sem a origem dentro da matriz,
`placeOnCanvas` e `artPlacementRect` voltam a usar `size` só como escala
(a origem nunca entra ali), e por isso a assimetria `grid`/`canvasGrid` que o
plano original previa **não existe** — os retângulos de colocação são
absolutos em todo lugar, sem um segundo sistema de coordenadas para a arte
vetor. Quem vier do plano vai esperar o contrário; não é o que está no código.

### O que o teste à mão da Task 8 mostrou

O passo 6 da Task 8 pede uma passagem interativa: zoom a 400% pela roda,
arrastar, voltar a 100%. **Isso não foi feito. Ninguém dirigiu a GUI.**

O que foi verificado programaticamente, sem janela, dirigindo `drawCanvas` de
verdade nos testes do Kit: um único ladrilho é pedido depois que o ease
assenta, e nenhum durante um arrasto de dez quadros
(`canvas_and_coordinator_ask_for_one_tile_and_place_the_answer`); o ladrilho
velho continua ancorado na imagem durante esse arrasto, lido do quadrilátero
que o ImGui de fato emitiu, não de um número calculado à parte
(`iconQuad`, mesmo caso, e `canvas_places_a_tile_at_its_own_origin_and_does_not_move_the_icon`);
e zoom ≤ 1 volta ao canvas inteiro na base
(`canvas_writes_the_tile_only_after_the_pan_settles`, e o fim do laço do caso
acima). O binário real foi aberto num aparelho de verdade com uma captura em
120%: a barra mostra `512 px` / `120%`, sem a faixa "rendering…" ao lado do
Fit, e em 120% o pedido corrente é um ladrilho de grade **614** — a ausência
do indicador de pendência é o sinal de que esse ladrilho foi pedido,
renderizado, ecoado, casado com a chave e posto no retângulo certo.

A passagem interativa — os olhos vendo a roda subir a 400%, a borda ficar mais
nítida, o "rendering…" aparecer e sumir — continua em aberto. Nenhum teste
aqui compara nitidez subjetiva; o gate só prova igualdade com o recorte.

### O plano copiado sem ser examinado, e o contraexemplo

Duas das quatro correções da revisão de branch de 18/09 — a expressão do teto
de área e o predicado de assentamento — **estavam literalmente no plano**
(`Docs/Plans/2026-09-16-viewport-render.md`, linhas 593 e 1568) e chegaram ao
código sem que ninguém as examinasse. Isso não foi escorregão de quem
construiu: foi o pensamento do plano, transcrito. Um plano é uma hipótese
escrita antes de o código existir, e as duas linhas só ficam visivelmente
erradas depois de se saber o que `PixelGrid::isFull` passou a distinguir e de
onde `st.painted` passa a vir — coisas que o plano não tinha como saber e que
o passo que digitou a linha já sabia. Copiar uma linha do plano é, portanto,
aceitar uma hipótese sem a medir.

O contraexemplo está duas subseções acima, e é ele que dá a medida: na Task 3
o plano pedia a origem do buffer dobrada dentro de `m2`; isso foi MEDIDO
(11 e 15 pixels da borda antialiasada a um ULP de `float16`), o plano foi
DESOBEDECIDO, e o resultado — o offset de viewport do Vulkan, com o fragmento
somando a origem de volta — é melhor do que o que estava escrito, e ainda
apagou uma assimetria `grid`/`canvasGrid` que o plano previa. A diferença
entre os dois casos não é a qualidade do plano. É se alguém mediu o que a
linha faz antes de deixá-la entrar.

### `[OBS]` O que ficou aberto

1. **HiDPI.** O ladrilho usa pixel lógico, exatamente como a textura usava
   antes desta frente (`ViewContext::tileSize`/`tile`, `Session.h`). Não
   olhado em nenhum monitor com escala ≠ 1.
2. **O tempo de render de um ladrilho num ícone pesado nunca foi medido.** Um
   dado de referência, não medido nesta task e que não limita nada sobre
   ladrilhos, só diz a ordem de grandeza em que a pergunta vive: um render
   CHEIO de 2048×2048 do ícone `GoWToolkit.icon` levou ~9,1 s nesta máquina.
3. **O grampo de `sampleBilinear` esconde uma margem curta sobre uma região
   plana.** `DisplacementOracle.cpp:171-184` (`at()`) documenta isto no
   próprio código: o segundo grampo é só memória — impede a leitura de sair
   do buffer, não promete detectar que ela saiu — e substitui a leitura pelo
   texel da borda do buffer; sobre um fundo uniforme ou o canvas transparente
   que costuma ocupar a margem externa de um ícone, esse texel **é** o valor
   certo, e uma margem curta passa sem aparecer no gate. Isto qualifica
   quanto um "zero" do invariante prova (a mesma ressalva que "A margem, e de
   onde vem o número" já carrega para `Jellify-Music`, acima).
4. **`RenderView::refined` é produzido e não é lido por ninguém na UI.**
   Quando um ladrilho cai para a base (teto de área ou de aparelho),
   `OnyxPorts.cpp:78-93` empurra uma linha para `notes` explicando O MOTIVO
   da queda ("ladrilho recusado, caiu para a base: …", ou a nota do teto de
   área preservada), e essa linha aparece no painel de Diagnóstico via o laço
   de `[OBS]` que já lê `view.notes` (`PanelCanvas.cpp:605`). Mas nada em
   `PanelCanvas.cpp` lê `view.refined`, então não existe uma linha própria
   dizendo "estes pixels na tela são o canvas inteiro esticado, não o
   ladrilho pedido" — o motivo da queda aparece; o fato de que o resultado
   NA TELA é a base esticada, não.
5. ~~**O predicado de assentamento não cobre um redimensionamento do
   viewport.**~~ **RESOLVIDO em 18/09.** `settled` passa a comparar também o
   LADRILHO que este quadro pediria com o do quadro anterior
   (`ViewContext::lastWanted`), de modo que redimensionar a janela ou
   arrastar o splitter do dock reinicia o temporizador como um arrasto
   reinicia. A comparação é do ladrilho, e não de `st.painted` cru, porque é
   o ladrilho que vira pedido: ele é inteiro (um pixel de jitter que não muda
   ladrilho nenhum não reinicia a espera) e com zoom ≤ 1 ele é sempre vazio,
   então nada muda lá. `[ART]` Contra o predicado antigo,
   `canvas_asks_for_nothing_while_the_painted_rectangle_keeps_changing`
   (`Tests/test_kit_canvas.cpp`) foi VISTO disparando um pedido por quadro
   durante o gesto (`asks` indo 2 → 3 → 4 → 5 … em vinte quadros de
   redimensionamento); com o conserto, um só, depois que o gesto para.
6. **O `<filter>` de SVG** já tem seu próprio `[OBS]` em "A margem, e de onde
   vem o número", acima ("O `<filter>` de SVG é RECUSADO num buffer parcial,
   não margeado") — não duplicado aqui.

Commits do estado: `03a043f` fechou o passo 8, e este arquivo fechou a spec
para a Task 9 do plano. O HEAD desta frente é agora o terceiro commit da onda
de conserto da revisão de branch (`789ce9b` o código, `fc979d1` a spec, e a
correção documental deste commit) — a linha "revisão" da tabela acima.
