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

**Quando o diferencial não for zero, o relatório diz de que tipo ele é.** A
arte vetor passa pela GPU (`Device.h`), e uma coordenada transladada não tem
garantia de arredondar igual à original. Então o gate imprime o `max |Δ|` e a
distância do pior pixel até a borda do buffer. Uma margem curta aparece com Δ
grande e colada na borda. Resíduo de aritmética aparece com Δ na ordem de ULP,
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
| sombra: desfoque | `2,8 × 64 × clamp(radius)`, máx **179,2** | `GlassShadow.cpp:164`, `BlurKernel.cpp:44` |
| sombra: deslocamento | **32** para baixo, somado ao de cima | `GlassShadow.h:274`, `GlassShadow.cpp:277` |
| sombra: anel | **16** | `GlassShadow.h` (`Shadow.ringWidth`) |
| refração: deslocamento | `|refractionStrength|`, teto **640** | `GlassMaterial.h` (`refractionStrengthMax`), `GlassLayer.cpp:146` |
| refração: banda do campo | `refractionHeight`, teto **256** | `GlassMaterial.h` (`refractionHeightMax`) |
| especular: banda do campo | `inset + height`, ~**24** | `GlassSpecular.h` |
| desfoque do material (desenho DESLIGADO hoje) | `2,8 × min(b,1) × 64`, máx **179,2**, +1 px de outset | `BlurKernel.h`, `IconRenderer.cpp:828` |

**A margem é calculada por documento, não é uma constante.** O pior caso
teórico é 640 pontos, mais de meio canvas. Com uma constante desse tamanho, o
viewport vira o render cheio em todo ícone. A margem então sai dos parâmetros
já denormalizados dos grupos que de fato desenham cada efeito. É o máximo, por
lado, entre:

- sombra: `2,8 × σ + deslocamento`;
- refração: `max(|strength|, height)`;
- especular: a banda dele.

Esse máximo é convertido em pixels pela escala e arredondado para cima.
`[ART]` **Medido em 16/09.** Só 3 dos 146 documentos do corpus têm
`refractivity`, e só **2** deles com força diferente de zero:

| documento | força | alcance em pontos |
| --- | --- | --- |
| `CamilleScholtz__swmpc__swmpc` | −0,527 | **337,3** |
| `StikDebug__StikPair__StikPair` | −0,359 | 229,9 |

Nos dois, a altura denormalizada (134,5 e 105,3) fica abaixo da força. O
terceiro, `videolan__vlc-ios__VLC26`, tem `enabled: false` e força 0, ou seja,
identidade. Então, em 144 dos 146 documentos, quem manda na margem é a sombra
(≤ 211 pontos). Nos dois que refratam, a margem chega a um terço do canvas por
lado. O viewport ainda ganha área, só que menos. A varredura é um passeio
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
`GlassTranslucency.h:206` e é a primeira coisa a conferir se o gate acusar). O invariante é o que prova isso; se ele acusar
diferença longe da borda, esta hipótese caiu e o campo precisa de tratamento
próprio.

## O teto de área

Todo passo roda na resolução da tela, margem cheia — não há efeito calculado na
resolução base e amostrado para dentro. Em troca existe um teto: se
`(viewport + margem)` passar de **16 Mpx**, o refino não acontece e o canvas
volta a esticar a textura da resolução base.

O teto é explícito porque a margem escala com o zoom e o retângulo visível não.
Com o canvas de 512 em 1600% (`size = 8192`, 8 px por ponto), os 211 pontos da
sombra viram ~1690 pixels por lado, e o buffer ainda é cortado pelo canvas. O
ladrilho é constante; a margem não é. Consequência: o teto dispara justamente
nos zooms mais altos e nos documentos com refração forte, e é lá que o canvas
volta a esticar. Isso está aceito, e o motivo é este.

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
- o retângulo do canvas que o gradiente do fundo mede (626-627, origem);
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

- o grampo UV de `glassOver`, que é o buffer e precisa ser o canvas
  (`GlassLayer.cpp:222-235`);
- o grampo de `sampleBilinear` (`DisplacementOracle.cpp:162`);
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
