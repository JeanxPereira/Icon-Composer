# Laudo — o oráculo: o `.icon` da Apple, e os nossos pixels contra os dela

*2026-09-15. Frente: extração, reconstrução e medida. O projeto tinha 145
documentos de corpus e **zero imagens de gabarito** — toda afirmação de pixel era
"o binário diz que a fórmula é esta, e nós a transcrevemos", nunca "a saída da
Apple e a nossa foram comparadas". Este laudo troca isso por um número.*

Fontes (`References/` é gitignored, só leitura):
`References/27.0-129/extracted/Icon Composer.app/Contents/Resources/Assets.car`
e `.../AppIcon.icns`. Saídas em `References/27.0-129/out/`.
Instrumentos novos e versionados: `scripts/car_extract.py`,
`scripts/car_to_icon.py`, `scripts/png-diff.py`.

---

## 0. O resultado, em cinco linhas

`[BIN]` O `Assets.car` do Icon Composer 27.0 (129) guarda **o documento `.icon`
compilado E a saída que a própria Apple rasterizou dele**. Os dois foram
extraídos.

`[BIN]` Reconstruído o bundle e alinhado o conteúdo, **o fundo bate em ±1 nível
de 255 ao longo de toda a rampa** e **as pastilhas de vidro erram 29,6 níveis em
média (11,6 %), com 99,98 % dos pixels diferentes**.

`[BIN]` E apareceu uma coisa que nenhuma transcrição tinha pego: **a saída da
Apple põe o chiclet em 824/1024 do quadro e desenha uma sombra externa em volta.
Este renderizador desenha de borda a borda e não desenha sombra externa nenhuma.**

---

## 1. Onde o documento estava escondido: a TLV, não o payload

`car_dump.py` lista as renditions; o conteúdo não está onde o nome sugere.

`[BIN]` As renditions `AppIcon.iconstack` (layout 1019) e `IconGroup`
(layout 1020) têm payload `RAWD` de **comprimento zero** — os doze bytes
`44 57 41 52 00000000 00000000` e nada mais. **O grafo de camadas inteiro mora na
seção TLV do `csiheader`**, entre os 184 bytes do cabeçalho e o payload.

A seção é uma sequência de `u32 tag, u32 len, bytes`. As tags medidas:

| tag | o que é | forma |
|---|---|---|
| `0x3f4` (1012) | **filhos** | `u32 count` + `count × 48`: 8 float (só o `[7]` usado — opacidade), `u32 0x10`, 12 bytes de chave de rendition |
| `0x3fc` (1020) | **tipo de nó / preenchimento** | `u32 count, u32 pad` + `[u32 kind][u32 flags][u32 strlen][char str[]]` |
| `0x3fd` (1021) | **efeito A** | `u32 count, u32 pad` + `count × 20`: `[u32][f32][u32][f32][u32]` |
| `0x3fe` (1022) | **efeito B** | `u32 count, u32 pad` + `count × 12`: `[f32][f32][u32]` |
| `0x3ec` (1004) | escala | `[f32][f32]` = `0.0, 1.0` |
| `0x3ed` (1005) | UTI | `public.layeredimage` |
| `0x3ef` (1007) | bytes por linha (só em bitmap) | `u32` |

`[BIN]` A chave de 12 bytes de cada filho são **três pares `u16` (atributo,
valor)** e casam com um `FACETKEY` do mesmo catálogo menos o par `(0,3)`, que é
constante. É assim que o grafo se resolve por nome:

```
AppIcon.iconstack
  ├─ AppIcon_Assets/system-dark      (kind 0 = fundo)
  ├─ AppIcon/Group 3   → 2.gray1,  4.gray2     (kind 2 = grupo)
  ├─ AppIcon/Group 2   → 6.green1, 8.green2
  └─ AppIcon/Group     → 10.blue1, 12.blue2
```

`[BIN]` **Aqua e DarkAqua são byte a byte idênticos** nas nove renditions de
grupo e nas três de stack. Só `ISAppearanceTintable` difere, e difere em duas
coisas: as opacidades de grupo passam a `1,0 / 0,9 / 0,7` (da frente para o
fundo) e o filho `12.blue2` ganha um nome de cor, `AppIcon_Assets/Color-4`.

---

## 2. A arte e o fundo

`[BIN]` Os seis `image.svg` são `RAWD` com um stream **LZFSE** (`bvxn` = LZVN)
dentro. Descomprimidos: seis SVG de `viewBox="0 0 1024 1024"`, um `<path>` e um
`<linearGradient>` cada, saídos do *Adobe Illustrator 29.0.0 SVG Export
Plug-In*, com `<g id="blue1">`, `id="green2"` etc.

`[BIN]` O fundo é a rendition `ARGG` (layout 1021) — um gradiente linear de duas
paradas, `offset 0.0 → AppIcon_Assets/Color-2` e `offset 1.0 →
AppIcon_Assets/Color-3`, e as duas cores estão no mesmo catálogo como `double`
exatos: **cinza 0,192** e **cinza 0,078**.

`[BIN]` E isso se confirma sozinho no bitmap: em `(256, 60)` a saída da Apple lê
`0,1924` e em `(256, 430)` lê `0,0791`. As duas pontas do ARGG, medidas nos
pixels.

---

## 3. O gabarito: `CELM` é uma CADEIA de `KCBC`, não um stream

`[BIN]` As duas renditions `AppIcon256x256_…png` (layout 12, 512×512, scale 200)
são `CELM` com compressão 4. O corpo **não é um stream LZFSE único**: é uma
cadeia de registros

```
'KCBC'(4) u32 flags u32 zero u32 nLinhas u32 nComprimido + nComprimido bytes de LZFSE
```

faixas de 170 linhas, quatro registros para 512 linhas. Tratar o corpo como um
stream só devolve a **primeira faixa** — um terço da imagem, e sem erro nenhum.

`[BIN]` As duas variantes da mesma imagem diferem na chave na posição do token
24 e **não têm a mesma ordem de canal**:

| `0x3ef` | formato | ordem |
|---|---|---|
| `0x800` = `w×4` | 8 bits por canal | **BGRA** premultiplicado |
| `0x1000` = `w×8` | half-float (binary16) | **RGBA** premultiplicado |

`[BIN]` A função de transferência é a mesma nas duas: em `(100,100)` a de 8 bits
dá `48` e a de 16 dá `0,1882`, e `0,1882 × 255 = 48,0`. A de 16 bits é só
precisão, não espaço linear.

`[OBS]` O `.icns` **não cruza** com isso: ele tem `ic13` (256 px), `ic07` (128) e
`ic11` (32) — **não tem 512**. Os dois caminhos não se sobrepõem em tamanho
nenhum, então não há confirmação byte a byte entre `.car` e `.icns`; o que há é a
confirmação de §7, que é normalizada.

---

## 4. O que foi suposto — e isto limita tudo o que vem depois

O bundle reconstruído está em `References/27.0-129/out/AppIcon-27.icon/`, com a
lista abaixo repetida em `inferences.json` ao lado dele.

| `[INF]` | o que foi suposto | apoio |
|---|---|---|
| ordem | o array compilado é **trás→frente** e foi invertido | as opacidades do recorte Tintable caem `1,0 / 0,9 / 0,7` na direção do fundo, e a numeração dos assets (2,4 / 6,8 / 10,12) sobe junto |
| mapa de `0x3fd` | `[specular, shadow.opacity, shadow.kind, translucency.value, translucency.enabled]` | **adjacência**: `(opacity,kind)` juntos e `(value,enabled)` juntos. **Não** foi escolhido por diferença de pixel |
| mapa de `0x3fe` | `[blur-material, SEM NOME, lighting]` | o segundo float (`0,0` / `0,02`) fica sem nome |
| `shadow.kind` | o compilado diz `3`; adotado `neutral` | a tabela de `Values.cpp` poria `none` em 3, e um ícone com sombra visível não diz `none` |
| `glass` | `true` em toda folha | nenhum bit de vidro foi achado no compilado; a saída da Apple tem realce e translucidez |
| `position` | identidade | os 7 floats antes da opacidade são zero em todos os registros |
| `fill` de camada | omitido | o campo de nome do `0x3fc` vem vazio nas folhas em System |
| espaço de cor | `display-p3` para SVG sem tag | `[ART]` 20 dos 145 documentos do corpus declaram a chave, e os 20 dizem `display-p3` |
| nomes de arquivo | `10.blue1.svg` etc. | o catálogo guarda o nome do FACET, não o nome que o autor importou |

A leitura rival de `0x3fd` — `[specular, blur-material, shadow.kind,
shadow.opacity, translucency.enabled]` — **não foi descartada**. `[OBS]` Ela não
se decide com este catálogo: seria preciso um segundo `.car` cujo `icon.json`
fonte também se conheça. Decidir entre as duas pelo diff seria fabricar o
oráculo, e por isso não foi feito.

---

## 5. O primeiro obstáculo: o leitor de SVG, não o renderizador

`[BIN]` Os seis SVG saem do Illustrator com o `<linearGradient>` **dentro do
`<g>` da camada**, logo antes do `<path>`. Pelo SVG isso é legal — um gradiente é
referenciado por id, esteja onde estiver. Mas em `Source/CoreSVG/Document.cpp` o
`collectGradient` só é chamado **na cadeia do `else if (e.name == "defs")`**. Um
gradiente fora de `<defs>` cai no ramo final, que "nomeia e não entra".

Resultado com os assets como a Apple os gravou: `6 of 6 layer(s) drawn` e, no
stderr, seis vezes

```
url(#SVGID_1_) nao resolve para nenhum gradiente do documento
```

— a arte desenha **sem cor nenhuma**. O ícone sai preto com um contorno.

`[BIN]` Este é o primeiro defeito que o oráculo pegou, e ele não é do
renderizador: é do leitor. `car_to_icon.py --hoist-gradients` move o elemento
para um `<defs>` no topo sem tocar em atributo nenhum — reescrita neutra — e é
com ela que as medidas de §6 foram feitas, para que o número seja sobre o
renderizador e não sobre o leitor.

---

## 6. O DIFF

`icrender` em Release, `--appearance dark`, contra
`References/27.0-129/out/apple-512.png` (o `CELM` de 8 bits).

### 6.1 Direto, 512 contra 512

```
pixels visíveis (alpha>0 num dos dois): 246854  (94,2 % do quadro)
pixels diferentes:                      238019  (96,42 % dos visíveis)
delta máximo    R 250  G 250  B 251  A 255
delta médio     R 32,8 G 40,9 B 46,5  A 85,9
blocos 8×8 com média ≥ 8: 3170 de 4096 (77,4 %)
```

`[BIN]` E a causa está no alfa, não na cor. As caixas medidas:

| | corpo (`α>127`) | com sombra (`α>0`) |
|---|---|---|
| **Apple, 512** | `412×412` em `(50,50)` | `448×448`, `x[32,479] y[36,483]` |
| **nosso, 512** | `512×512` | `512×512` |

`[BIN]` **412/512 = 0,8047 = 824/1024.** A saída da Apple põe o chiclet no
retângulo interno de 824 pontos de um canvas de 1024 e usa os 100 pontos que
sobram para uma sombra externa, deslocada `+2 px` em y. Este renderizador desenha
o chiclet de borda a borda e não desenha sombra externa.

`[BIN]` `--idiom macOS` **não muda nada**: medido, a linha central continua
`α>127` de `x=0` a `x=511`. O recuo não está implementado sob nenhum idioma.

### 6.2 Alinhado, para medir o conteúdo

Render a `--size 412` e posto em `(50,50)` num quadro de 512 — cópia pixel a
pixel, **sem reamostrar**, então nenhum erro de filtro entra na conta.

```
pixels visíveis: 189993
pixels diferentes: 160426  (84,44 % dos visíveis)
delta máximo    R 235  G 215  B 248
delta médio     R 11,5  G 11,3  B 10,5   A 4,9
```

`[BIN]` **99,8 % da soma do erro está a mais de 3 px da silhueta.** A borda não é
a história — o corpo é.

### 6.3 Onde, separando fundo de pastilha

O fundo do ícone é cinza (`R==G==B`) e toda a arte é cromática, então a própria
saída da Apple separa os dois sem arbitragem nossa:

| região | px | diferentes | delta médio | máximo |
|---|---|---|---|---|
| **fundo** | 93 325 | 68,3 % | **5,10** | 158 |
| **pastilhas** | 65 920 | **99,98 %** | **29,56** | 235 |

E a rampa do fundo, lida na coluna `x=70`, fora das pastilhas:

```
 y     Apple   nosso   Δ
134     45      46     -1
186     41      42     -1
238     36      36      0
290     31      31      0
342     27      26     +1
394     22      23     -1
```

`[BIN]` **O fundo bate em ±1 nível de 255 ao longo de toda a rampa.** O gradiente
de duas paradas com a interpolação Hermite/smoothstep que a frente da rampa
mediu está certo, confirmado agora contra a saída da Apple e não só contra o
binário.

`[BIN]` O erro é o **vidro**: 29,6 níveis de média nas pastilhas, contra 5,1 no
fundo.

### 6.4 O mapa

`References/27.0-129/out/map-alinhado.png`, média do delta por bloco 16×16. Os
dez piores blocos são, todos, **os quatro cantos do chiclet** (`(416,64)`,
`(80,64)`, `(80,432)`, `(432,416)`, …), com média 120–128. Quatro cantos acesos
simetricamente não é translação — é **forma de canto**. `[BIN]` E a medida
pontual concorda: em `(70,420)` a Apple ainda é opaca e nós já somos `α=0`, isto
é, **o nosso canto entra mais cedo que o dela**.

`[OBS]` Se isso é o raio (266,24 num canvas de 1024) ou o regime da curva
contínua não se decide aqui; o que está medido é que a nossa silhueta de canto
não é a dela.

Fora dos cantos, o vermelho do mapa desenha **o contorno de cada pastilha** — o
realce de aresta. E o fundo mostra listras horizontais alternando preto (igual) e
verde (Δ pequeno): a rampa acerta o valor e erra a **posição da banda de
quantização**, que é exatamente o que um dither ausente produz.

---

## 7. A classe de tamanho: medida, e ela não morde

`[BIN]` O `.icns` dá três tamanhos e o `.car` dá o quarto. Geometria:

| | px | corpo | razão |
|---|---|---|---|
| `.car` | 512 | 412×412 | 0,8047 |
| `ic13` | 256 | 206×206 | 0,8047 |
| `ic07` | 128 | 102×102 | 0,7969 |
| `ic11` | 32 | 26×26 | 0,8125 |

`[BIN]` **824/1024 vale nos quatro**, com 128 e 32 dentro do arredondamento de um
pixel (`102,2` e `25,75`).

E a rampa do fundo, lida em **fração da caixa** para ser comparável:

| tamanho | 0,10 | 0,20 | 0,30 | 0,70 | 0,80 | 0,90 |
|---|---|---|---|---|---|---|
| 512 | 49 | 45 | 42 | 27 | 23 | 20 |
| 256 | 49 | 45 | 42 | 27 | 23 | 20 |
| 128 | 49 | 45 | 41 | 27 | 23 | 20 |
| 32 | 49 | 44 | 41 | 26 | 22 | — |

`[BIN]` **Idênticas numa faixa de 16× de tamanho**, dentro de ±1 no menor.

Isto é o que esta frente pode dizer sobre a armadilha `valor[3 − classe]` das
tabelas `SizeBasedValue`: **o fundo deste ícone não a exercita em nenhum dos
quatro tamanhos**. O projeto supunha isso porque os quatro valores da tabela são
iguais nesta versão; agora é medida, não suposição. `[OBS]` Continua sem
exercitar — um ícone cujos quatro valores fossem diferentes morderia, e ele não
está neste catálogo.

---

## 8. O que sobra `[OBS]`

1. **O recuo de 824/1024 e a sombra externa** não existem neste renderizador, em
   nenhum idioma. É a maior diferença única, e é geométrica.
2. **A forma do canto do chiclet** difere: o nosso entra mais cedo. Quatro cantos
   acesos simetricamente no mapa, e um ponto medido em `(70,420)`.
3. **O mapa de campos de `0x3fd`** tem duas leituras vivas (§4). Não se decide
   com um catálogo só.
4. **O segundo float de `0x3fe`** (`0,0` no grupo de trás, `0,02` nos outros dois)
   não tem nome.
5. **Nenhum oráculo de aparência.** O `.car` tem o documento em três aparências
   mas **um** bitmap rasterizado (System). Aqua e DarkAqua são idênticos no
   compilado, então mesmo que houvesse dois eles não separariam nada.
6. **O leitor de SVG recusa gradiente fora de `<defs>`** (§5). É um defeito de
   uma linha de alcance e atinge toda arte exportada pelo Illustrator — mas
   consertá-lo é de outra frente, e esta não entrou em `Source/`.
7. `[OBS]` A rendition `AppIcon` (layout 1010, payload `SISM`, 24 bytes) e os
   campos `flags` do `0x3fc` não foram decodificados.

---

## 9. Como refazer

```
python scripts/car_extract.py "<...>/Assets.car" --out References/27.0-129/out/car
python scripts/car_to_icon.py References/27.0-129/out/car \
       --out References/27.0-129/out/AppIcon-27-defs.icon --hoist-gradients
icrender References/27.0-129/out/AppIcon-27-defs.icon --out ours-412.png \
       --size 412 --appearance dark
python scripts/png-diff.py apple-512.png ours-412-em-512.png --map mapa.png
```

`car_extract.py` usa `liblzfse` quando ele está instalado; sem ele os payloads
`bvx-` (não comprimidos) ainda saem e os demais são reportados como
`lzfse-sem-decoder` em vez de virem errados em silêncio.
