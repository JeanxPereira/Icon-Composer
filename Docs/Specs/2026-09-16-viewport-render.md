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
`size`, e em quatro viewports que cobrem quadrantes sobrepostos — e compara. A
tolerância é ZERO em aritmética de ponto flutuante determinística; se não for
zero, o desenho está errado e não a tolerância.

## A margem, e de onde vem o número

Um pixel dentro do viewport pode depender de geometria fora dele. Os alcances
são conhecidos e medidos, em unidades de canvas (o canvas tem 1024 —
`IconRenderer.h`):

| efeito | alcance | sítio |
| --- | --- | --- |
| desfoque do material | `min(b,1) × blurStrengthMax`, máx **64** | `GlassMaterial.h:237` |
| sombra: deslocamento | **32** para baixo | `GlassShadow.h:274` |
| sombra: desfoque | mesmo `blurStrengthMax × clamp(radius)`, máx **64** | `GlassShadow.h:123` |
| sombra: anel | **16** | `GlassShadow.h:126` |
| campo de distância | o que a banda do especular lê, ~**24** | `GlassSpecular.h` |

O pior caso empilha sombra deslocada e desfocada: 32 + 64. A margem inicial é
**128 pontos de canvas** em cada lado, convertida em pixels pela escala do
render, e o invariante acima é que decide se ela basta — cresce até o
diferencial zerar, e o número final entra aqui como medida.

`[OBS]` O campo de distância é o único passo cujo alcance não é uma constante: a
distância de um pixel fundo dentro da forma pode medir até a borda mais distante
do canvas. O que salva é que nenhum consumidor lê o campo além da sua própria
banda — o especular clampa em `inset + height`, a sombra em `ringWidth`, a
translucidez na própria rampa. O invariante é o que prova isso; se ele acusar
diferença longe da borda, esta hipótese caiu e o campo precisa de tratamento
próprio.

## O teto de área

Todo passo roda na resolução da tela, margem cheia — não há efeito calculado na
resolução base e amostrado para dentro. Em troca existe um teto: se
`(viewport + margem)` passar de **16 Mpx**, o refino não acontece e o canvas
volta a esticar a textura da resolução base.

O teto é explícito porque a margem escala com o zoom enquanto o retângulo
visível não: em 1600% os 64 pontos do desfoque viram 1024 pixels por lado. O
ladrilho é constante; a margem não é.

## O que o Kit faz

A chave do `RenderCoordinator` ganha o viewport, e o canvas passa a mostrar
**apenas o ladrilho corrente** — não há composição com o render cheio por baixo.

O custo disto está aceito e registrado aqui para que ninguém o redescubra como
defeito: **cada pan e cada zoom tem um intervalo sem imagem nítida**, e em um
ícone pesado esse intervalo é longo. O `RenderedIcon` já carrega `pending` e
`pendingSeconds`, e o painel já os mostra; é por ali que o intervalo se explica
ao usuário.

O retângulo visível sai do que `PanelCanvas` já calcula: `canvasImageRect(pan,
sidePx, zoom)` interseptado com a área disponível do painel, levado de volta ao
espaço da imagem.

## Os sítios

`options.size` aparece 32 vezes em `IconRenderer.cpp`. Os que carregam origem
ou extensão, e portanto mudam:

`paintBackground` (633), `clipToChiclet` (639), `drawChicletHighlights` (669),
`glassRefractionFor` (745), o raio do desfoque (829), `placeRaster` (1160),
`artPlacementRect` (1170, 1372), `placeOnCanvas` (1273, 1468),
`generateFieldFromAlpha` (1317), `generateFieldFromContours` (1301),
`glassOver` (1351), `rasterPlacementRect` (1373), `shadowGeometry` (1428) e
`shadowImage` (1429).

Os demais usam `size` como escala pura — esses não mudam, e o fato de serem
distinguíveis é o argumento de que a separação dos três significados é a
refatoração certa e não uma varredura cega.

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
   gate do invariante escrito ANTES de qualquer sítio mudar — ele tem que passar
   trivialmente no padrão e falhar em qualquer viewport não trivial.
2. Os sítios, em grupos que o gate consegue julgar um a um: fundo e chiclet;
   colocação de arte; campo e vidro; sombra.
3. A margem, crescida até o diferencial zerar, com o número medido escrito aqui.
4. O teto de área.
5. O Kit: chave, ladrilho, e o estado de espera já existente.
