# Spec — a mescla e o traço, e por que são **uma** frente

*2026-09-03. Escrito depois de as duas investigações de RE fecharem, não antes —
os dois pré-requisitos que este spec existia para nomear foram resolvidos, e o
que sobrou é construção.*

---

## 1. Por que juntas, e o número que obriga a isso

A régua diz que o traço bloqueia 10 documentos e que a mescla bloqueia 19. Os
dois números são de uma tabela que conta **uma camada em várias linhas**, e
somá-los responde à pergunta errada. A pergunta certa é *quantos documentos
fecham se esta família inteira cair*, e ela se responde removendo a família e
re-julgando:

| frente | +documentos | +camadas |
|---|---|---|
| traço pintado | +4 | **+30** |
| mescla (todos os modos) | +4 | +2 |
| **as duas JUNTAS** | **+14** → 47/55 (85,5%) | **+33** → 178/194 (91,8%) |
| máscara + clip-path | +2 | +2 |
| referência pendurada | +2 | +2 |
| filtro SVG | +1 | +6 |
| raster (Adam7) | +1 | +1 |
| `use` + `pattern` | **+0** | 0 |

`[ART]` **4 + 4 = 14.** Não é erro de conta: **seis documentos estão bloqueados
por traço E por mescla e por mais nada**, então nenhuma das duas frentes sozinha
os entrega. É a única combinação do corpus com esse efeito, e é a razão deste
spec cobrir duas coisas que de resto não se parecem.

E elas puxam grandezas diferentes: o traço é quase toda a régua de camada (+30
de +33), a mescla é quase toda a de documento. Fazer só uma é escolher qual
número subir.

---

## 2. A MESCLA — a tradução existe, e são dois saltos

O doc 03 §26 leu os 56 blocos do `RB::Shader::blend` e casou 39 por fórmula.
Dezesseis dos 18 nomes do formato ficaram com candidato único; **dois não**, e
eram justamente os que o corpus mais usa. Este spec fecha isso.

### 2.1. A ponte fala CoreGraphics, não RenderBox

`[BIN]` `IconRendering.arm64` **0x94AC0** (e uma cópia idêntica em **0x978F4**)
guarda **18 × uint32**, indexado pelo tag do `Icon.BlendMode`:

```
[0, 4, 1, 7, 26, 5, 2, 6, 27, 3, 8, 9, 10, 11, 12, 13, 14, 15]
```

`[BIN]` O escritor está em **0x25570–0x25580**: `adrp/add x8, 0x94AC0` ;
`ldr w2, [x8, x23, lsl #2]` ; `bl _objc_msgSend$setBlendMode:`.

**Esses 18 números são a numeração pública do `CGBlendMode`.** `normal`=0,
`multiply`=1, `screen`=2, `overlay`=3, `darken`=4, `lighten`=5, `colorDodge`=6,
`colorBurn`=7, `softLight`=8, `hardLight`=9, `difference`=10, `exclusion`=11,
`hue`=12, `saturation`=13, `color`=14, `luminosity`=15, `plusDarker`=26,
`plusLighter`=27.

> **Por que isto vale mais que um casamento por fórmula.** As 18 posições batem
> com um **cabeçalho público da Apple**, que é uma fonte inteiramente fora deste
> binário e que ninguém ajustou para caber. Dezoito acertos simultâneos contra
> uma numeração publicada não é evidência circunstancial.

### 2.2. O segundo salto, e ele também se confere sozinho

`[BIN]` `RenderBox.arm64` **0x15ED18** (`cg_table`, 28 × uint32) leva o
`CGBlendMode` ao caso do shader; `rb_blend_mode` em **0x8B4D4** é quem a indexa,
e `RB::RenderPass::set_blend_state` em **0x11A3D4** faz
`bfi w9, w8, #0x10, #0xe ; str w9, [x19,#4]` — **catorze bits a partir do bit 16
da palavra 1**, que é literalmente o `(palavra1 >> 16) & 16383` que o
`shader_blend.metal` lê. O laço fecha dos dois lados.

`[BIN]` E `RB::blend_name` em **0x110E2C** indexa uma tabela de 56 ponteiros em
**0x18E718** que dá **nome a cada um dos 56 casos**:

```
 0 copy            14 exclusion       28 lighten        42 pin_light
 1 clear           15 maximum         29 color_dodge    43 plus_lighter
 2 source_over     16 minimum         30 color_burn     44 plus_darker
 3 source_in       17 subtract_s      31 soft_light     45 darken_source
 4 source_out      18 subtract_d      32 hard_light     46 lighten_source
 5 source_atop     19 clip_copy       33 difference     47 minimum_inverse
 6 dest_over       20 clip_intersect  34 subtract       48 plus_lighter_ignore_alpha
 7 dest_in         21 clip_copy_inv   35 divide         49 plus_darker_ignore_alpha
 8 dest_out        22 clip_int_inv    36 hue            50 subtract_s_ignore_alpha
 9 dest_atop       23 accum_copy      37 saturation     51 sdf_maximum
10 exclusive_or    24 pass_through    38 color          52 sdf_minimum
11 additive        25 multiply        39 luminosity     53 sdf_minimum_inverse
12 screen          26 overlay         40 linear_burn    54 custom_normal
13 linear_dodge    27 darken          41 linear_light   55 custom_complex
```

`[ART]` **As 28 entradas da `cg_table` concordam com a numeração pública do
CoreGraphics, nome por nome** — `CG 16 clear` → caso 1 `clear`, `CG 17 copy` →
caso 0 `copy`, `CG 25 xor` → caso 10 `exclusive_or`, e assim por diante.
Duas tabelas achadas independentemente, validadas contra uma terceira fonte que
não é binária.

### 2.3. A tabela de 18, fechada

| formato | nome | CG | **caso do RenderBox** | nome do caso |
|---|---|---|---|---|
| 0 | normal | 0 | **2** | `source_over` |
| 1 | darken | 4 | **27** | `darken` |
| 2 | multiply | 1 | **25** | `multiply` |
| 3 | colorBurn | 7 | **30** | `color_burn` |
| 4 | **plusDarker** | 26 | **44** | `plus_darker` |
| 5 | lighten | 5 | **28** | `lighten` |
| 6 | screen | 2 | **12** | `screen` |
| 7 | colorDodge | 6 | **29** | `color_dodge` |
| 8 | **plusLighter** | 27 | **43** | `plus_lighter` |
| 9 | overlay | 3 | **26** | `overlay` |
| 10 | softLight | 8 | **31** | `soft_light` |
| 11 | hardLight | 9 | **32** | `hard_light` |
| 12 | difference | 10 | **33** | `difference` |
| 13 | exclusion | 11 | **14** | `exclusion` |
| 14 | hue | 12 | **36** | `hue` |
| 15 | saturation | 13 | **37** | `saturation` |
| 16 | color | 14 | **38** | `color` |
| 17 | luminosity | 15 | **39** | `luminosity` |

`[BIN]` **`plusLighter` = 43 e `plusDarker` = 44**, com três testemunhos: a
`cg_table`, o `blend_name`, e o próprio shader — o bloco compartilhado de 43/44
testa `icmp eq i32 %9, 44` e o ramo verdadeiro subtrai o excesso de alpha
(`rgb = (s+d) + (saturate(as+ab) − (as+ab))`), que é o mais escuro.

`[BIN]` **`darken` = 27 e `lighten` = 28**, confirmados: 15 e 16 são `maximum` e
`minimum`, e **não têm equivalente CoreGraphics** — `cg_blend_mode`, o inverso em
**0x161AE8**, mapeia os dois para `CG 0`.

`[BIN]` **Os 17 blocos que o doc 03 §26.5 registrava "sem casamento" ganham
nome**, e entre eles os casos 45 e 46: são `darken_source` e `lighten_source`.
A inferência do §26.4 — de que as constantes Rec.709 deles indicavam que não são
mescla de cor — estava certa, e agora tem rótulo.

### 2.4. O negativo nomeado

`[BIN]` Uma tabela direta `formato → caso do RenderBox` **não existe** em nenhuma
das sete slices. Busca literal exaustiva, byte a byte, em larguras 1/2/4/8 LE
sobre os arquivos inteiros: zero ocorrências, inclusive da subsequência
distintiva `36,37,38,39`. Método puramente literal, sem decodificador no
caminho — logo **sem a armadilha de cobertura**, e o negativo é real.

`[BIN]` E o editor expõe **10 dos 18** modos: o `IconComposerKit` referencia dez
case-witnesses de `Icon.BlendMode` e nenhuma de `colorBurn`, `colorDodge`,
`difference`, `exclusion`, `hue`, `saturation`, `color` ou `luminosity`. Os oito
restantes só chegam por `.icon` escrito fora do app. **Isso não muda a tabela** —
o caminho documento→motor passa pelo enum de 18 —, mas diz onde procurar corpus.

### 2.5. E a mescla mora no GRUPO — onde o nosso renderizador não olha

`[ART]` Medido nos 145 documentos, `blend-mode` aparece nos dois níveis, e o
grupo é onde ele é usado de verdade:

| | grupo | camada |
|---|---|---|
| `plus-lighter` | **17** | 5 |
| `overlay` | 4 | 1 |
| `darken` | 3 | 0 |
| `screen` | 2 | 8 |
| `hard-light` | 2 | 2 |
| `multiply` | 1 | 3 |
| `soft-light` | 1 | 3 |
| `plus-darker` | 1 | 3 |
| `lighten` | 1 | 0 |

**São dois problemas.** Mesclar uma **camada** é escolher a função de mescla ao
compor aquele desenho. Mesclar um **grupo** exige compor o grupo inteiro num alvo
separado e só então misturar o resultado — e é o caso majoritário do
`plus-lighter`, justamente o modo que estava sem resolver.

> **E aqui há um defeito no código já commitado.** `IconRenderer.cpp:484` lê
> `blend-mode` **só da camada**. Quando a mescla está no grupo — 12 documentos do
> corpus —, o renderizador desenha tudo em `normal` e **não reporta nada**. Não é
> lacuna nomeada: é pixel errado em silêncio, que é a classe que este projeto
> trata como pior do que não desenhar. A régua conta esses documentos como
> bloqueados porque varre a árvore inteira; o renderizador é que se cala.
>
> Isto entra como **correção**, e a correção vem antes da funcionalidade: mesmo
> que a mescla de grupo não seja transcrita nesta frente, o silêncio tem de
> acabar.

---

## 3. O TRAÇO — os bits que travavam têm nome

O doc 03 §10.3 parou aqui:

```llvm
and i32 %9, 448     ->  bits 6-8    (o braço testado é  == 128)
and i32 %9, 1536    ->  bits 9-10   (o braço testado é  == 512)
```

*"O que esses bits selecionam não foi medido."* Foi agora.

### 3.1. O nome está no inicializador estático, e é um só

`[BIN]` `default_mod69.ll` (`stroke_joins_vertex`), no `air.static_init`:

```llvm
%2 = extractelement <4 x i32> %1, i64 0
%3 = and i32 %2, 1536 ; %4 = icmp ne i32 %3, 512
%5 = and i32 %2, 448  ; %6 = icmp eq i32 %5, 128
%7 = or i1 %4, %6
store i8 ..., @__metal_implicit_fc_pred_1.83
!24 = !{ptr addrspace(2) @__metal_implicit_fc_pred_1.83, !"bool",
        !"RB::Shader::Constant::per_vertex_joins"}
```

**`per_vertex_joins = (w0 & 1536) != 512 || (w0 & 448) == 128`.**

`[BIN]` E a palavra é a **0**, sem ambiguidade: o `extractelement ... i64 0` está
na mesma função, e os seis módulos do traço carregam só o global `.0` — nenhum
referencia `.1`, `.2` ou `.3`.

### 3.2. Bits 6–8 = `RB::LineCap`, sete casos

`[BIN]` `RenderBox.arm64` **0x18F920**, via `XML::Value::LineCap::to_string`:

| valor | nome |
|---|---|
| 0 | `round` |
| 1 | `square` |
| **2** | **`butt`** |
| 3 | `outwards-triangle` |
| 4 | `inwards-triangle` |
| 5 | `forwards-triangle` |
| 6 | `backwards-triangle` |

`[BIN]` E `cg_line_cap` em **0x15EEF8** = `[1, 2, 0, 1, 1, 1, 1]`: os três
primeiros vão para `CGLineCap {1, 2, 0}`, que é exatamente `butt=0, round=1,
square=2` do CoreGraphics — os quatro triângulos, que o CG não tem, caem em
`round`. **Segunda validação contra fonte pública nesta mesma frente.**

`[BIN]` O `stroke_lines_fragment` lê o campo **inteiro**, com um `switch` de 7
casos, e cada caso é uma função de distância na região além da ponta. O `butt`
é `max(r + ov − s, d)`, que cobre só `ov ≤ s` — a faixa de um pixel do
antialiasing.

**Logo o braço `== 128` é `cap == butt`**, porque `2 << 6 = 128`.

### 3.3. Bits 9–10 = `RB::LineJoin`, três casos

`[BIN]` **0x18F958**: `0 = miter`, `1 = round`, `2 = bevel` — e
`rb_line_join(CGLineJoin)` é a identidade, mesma numeração do CoreGraphics.
O braço `== 512` é `join == round`.

### 3.4. A CPU monta os bits, e isso fecha a medição pelos dois lados

`[BIN]` `RB::(anônimo)::draw_stroke`, **0xA9324–0xA9380**:

```
ldrb w8, [x25,#4]      ; StrokeInfo.cap
and  w8, w8, #7        ; 3 bits
ldrb w9, [x25,#5]      ; StrokeInfo.join
bfi  w8, w9, #3, #2    ; 2 bits logo acima
lsl  w27, w8, #6       ; o payload inteiro entra no bit 6
orr  w8, w27, #0xe     ; FunctionType = 14 (stroke lines)
```

Cap em 6–8, join em 9–10, palavra 0. O leitor e o escritor concordam.

### 3.5. A hipótese do §11.1 está REFUTADA, e a razão vale mais que o negativo

O doc 03 §11.1 inferia que 6–8 fosse *o mesmo campo* que o estágio de path usa
para escolher entre polilinha e cúbicas — e que decodá-lo pagaria duas vezes.

`[BIN]` O `shader_path` testa o **mesmo bit 6 da mesma palavra 0**, mas
`(estado & 64) != 0` faz ele ler o buffer como `<2 x float>` (polilinha) e `== 0`
como `Path::CubicSegment`. Se fosse o mesmo campo, "polilinha" seria
`cap ∈ {square, outwards-triangle, forwards-triangle}`. É absurdo.

`[BIN]` O que explica a coincidência: **os bits 6–15 da palavra 0 são um payload
de 10 bits POR FAMÍLIA de shader**, não um mapa global. Os onze sítios
`bfi ..., #6, #0xa` do `RenderBox` inserem ali um `PrimitiveCoverageState` ou um
`AccumulatorCoverageState` — tipos nomeados e distintos —, o `draw_stroke`
escreve o seu próprio layout, e o `custom_effect` lê um terceiro. **Os bits 0–5
são o `RB::FunctionType`, e é ele que diz qual leitura vale.**

> **Consequência prática: o §10 não paga o §7.** São duas medições separadas, e a
> do §7 continua de pé por conta própria. A inferência foi registrada como `[INF]`
> e caiu quando medida — que é para isso que o selo existe.

`[BIN]` **E há uma correção de instrumento**: o §11.1 credita essas máscaras a
`shader_blend` e ao `mod100`. **Não há `shader_blend` entre os consumidores.**
São os quatro módulos do traço mais `uber_vertex` e `uber_fragment`, e os dois
uber só as têm porque **inlinam** o traço.

### 3.6. E o corpus pede exatamente o braço que travava

`[ART]` Medido nos 35 SVGs com traço pintado, em 10 documentos:

| atributo | o corpus usa |
|---|---|
| `stroke-linecap` | **ausente em todos os 35** → default do SVG: `butt` |
| `stroke-linejoin` | **ausente em todos os 35** → default do SVG: `miter` |
| `stroke-dasharray` / `dashoffset` | **ausentes em todos** |
| pintura | cor chapada sempre (31 hex, 4 `white`) — **nunca gradiente** |
| `stroke-width` | 5 valores: `1`, `5`, `0.74`, `1.75`, `1.53` |
| `stroke-miterlimit` | `22.9256`, por classe CSS, 11 ocorrências |
| `stroke-opacity` | `0.2`, 12 ocorrências |

`butt` é `LineCap` 2 → bits 6–8 = 2 → **`w0 & 448 == 128`**. `miter` é `LineJoin`
0 → bits 9–10 = 0 → `(w0 & 1536) != 512`.

**O braço `== 128`, que é exatamente onde a transcrição parou, é o único braço
que este corpus percorre.** E `per_vertex_joins` é `true` para o corpus inteiro.

`[ART]` Os 10 documentos com traço são **8 desenhos distintos**: `insidegui`
V1 e V3 têm arte idêntica, e `Apollo-Reborn__apollo` e `__AppIcon` também. Menos
duplicação que o cluster do `Flare`, que valeu +18 documentos e só 9 desenhos.

---

## 4. O que será construído

| # | tarefa | prova |
|---|---|---|
| 1 | `BlendMode.h/.cpp` — o enum de 18 do formato, a tabela para `CGBlendMode`, a `cg_table` e os 56 nomes | as 18 linhas testadas, não três pontos; e a `cg_table` conferida contra a numeração pública |
| 2 | **A correção do silêncio**: o renderizador passa a LER `blend-mode` do grupo e a nomear o que não desenha | um documento com mescla de grupo produz nota, hoje não produz |
| 3 | as fórmulas dos modos que o corpus usa, na CPU, contra o oráculo | `screen`, `plus-lighter`, `plus-darker`, `hard-light`, `overlay`, `multiply`, `soft-light`, `darken`, `lighten` |
| 4 | a mescla de **camada**, ligada no compositor | as camadas que só a mescla trava passam a desenhar |
| 5 | a mescla de **grupo**: alvo separado por grupo, depois mistura | o caso majoritário do `plus-lighter` |
| 6 | `StrokeGeometry` — o quad de linha com `cap` e `join` do estado | os 7 caps e os 3 joins, contra a forma fechada |
| 7 | ler a regra da CPU que emite o fluxo de pontos (`StrokeLines::lineto`, `closepath`, `finish_subpath`) | sem isso a GPU é transcrita e alimentada com um buffer chutado |
| 8 | ligar o traço no conversor de SVG: largura, opacidade, miterlimit, pintura chapada | os 35 SVGs do corpus |
| 9 | mutações no gate para tudo acima | cada guarda morde |
| 10 | re-medir o alcance | número novo, medido |

---

## 5. O que este spec NÃO cobre

| | por quê |
|---|---|
| o **nome** do bit 11 do traço | `[BIN]` o comportamento está medido — `0` dá antialiasing por smoothstep sobre um pixel, `1` dá cobertura binária dura, e vem do byte `StrokeInfo+0xC`. `[OBS]` O rótulo não apareceu em nenhum `air.static_init` varrido |
| `stroke_joins_fragment` e as fórmulas do miter | `[OBS]` a ramificação foi lida; as fórmulas do `switch` de `vid`, não |
| `RB::FunctionType` | `[OBS]` não tem tabela de nomes no binário. Sabe-se traço-linhas = 14 e partículas = 16 |
| cap e join que o corpus não usa | `[BIN]` os sete caps estão lidos e serão transcritos, mas **nenhum documento real os exercita** — o mesmo estado da refração do vidro, e será dito no cabeçalho do teste |
| dash | `[OBS]` nenhuma ocorrência no corpus, e nada foi lido sobre ele |
| o caso 56 do shader de mescla | `[OBS]` o `switch` lista `i32 56`, mas `blend_name` guarda em `w0 ≤ 55`. Irrelevante para os 18 |
| `custom_table` (0x15ED88) e os modos 1000+ | `[OBS]` despejada, não rastreada. É por onde `additive`, `linear_burn`, `sdf_*` entram — **nenhum pelo formato** |

---

## 6. Como saber que acabou

- O gate passa com a varredura completa, no worktree, e as mutações novas mordem.
- `slice-reach` mostra **178 camadas e 47 documentos** — medido, não estimado.
- Um documento com `blend-mode` no grupo **não sai mais silencioso**: ou desenha
  a mescla, ou nomeia o que não fez.
- O doc 03 ganha a correção do §11.1 (o consumidor não é o `shader_blend`) e a
  refutação da inferência do mesmo parágrafo.
- Cada lacuna do §5 está nomeada no relatório do `icrender`, com o motivo.

---

## 8. O resultado, medido

*2026-09-04. As dez tarefas rodaram; isto é o que elas entregaram.*

`[ART]` A régua, medida contra a anterior tirada do próprio git:

| régua | antes | depois | o §6 prometia |
|---|---|---|---|
| camadas | 145 (74,7%) | **169 (87,1%)** | 178 |
| documentos | 33 (60,0%) | **40 (72,7%)** | 47 |
| suíte | 506 casos | **515 casos, 0 falhas** | — |
| mutações | 247 | **267** | — |

### 8.1. O déficit é EXATAMENTE a recusa que este spec não previu

`169 + 9 = 178`. `40 + 7 = 47`.

Os nove camadas e os sete documentos que faltam são **todos** "mescla de grupo
sobre vidro". Os números do §6 estavam certos para as duas frentes tomadas
separadamente; o que ninguém previu foi a **interação** entre elas.

`[OBS]` Um grupo que mescla e carrega vidro não é desenhado. O vidro refrata o
seu fundo: num alvo próprio esse fundo está vazio, e na tela a mescla misturaria
o fundo duas vezes. Qual das duas o alvo faz **não foi lido** — a pergunta nem
existe até um grupo ganhar buffer próprio, e nenhuma medição dela existe.

`[ART]` Custa 8 documentos, entre eles as seis variantes do
`insidegui/AssetCatalogTinkerer` e o `RuntimeViewer`. **O custo é o argumento:**
a alternativa compra alcance com um quadro que ninguém mediu.

### 8.2. Dois defeitos que o teste achou e a leitura não

**O primeiro é do renderizador.** Uma forma com `fill="none"` era descartada
**antes** de o traço ser considerado — e `fill="none"` com traço é a maneira
normal de desenhar uma linha. Todo desenho só-de-traço saía em branco, com
relatório limpo. O teste ponta a ponta é um `cross.svg` cuja única tinta é o
traço, e ele veio vazio.

**O segundo é meu.** A sonda do teste estava no lugar errado: o viewBox de 512
cai num canvas de 1024 pontos, então a arte ocupa metade do alvo e os braços da
cruz vão de 24 a 40 px, não de 16 a 48. **Sonda errada e renderizador errado dão
o mesmo vermelho**, e só a segunda leitura separa os dois.

### 8.3. E o gate reprovou os meus testes, duas vezes

`[ART]` Quatro das cinco mutações do compositor **sobreviveram** na primeira
rodada: os testes contavam camadas desenhadas e nunca olhavam a imagem. Um grupo
cujo resultado é jogado fora ainda reporta `drawn == 1`.

Na segunda rodada sobreviveram duas, por razões diferentes:

- a do **grupo**, porque eu afirmava `pr > nr + 0,05` e a versão com o
  acumulador premultiplicado duas vezes também passa desse limiar — **1,25
  contra o 1,5 correto, com 1,0 de base**. Um limiar de "maior" não é um
  oráculo.
- a da **camada**, porque o fixture punha a mescla no grupo, e `blendOver`
  desvia para `over` quando o modo é `Normal` — o código mutado nem rodava.

E uma quinta era **mutante equivalente**: eu tinha escrito que pular origem com
alpha zero quebraria `plus-darker`. Está errado, e a análise de caso mostra — a
cauda vira `d`, todo termo `B` carrega fator `as`, e a folga do par plus é
`saturate(ab) − ab = 0`. Pular e misturar escrevem o mesmo número nos nove
modos. Classificada por análise e não por chute, trocada por uma observável, e a
ausência ficou registrada na lista do gate com o motivo.

### 8.4. A ressalva da régua cresceu de novo

`[ART]` **Das 169 camadas desenháveis, 65 desenham sem o material que o
documento pede** — eram 51. Subiu porque mais camadas de vidro ficaram
alcançáveis, não porque algo regrediu.

### 8.5. As correções que este trabalho carrega

1. **O doc 03 §10.2 estava errado por um índice.** Publicava
   `min(join[iid], join[iid+2])`; é `join[iid+1]` e `join[iid+2]`. Com o índice
   publicado, **o primeiro segmento de todo subpath seria descartado**, porque o
   ponto `iid` é o fantasma que carrega o `-3`. O erro só apareceu quando o
   layout que a CPU emite foi lido e não fechou com o que a linha afirmava.
2. **A inferência do §11.1 caiu.** Os bits 6–8 do traço **não** são o mesmo
   campo que escolhe polilinha × cúbicas no path: os bits 6–15 da palavra 0 são
   payload **por família de shader**, e os bits 0–5 dizem qual leitura vale. O
   §10 não paga o §7.
3. **O §11.1 creditava as máscaras a `shader_blend`.** Não há `shader_blend`
   entre os consumidores — são os quatro módulos do traço mais os dois *uber*,
   que só as têm porque **inlinam** o traço.
4. **O §24.3 já tinha sido corrigido** pelo trabalho do `automatic`, e o §26.5
   ganhou nome para os 17 blocos que registrava sem casamento.
