#pragma once
// O CHICLET GANHA OS PROPRIOS REALCES.
//
// Ate este arquivo, `ICRRenderingParameters.Highlights` entregava luz a UM dos
// dois lados que a pedem. `GlassSpecular.*` desenha o lado do GLIFO -- o
// conjunto `glyphs*`, seis realces por grupo de vidro na geracao 27. O outro lado, a
// propria pastilha, tinha os numeros lidos (`Docs/Laudos/2026-09-15-highlights.md`
// §4.3) e NADA os consumia; o `[OBS] 6` daquele laudo dizia exatamente isso:
// "o chiclet -- numeros lidos, nada os consome; o `hasSpecular` desta cadeia e
// o da camada". O fundo saia uma rampa chapada.
//
// Fonte: `References/2.0-125/out/slices/IconRendering.arm64` (VA == desloc. de
// arquivo) e `References/2.0-125/out/fieldmd_iconrendering.txt`. O laudo desta
// frente e `Docs/Laudos/2026-09-15-chiclet-realces.md`.
//
// ===========================================================================
// 1. ONDE A CADEIA DO CHICLET E APLICADA, E QUAL E O PORTAO DELA
// ===========================================================================
//
// `[BIN]` O glifo passa por `0x000491C0`-`0x00049DBC` com o portao
// `material.hasSpecular` em `0x00049200`. O CHICLET NAO TEM `hasSpecular` de
// camada, e por isso nao podia estar naquela funcao. Ele tem a sua propria:
//
//     `0x000475A0`-`0x00047AE8` (1352 bytes)
//       0x000475C8  ldrb w8, [x20, #0x21]
//       0x000475CC  cmp  w8, #1
//       0x000475D0  b.ne #0x47a5c          ; -> epilogo. FALSO NAO DESENHA NADA.
//       0x000475D8  add  x0, x20, #0x408   ; o FILL (o struct de 0xB0 do §2)
//       0x000475DC  add  x20, x20, #0x68   ; o contexto
//       0x000475E0  bl   #0x5e590          ; <- o resolvedor do CHICLET
//
// `[BIN]` `0x0005E590` e um thunk de tres instrucoes que poe `x1 = 0x00062588`
// -- o *closure* do chiclet -- e cai no MESMO corpo `0x0005E59C` que o glifo
// usa por `0x0005E194` (`x1 = 0x000627B4`). Um corpo, dois seletores: e por
// isso que os dois lados compartilham o expansor, o resolvedor e o agrupador.
//
// `[BIN]` Chamado de UM sitio so (`0x000475E0`), contra tres do lado do glifo.
//
// `[OBS]` O QUE E O BYTE `ctx+0x21` NAO FOI LIDO. Ele e o `hasSpecular` desta
// cadeia e nao tem nome no metadado que abriu aqui. Este renderizador desenha
// os realces do chiclet sempre que o fundo e pintado -- que e o unico estado em
// que ha pastilha -- e diz isso em `out.notes` em vez de fingir que leu.
//
// ===========================================================================
// 2. QUAL CONJUNTO O CAMINHO NORMAL ESCOLHE
// ===========================================================================
//
// `[BIN]` O seletor do chiclet, `0x00062588`-`0x000627B0`, tem DOIS ramos, e o
// primeiro teste nao e sobre o fill e sim sobre o parametro:
//
//     0x000625B4  ldrb w8, [x20, #0x110]     ; chicletHighlightsAppearanceMode
//     0x000625B8  cmp  w8, #1
//     0x000625BC  b.ne #0x62698              ; -> ramo systemAppearance
//
// `[BIN]` E o metadado diz o que esse enum e -- `fieldmd 0xA4FB0`,
// `ICRRenderingParameters.Highlights.ChicletHighlightsAppearanceMode`, DOIS
// casos: `systemAppearance` e `chicletLuminance`. `[BIN]` O padrao e `1`
// (`strb w22, [x19, #0x110]` com `w22 == 1`, `0x00062B34`), e `1` e o segundo
// caso: **`chicletLuminance`**. O caminho normal e o de luminancia.
//
//   ramo `chicletLuminance` (modo == 1), `0x000625C0`-`0x000625E8`:
//       if (fill[+0x90] == 1 && (fill[+0x68]|+0x70|+0x78|+0x80|+0x88) == 0)
//           switch (fill[+0x5B]) {          ; 0x00062744
//               0 -> chicletDefault (+0x118)
//               1 -> chicletBright  (+0x748)
//               _ -> chicletDim     (+0xD78) }
//       else
//           duas comparacoes de forma (0x00040E60, 0x0006D6B0) escolhem
//           chicletScreened (+0x19D8) ou chicletClear (+0x13A8)
//
//   ramo `systemAppearance` (modo != 1), `0x00062698`-`0x000626AC`:
//       fill[+0x61] == 1 -> chicletDim ; senao chicletDefault
//
// `[BIN]` E A GERACAO 26 E O RAMO `systemAppearance`: `0x76FC0` grava `0` em
// `Highlights+0x110` (`0x77298`). La o conjunto sai so da aparencia do estilo
// (`+0x61`, `1` e escura -- o mesmo byte que `0x0005E59C` le para decidir o
// clear mode efetivo), sem olhar o modo de renderizacao nem `iconBrightness`.
// `chicletHighlightsSetFor`, abaixo, e a arvore inteira.
//
// `[BIN]` E o "fill simples" do primeiro ramo NAO e sobre o fill: as cinco
// palavras e a tag de `+0x68..+0x90` sao o MODO DE RENDERIZACAO do icone, e o
// teste e `renderingMode == .color` (`GlassSpecular.h`, `glyphHighlightsSetFor`).
// As "duas comparacoes de forma" sao a igualdade do clear mode efetivo com nil:
// nil escolhe `chicletScreened`, senao `chicletClear`.
//
// `[BIN]` E ENTAO O SELETOR CHAMA O MESMO EXPANSOR DO GLIFO, com os dois
// campos de curvatura DO CHICLET e um booleano a menos:
//
//     0x00062770-0x00062794
//       sp+0x2C0 <- Highlights+0x50  (chicletHighlightCurvature)
//       sp+0x180 <- Highlights+0x70  (chicletDarklightCurvature)
//       strb wzr, [sp, #0x1A0]       ; o terceiro argumento e FALSO aqui
//       bl #0x30e88                  ; o expansor de sete posicoes
//
// `[BIN]` Os dois sao `[0.75]x4` na geracao 27 (`fmov v0.2d, #0.75`,
// `0x00062ABC`, escrito quatro vezes sobre `+0x50` e `+0x70` em
// `0x00062ADC`-`0x00062AE0`), os mesmos numeros do glifo, e `[1]x4` na geracao
// 26 (`0x77244`, `0x77270`). `[BIN]` O booleano falso e o byte de `x1+0x20`
// que o expansor le em `0x00030EBC`: ligado, as duas posicoes `dark` tomariam a
// curvatura do REALCE em vez da do darklight (`0x000311C4`). Os dois seletores
// passam zero. (Ate 01/10 este paragrafo o lia como "o VCM nao vale para o
// chiclet"; o VCM nao passa por aqui -- o chiclet tem o proprio rasterizador,
// §4.)
//
// -----------------------------------------------------------------------
// 2.1. `fill[+0x5B]` E A CLASSE DE LUMINANCIA, E A ESCRITA DELE ESTA LIDA
// -----------------------------------------------------------------------
//
// O laudo dos realces registrou "`[OBS]` o que `fill[+0x5B]` E nao foi lido...
// a escrita do byte nao foi achada". Foi achada. `[BIN]` `0x0001A8C0` e
// `0x0001A920`-`0x0001A97C`, dentro de `0x00019F4C`-`0x0001A998`:
//
//     ; x20 e o objeto de parametros (payload a +0x10), entao
//     ;   +0xA8 == Highlights+0x98  maxDimChicletLuminance     = 0.2
//     ;   +0xB0 == Highlights+0xA0  minBrightChicletLuminance  = 0.99
//     ;   +0xB8 == Highlights+0xA8  iconBrightnessOnlyUsesMax  = false
//     0x0001A920  ldp  d10, d11, [x20, #0xa8]
//     0x0001A924  ldrb w21,      [x20, #0xb8]
//     0x0001A944  bl   #0x1f10c             ; -> (d0 = MIN, d1 = MAX)
//     0x0001A958  fcmp d11, d9              ; minBright vs MAX
//     0x0001A95C  b.pl #0x1a968
//     0x0001A960  mov  w8, #1               ; BRIGHT
//     0x0001A968  cmp  w21, #0
//     0x0001A96C  fcsel d0, d9, d8, ne      ; onlyUsesMax ? MAX : MIN
//     0x0001A970  fcmp d0, d10              ; vs maxDim
//     0x0001A974  b.pl #0x1a8c0             ; -> mov w8, #0  DEFAULT
//     0x0001A978  mov  w8, #2               ; DIM
//     0x0001A8C4  strb w8, [x19, #0x5b]
//
// e o mesmo teste do seletor guarda a entrada (`0x0001A89C`-`0x0001A8B4`):
// fora dele ninguem classifica e o byte fica **0**, `Default`.
//
// `[BIN]` ESSE TESTE E O MODO DE RENDERIZACAO, e nao o tipo do fill, como este
// paragrafo dizia ate 01/10. As cinco palavras e a tag que ele olha (`x25`,
// `x22`, `x21`, `x26`, `x24`, `w27`) sao as que `0x0001A80C`-`0x0001A824` gravam
// em `+0x68..+0x90`, o campo que `0x0005E59C` le como o modo; a faixa de leveza
// sai do fill RESOLVIDO em `+0x10` (`0x0001A928`-`0x0001A944`), qualquer que
// seja o tipo dele. Um fill de sistema classifica pelas duas paradas dele.
//
// `[BIN]` `0x0001F10C`-`0x0001F32C` e a faixa de luminancia de um fill, e ela
// e uma conta so, por PARADA de gradiente, com passo `0x28`:
//
//     0x0001F1EC-0x0001F208   hi = max(r,g,b) ; lo = min(r,g,b)
//     0x0001F220-0x0001F224   L  = (hi + lo) * 0.5        <- LEVEZA HSL
//     0x0001F278-0x0001F2C0   devolve (min L, max L) sobre todas as paradas
//     0x0001F2F8-0x0001F2FC   sem paradas: (0.0, 1.0)
//
// `maxDimChicletLuminance` e `minBrightChicletLuminance` nao sao lidos em
// nenhum outro sitio do slice (varredura de `ldr d, [x, #0x98]` / `#0xA0` sobre
// todo o `__text`: quatro e treze ocorrencias, e as unicas que leem as DUAS
// juntas sao dois construtores de copia, `0x00070F34` e `0x00081290`). Este e o
// consumidor, e e o unico.
//
// -----------------------------------------------------------------------
// 2.2. E O RESULTADO E QUE A CLASSE NAO MOVE PIXEL NENHUM NA GERACAO 27
// -----------------------------------------------------------------------
//
// `[BIN]` O laudo dos realces fechou com "`fill[+0x5B]` ... inertes em 2.0-125
// do lado do glifo, **nao** do lado do chiclet". A segunda metade dessa frase
// esta ERRADA, e o construtor prova: `chicletDefault`, `chicletBright` e
// `chicletDim` sao montados dos MESMOS valores, membro a membro.
//
// O construtor `0x00062A78`-`0x00063C1C` guarda cada constante uma vez em
// `sp+0x00`..`sp+0xD0` e depois so as recopia. Os trinta `memcpy` de `0x101`
// bytes contam a historia:
//
//     Default  0x118 0x220 0x328 0x430 0x538 0x640   (0x62BC8 .. 0x62ED0)
//     Bright   0x748 0x850 0x958 0xA60 0xB68 0xC70   (0x62F50 .. 0x63238)
//     Dim      0xD78 0xE80 0xF88 0x1090 0x1198 0x12A0(0x632C8 .. 0x63608)
//
// e os DEZOITO membros desses tres conjuntos leem exclusivamente as fatias
// gravadas antes de `0x00062F00`. A primeira constante NOVA do construtor
// aparece em `0x00063624` (`ldr q0, [x8, #0x740]` == `{1.0, 1.25}` e
// `fmov v1.2d, #1.25`) -- DEPOIS do sexto `memcpy` do `Dim` em `0x00063608`,
// e portanto ja dentro de `chicletClear`. `Clear` e `Screened` de fato diferem;
// `Default`, `Bright` e `Dim` nao.
//
// > Entao a regra de luminancia e MEDIDA e IMPLEMENTADA aqui, e o pixel que ela
// > escolhe e o mesmo nos tres casos. Este arquivo a implementa mesmo assim,
// > pelo mesmo motivo que `highlightSizeValue` implementa a inversao de tamanho
// > que tambem nao se ve hoje: o dia em que um arquivo de parametros
// > diferenciar os conjuntos, a regra ja esta certa.
//
// `[BIN]` NA GERACAO 26 ELA MOVE, e pelo outro lado: o chiclet de la nem a
// consulta (o ramo `systemAppearance`), mas o seletor do GLIFO consulta, e
// `glyphsBright` da 26 nao e `glyphsDefault`.
//
// `[BIN]` `chicletClear` (`+0x13A8`) e `chicletScreened` (`+0x19D8`) ESTAO
// transcritos desde 01/10 (`RenderingParameters.cpp`), e o seletor que escolhe
// entre os dois tambem -- e o renderizador os ALCANCA: fora de `.color` a
// geracao 27 desenha `chicletClear` sob a mascara do Clear (onde os claros saem
// repintados pela cor dela, `SpecularArguments::clearPaint`, e os escuros ficam
// para o sistema) e `chicletScreened` no Tinted Dark, cujo modo efetivo do
// Clear e nil (`0x62684`-`0x62740`).
//
// ===========================================================================
// 3. OS SEIS MEMBROS DE `chicletDefault` DA GERACAO 27, RELIDOS DO BINARIO
// ===========================================================================
//
// `[BIN]` `0x00062AB8`-`0x00062EDC`. Campo a campo, com o endereco de cada
// escrita conferido contra a regua de `HighlightSettings` (`0x101` bytes, dez
// campos) que `GlassSpecular.h` ja carrega:
//
//   | | keySharp | keyDiffuse | fillSharp | fillDiffuse | dark | rim |
//   | brightness | 1.1 | 1.1 | 1.1 | 1.1 | 0.0 | 1.0 |
//   | opacity    | 0.2 | 0.5 | 0.2 | 0.25 | 0.2 | 0.0 |
//   | distance   | 10  | 40  | 10  | 40   | 10  | 10  |
//   | spread     | 2pi/3 | pi/2 | 2pi/3 | pi/2 | pi/3 | pi |
//   | bias       | 0.5 | 0.08 | 0.5 | 0.08 | 0.5 | 1.0 |
//
// `outsetOpacity` e `minInsetPixels` sao `nil` NOS SEIS (o byte de tag `1` em
// `+0x48` e `+0xD0`), `inset` e `minDistancePixels` sao zero nos seis, e
// `blendModeOverride` e o sentinela `18` nos seis.
//
// DUAS CORRECOES AO §4.3 DO LAUDO DOS REALCES, as duas `[BIN]`:
//
//   1. O `bias` do `rim` e **1.0**, nao `0.5`. `x27` e reatribuido a
//      `0x3FF0000000000000` em `0x00062E68`, entre o `dark` e o `rim`, e e esse
//      x27 que vai para `+0xF8` em `0x00062EC0`. (Nao move pixel: a opacidade
//      do `rim` e zero.)
//   2. Os quatro valores de cada `SizeBasedValue` sao IGUAIS nos seis membros do
//      chiclet -- ao contrario do glifo, onde `distance` vale 4 em `display` e
//      6 nas outras tres e `opacity` cai a 0.3 em `small`. A inversao
//      `slots[3 - sizeClass]` continua aplicada aqui e continua invisivel.
//
// TODOS OS SEIS EXISTEM, o que e a diferenca que mais muda a figura: no glifo
// o `rim` e `nil` e `fillDiffuse` e `matchKey`. No chiclet os seis estao
// presentes, o expansor faz SETE posicoes (o `dark` duas vezes), e o `rim` --
// presente mas com `opacity == 0` -- e o unico que nao pinta. Ficam **seis
// realces vivos**, os mesmos seis do glifo por outro caminho.
//
// `[BIN]` NA GERACAO 26 SAO TRES, e outros (`0x772BC`-`0x7745C`, e
// `chicletDim` em `0x775BC`-`0x776F8`): `keySharp` (brightness 1.1, cone de
// 78 graus), `fillSharp` com payload proprio (1.1, 65 graus) e o `rim` (1.0,
// cone pi) -- `keyDiffuse` e `dark` nil, `fillDiffuse` `.custom(nil)`. Os tres
// com `distance` `[22, 22, 30, 39]`, bias 0.5, a opacidade POR CLASSE de
// tamanho e `blendModeOverride == .normal`: la o realce nao soma, ele cobre.
// `chicletBright`, `chicletClear` e `chicletScreened` nao sao reescritos.
//
// ===========================================================================
// 4. O RASTERIZADOR: O QUE FOI LIDO, E O QUE ESTE ARQUIVO TOMA DELE
// ===========================================================================
//
// `[BIN]` O glifo termina no shader Metal `glassHighlight` (`0x0000E834` monta
// por nome). O CHICLET NAO. `0x000475A0` chama `0x0000D904`, 3888 bytes que
// desenham no `RBDisplayList`:
//
//     0x0000E0F0  beginLayerWithFlags:
//     0x0000E134  clipLayerWithAlpha:mode:
//     0x0000E448  setConicGradientCenter:angle:stopCount:colors:colorSpace:
//                 locations:flags:
//     0x0000E490  drawShape:fill:alpha:blendMode:
//     0x0000E500  drawLayerWithAlpha:blendMode:
//
// isto e: uma FAIXA recortada, preenchida por um GRADIENTE CONICO em torno do
// centro. A forma da pastilha e analitica, entao o alvo nao precisa de campo de
// distancia: o conico da o termo ANGULAR e a camada recortada da o termo
// RADIAL.
//
// `[BIN]` LIDO EM 01/10, passo a passo, por passada de realces. (O passo 1 foi
// relido instrucao por instrucao para este arquivo; os passos 2 a 5 sao do
// levantamento da geracao 26, lidos uma vez -- nenhum deles e transcrito aqui.)
//
//   1. 256 amostras angulares. Por realce: `theta = atan2(dir.x, dir.y)`
//      (`0x0000DB90`), e por amostra `i`
//
//          phi_i = theta - pi/2 - 2 pi i / 256                  0x0000DBC8, 0x0000DC90
//          lit_i = max(0, (cos phi_i - cos spread)
//                         / max(1 - cos spread, 1e-6))          0x0000DBCC-0x0000DCB8
//
//      OU `lit_i = 1` quando `|spread / (2 pi) - 0.5| < 1e-6` -- o cone de pi
//      exato (`0x0000DBA0`-`0x0000DBBC`, testado em `0x0000DC50`). Cada realce
//      entra por cima do anterior na mesma amostra, `a <- w + (1 - w) a`
//      (`0x0000DBF4`-`0x0000DC40`): a uniao dos realces da passada. NAO ha aqui
//      a sentinela `spread > pi` do shader do glifo.
//   2. Uma rampa de 65 paradas de alfa, `t = j / 64`, na cor da passada:
//      `alpha = cor.a t / max(1 + (1/bias - 2)(1 - t), 1e-6)`, instalada com
//      `addStyle:1`.
//   3. Uma camada: `0x000126EC` desenha a forma do chiclet com
//      `setRenderingMode:2` e `setRenderingModeArgument: height`, sob uma
//      matriz de cor `alfa <- curvature x alfa`; depois
//      `clipLayerWithAlpha:1.0 mode:1` (`0x0000E134`).
//   4. A forma TRACADA com largura `2 x height`, preenchida pelo gradiente
//      conico centrado no meio do retangulo (257 paradas, `0x0000E448`).
//   5. `drawLayerWithAlpha:(float)opacidade blendMode:` pela tabela `0x978F4`
//      (`0x0000E500`).
//
// Ele nunca le `inset` nem `outsetOpacity`.
//
// `[INF]` ESTE RENDERIZADOR CONTINUA SEM TRANSCREVER ESSE CAMINHO INTEIRO. Ele
// resolve os realces pelo MESMO `0x0004BD90` que o alvo usa nos dois lados -- e
// que `resolveHighlight` ja e -- e os avalia com o corpo de
// `glassHighlightFragment` sobre o campo de distancia do proprio chiclet. Os
// NUMEROS sao `[BIN]`; a maquina que os converte em cobertura e `[INF]`. Duas
// coisas do rasterizador lido entram, porque sem elas a geracao 26 sai errada
// a olho: o cone de pi exato acende o contorno INTEIRO (`chicletHighlightCone`)
// e o `inset` nao e lido (`resolveChicletHighlight`). O resto fica como estava
// e `kChicletRasteriserNote` diz o que: o perfil radial (o modo de renderizacao
// 2 do RBShape e o `clipLayerWithAlpha mode:1` nao foram lidos; aqui e o
// `shade` do shader do glifo), a convencao de angulo do conico (aqui a normal
// do contorno, que nos cantos nao e o angulo polar do centro), a uniao dos
// realces de uma passada (aqui um por vez), e os dois pisos `1e-6` (aqui o
// `2^-10` do shader).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/GradientOracle.h"
#include "Source/RenderBox/PixelGrid.h"
#include "Source/RenderBox/RenderingParameters.h"

namespace rb {

// `[BIN]` O que `fill[+0x5B]` guarda, e o que `0x00062744` faz com ele, e
// `ChicletAppearance` -- declarado em `GlassSpecular.h` desde 01/10, porque o
// seletor do GLIFO (`0x000627B4`) troca pelo mesmo byte.

// `[BIN]` O SELETOR DO CHICLET, `0x00062588`, inteiro (§2):
//
//     modo == chicletLuminance (geracao 27)
//         renderingMode == .color   iconBrightness: 0 -> Default, 1 -> Bright,
//                                   senao Dim                     0x00062744
//         senao                     clear mode efetivo nil ? Screened : Clear
//     modo == systemAppearance (geracao 26)
//         aparencia escura ? Dim : Default                        0x00062698
HighlightsSetKind chicletHighlightsSetFor(ChicletHighlightsAppearanceMode mode,
                                          bool appearanceIsDark, bool colourMode,
                                          ChicletAppearance iconBrightness,
                                          bool effectiveClearModeIsNil);

// `[BIN]` O par que `0x0001F10C` devolve: a menor e a maior LEVEZA HSL
// `(max(r,g,b) + min(r,g,b)) / 2` entre as paradas do fill. Sem paradas,
// `(0.0, 1.0)` (`0x0001F2F8`).
struct ChicletLuminance {
    double lo = 0.0;
    double hi = 1.0;
};

// `0x0001F10C`. `rgb` e uma parada por elemento, componentes em [0,1] e NAO
// pre-multiplicadas -- o alvo le os tres primeiros `Double` de cada elemento de
// `0x28` bytes e nao toca no quarto.
ChicletLuminance chicletFillLuminance(const std::vector<RampPoint>& stops);
ChicletLuminance chicletFillLuminance(const float rgb[3]);

// `0x0001A920`-`0x0001A97C`. `simpleFill == false` devolve `Default` sem
// classificar, que e o `mov w8, #0` de `0x0001A8C0` -- e o que o nome chama de
// "fill simples" e `renderingMode == .color` (§2.1).
//
// Os dois limiares entram com os valores que o preambulo escreve
// (`Highlights+0x98` e `+0xA0`, pool `0x986E0` == `{0.2, 0.99}`), e
// `onlyUsesMax` com o `false` de `0x00062AF8` -- a geracao 27. A 26 liga
// `iconBrightnessOnlyUsesMax` (`0x77848`); quem chama passa os tres do bloco.
ChicletAppearance classifyChicletAppearance(const ChicletLuminance& l, bool simpleFill = true,
                                            bool onlyUsesMax = false,
                                            double maxDimLuminance = 0.2,
                                            double minBrightLuminance = 0.99);

// `[BIN]` As SETE posicoes que `0x00030E88` monta a partir do `chicletDefault`
// da geracao 27, na ordem em que as monta. A setima (`rim`) tem `opacity == 0`
// e por isso nao pinta -- ela esta aqui porque esta la, e porque um leitor que
// a visse sumir precisaria saber se ela foi descartada (como no glifo, onde
// `rim` e `nil`) ou apenas nao pinta (como aqui). E
// `expandedHighlights(G27, Chiclet, Default)` (`RenderingParameters.h`) sob o
// nome antigo; o renderizador pede a lista da geracao e do conjunto que escolheu.
const HighlightSlot* chicletHighlightSlots(std::size_t& count);

// `[BIN]` O TERMO ANGULAR DO RASTERIZADOR DO CHICLET (§4, passo 1), na forma
// que `glassHighlightFragment` consome: o cosseno do cone, ou -- no cone de pi
// exato -- "aceso em todo o contorno".
//
//     0x0000DBA0  fmul d1, d11, #0.5 ; fdiv d1, d1, pi ; fadd d1, d1, #-0.5
//     0x0000DBBC  fabs d11, d1                 ; |spread / (2 pi) - 0.5|
//     0x0000DC50  fcmp d11, 1e-6 ; b.pl ...    ; menor: lit = 1.0 (fmov d4, #1.0)
//
// E a diferenca que a geracao 26 mostra: o `rim` dela tem `spread == pi` e
// opacidade positiva, e pelo shader do glifo sairia `lit = (dot + 1) / 2` --
// meia volta acesa em vez da volta inteira.
struct ChicletCone {
    double cone = 0.0;
    bool alwaysLit = false;
};
ChicletCone chicletHighlightCone(double spread);

// `resolveHighlight` para o lado do chiclet: o mesmo `0x0004BD90`, e depois o
// `inset` zerado -- `[BIN]` `0x0000D904` le `height`, `curvature`, `spread`,
// `bias`, a direcao, a cor, a opacidade e o modo, e nunca `+0x38`.
GlassHighlightSettings resolveChicletHighlight(const HighlightSlot& slot,
                                               const SpecularArguments& args);

// Um fragmento do realce do chiclet: `highlightFragment` com o termo angular de
// `chicletHighlightCone`.
double chicletHighlightFragment(const GlassHighlightSettings& s, double sd, double nx,
                                double ny);

// O que deste rasterizador continua desenhado sem ter sido lido (§4). Vai no fim
// de `chicletHighlightsNote`.
extern const char* const kChicletRasteriserNote;

// O contorno da pastilha que `drawChicletHighlights` usa para o campo, exposto
// para o caminho residente (que faz o mesmo campo na GPU).
std::vector<FieldContour> chicletFieldContours(std::uint32_t size, IconPlatform platform);

// Compoe os realces do chiclet -- os do conjunto `args.set` da geracao
// `args.generation` -- sobre `rgba` (pre-multiplicado, `size` x `size`,
// ja recortado a pastilha). Devolve quantos pixels distintos moveram.
//
// O campo de distancia e o do proprio contorno continuo de `ChicletShape.h`, e
// o recorte -- o `clipLayerWithAlpha:` de `0x0000E134` -- e o ALFA QUE O FUNDO
// JA TEM: ele ja carrega a cobertura da pastilha, e uma pastilha transparente
// (`automatic` sob `tinted` e `IconColor.clear`) nao tem superficie para
// acender. Sem isso o realce poria luz sobre o nada.
//
// A pastilha e ABSOLUTA e so o buffer de `grid` e varrido: o contorno sai de
// `grid.size` e o campo nasce com a origem de `grid`, entao o realce de um
// pixel e o mesmo do render cheio (spec 2026-09-16, "Os sitios").
//
// `platform` e a mesma de `ChicletShape.h`: o realce nasce do contorno da
// plataforma, entao no watchOS ele corre a borda do circulo e nao a de um
// quadrado que nao esta la.
std::size_t drawChicletHighlights(std::vector<float>& rgba, const PixelGrid& grid,
                                  const SpecularArguments& args,
                                  IconPlatform platform = IconPlatform::Main);
std::size_t drawChicletHighlights(std::vector<float>& rgba, std::uint32_t size,
                                  const SpecularArguments& args,
                                  IconPlatform platform = IconPlatform::Main);

// A frase que vai para `out.notes`: o que foi desenhado, com que conjunto, e o
// que ficou por ler embaixo. `appearance` entra porque a classe medida e
// informacao mesmo quando ela nao muda o pixel.
std::string chicletHighlightsNote(DesignGeneration generation, HighlightsSetKind set,
                                  ChicletAppearance appearance, const ChicletLuminance& l);

}  // namespace rb
