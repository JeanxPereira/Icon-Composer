# Laudo — o anel da sombra

*2026-09-15. Frente: fechar o item 5 do `[OBS]` do laudo-mãe — a **geometria** da
forma de recorte que `Shadow.ringWidth` alimenta. Entradas:
`Docs/Laudos/2026-09-15-sombra.md` (§5.1, passo 4) e
`Docs/Laudos/2026-09-15-sombra-desenho.md`, que fez a sombra chegar ao pixel
horas antes desta.*

O laudo-mãe parou exatamente aqui:

> **`ringWidth`.** A largura é lida, escalada e negada (`0x20C8C`), e vira uma
> forma de recorte via `0x11C40`. A geometria dessa forma não foi transcrita.

E `Shadow.ringWidth` vem **preenchido por padrão** nesta versão — `[16,16,16,16]`,
tag `0`, em `0x5EC58`. Ou seja: **toda** sombra que este renderizador desenhava
estava faltando um passo, e a `kShadowRingNote` gritava isso em cada render.

Agora não está. `0x11C40` foi lido inteiro (1604 bytes), e com ele dois trechos
do `RenderBox.arm64` que decidem o sinal e a forma da rampa.

Endereços do slice `References/2.0-125/out/slices/IconRendering.arm64`
(VA == offset de arquivo), salvo quando o texto disser `RenderBox.arm64`.

---

> **ERRATA — 2026-10-01. As §0 e §3 estão ERRADAS na conclusão, e o resto do
> laudo se mantém.** A leitura de `0x893A4` (uma escala e um viés) está certa;
> o que se tirou dela — "monótona, logo não é coroa" — não. A escala e o viés
> são só o `t`. O que o *fragment* faz com `t` não tinha sido lido, e é
> (`alpha_effect`, `default_mod66.ll` %46–%61 do RenderBox):
>
> ```
> a   = saturate(t / fwidth + 0.5)
> b   = saturate((t − 1) / fwidth + 0.5)
> out = cor × (a − a·b)            ; bits 9–11 do estado zerados
> ```
>
> um para `0 ≤ t ≤ 1`, zero **dos dois lados**. A variante de um degrau só
> (`out = cor × a`, bit 9) é escolhida apenas quando `maxAlpha = +inf`
> (`0x8952C`–`0x89548`), e `0x11D08`–`0x11D64` passa um `maxAlpha` finito.
> **O anel é um anel:** a fonte da sombra é a faixa de `ringWidth` para dentro
> do contorno, e o miolo da camada não projeta sombra. A hipótese da coroa, que
> a §0 dá como derrubada, era a certa.
>
> No código: `shadowRingMask` e `icon_shadow.comp` passaram a
> `1 − clamp(profundidade − ringWidth + 0,5, 0, 1)`; o caso de teste da barra
> 41×41 da §8 agora checa a banda (`glass_shadow_ring_is_a_band_inside_the_outline`).
> A tabela de pixels da §9 descreve a leitura antiga. `[OBS]` `fwidth` é
> `|ddx| + |ddy|`, de 1 a √2 pixels conforme a direção do contorno; o código
> usa 1.

---

## 0. O resultado, em uma linha

`[BIN]` O "anel" **não é um anel**. É uma rampa linear de máscara, monótona na
distância, que vale `0` sobre o contorno da própria camada e sobe até `1` a
`ringWidth` pontos **para dentro** dele:

```
mask(p) = clamp( profundidadeDentro(p) / (ringWidth[3−k] × s), 0, 1 )
```

com `s = min(L,A)/1024`. O `maxDistance` do SDF, que aparece nas duas pontas da
conta, **se cancela**.

A hipótese que chegou nesta frente — *"largura negativa alimentando um recorte
tem cara de inset; o anel seria a coroa entre o contorno e o contorno
encolhido"* — está **meio derrubada e meio confirmada**:

- **confirmada** no lado do *inset*: a banda afetada fica de fato **dentro** do
  contorno, e a negação em `0x20C8C` é o que a coloca lá;
- **derrubada** no lado da *coroa*: uma coroa é não-nula numa faixa e nula dos
  dois lados dela, e o filtro que constrói essa máscara é **uma escala e um
  viés** (§3), portanto uma função **monótona** da distância. Ele não consegue
  descer de volta. Não há aro; há um esfumado de um lado só.

Essa distinção não é acadêmica. Desenhar a coroa plausível apagaria a sombra
inteira no miolo do glifo e deixaria só um contorno — o oposto do que o binário
faz.

---

## 1. `0x11C40` é `0x10E1C` com dois filtros em volta

`[BIN]` A primeira coisa que `0x11C40` revela é que ele **não é** um construtor
de forma. Ele tem a **mesma assinatura** do helper genérico `0x10E1C` — "desenhe
este SDF dentro deste rect" — e repassa todos os quatorze argumentos, mudando
três:

| argumento | recebido | repassado a `0x10E1C` |
|---|---|---|
| `d5` (a largura) | `−(ringWidth[3−k] × s)` | **`1.0`** (`0x12144`) |
| `x2` (uma cor) | `&0xCDE70` | **`0`** (`0x12154`) |
| `w5` (uma flag) | `(ctx == 2)` | **`0`** (`0x12160`) |

`0x1216C` é a chamada. Os outros onze — `d0..d4`, `x0`, `x1`, `w3`, `x4`, `x6`,
`x7` — saem como entraram, guardados na pilha em `0x11CDC`–`0x11CE8` e relidos em
`0x1213C`–`0x12168`.

Entre `save` (`0x11CB8`) e `restore` (`0x12174`) ele instala **exatamente dois
filtros**, e nada mais:

### 1.1. O filtro de limiar de alpha

`[BIN]` `addAlphaThresholdFilterWithMinAlpha:maxAlpha:color:colorSpace:` em
`0x11D64`, com `colorSpace = 3` e cor vinda do global preguiçoso `0xCDE70`. O
inicializador `swift_once` dele é `0x3DAA0`, três instruções:

```
0x0003DAA0  adrp  x8, #0xcd000
0x0003DAA4  add   x8, x8, #0xe70
0x0003DAA8  fmov  v0.2d, #1.00000000
0x0003DAAC  stp   q0, q0, [x8]
```

Quatro `1.0`: **branco opaco**. (O vizinho `0x3DAD4` escreve `(0,0,0,1)` em
`0xCDE98` — o preto que o ramo `neutral` da §5.1 já usa. Duas cores, dois
globais, a mesma forma.)

### 1.2. O filtro de matriz de cor

`[BIN]` `addColorMatrixFilterWithArray:flags:0` em `0x12138`. Os vinte `Float`
são montados em `0x11DA0`–`0x11EE8` a partir de quatro arrays de cinco lidos da
tabela em **`0xCE900`** (`madd x19, x21, #0x14, x20` com `x20 = 0xCE8D8`, e os
`ldur q0,[x19,#0x28]` + `ldr s8,[x19,#0x38]` juntam cinco floats contíguos).
Despejando os bytes:

```
linha 0  [0, 0, 0, 0, 0]
linha 1  [0, 0, 0, 0, 0]
linha 2  [0, 0, 0, 0, 0]
linha 3  [1, 0, 0, 0, 0]
```

Dezenove zeros e um `1.0` no índice 15. Numa matriz 4×5 sobre `(r,g,b,a,1)` isso
é `out.rgb = 0`, `out.a = in.r`: **move o vermelho pré-multiplicado para o
alpha e zera a cor**. É a jogada que transforma uma rampa branca-sobre-transparente
numa máscara de alpha pura — que é justamente o que o
`clipLayerWithAlpha:mode:` de `0x20CE8` consome em seguida.

A dupla é autoconsistente: o filtro 1.1 emite *branco × t*, o filtro 1.2 lê o
canal vermelho desse branco (que é `t`) e o põe no alpha. Nenhum dos dois é uma
forma.

---

## 2. A banda, e por que `maxDistance` some

`[BIN]` `0x11CF8`–`0x11D3C`:

```
0x11CF8  scvtf d0, x20            ; x20 = sdfTexelsW − 2
0x11CFC  fdiv  d0, d0, d9         ; ÷ rectW  (d9 = 3º Double recebido)
0x11D00  fmul  d0, d12, d0        ; × d5 = −(ringWidth × s)     -> u, em texels
0x11D08  fminnm d2, d0, d1        ; min(u, 0) = u
0x11D0C  fmov  d3, #-2.0
0x11D10  fmul  d3, d13, d3        ; −2 × d0recebido
0x11D14  fdiv  d2, d2, d3
0x11D1C  fadd  d2, d2, d4         ; + 0.5
0x11D20  fcmp  d0, #0.0
0x11D24  fcsel d0, d0, d1, ge     ; max(u, 0) = 0
0x11D28  fdiv  d0, d0, d3
0x11D2C  fadd  d0, d0, d4         ; + 0.5
0x11D30..0x11D44                   ; minAlpha = min(par), maxAlpha = max(par)
```

`x20` sai de `0x11CD0`–`0x11CD4`: o primeiro `Int` devolvido por `0x84304`
(largura do SDF em texels) **menos 2**. O `−2` é a borda de um texel de cada lado
— a mesma que `0x10E1C` desfaz com `scaleByX: rectW/(sdfW−2)` e
`translateByX:−1 Y:−1` em `0x10F48`–`0x10F6C`.

`u ≤ 0`, então:

```
minAlpha = 0.5
maxAlpha = 0.5 + ringWidthEmTexels / (2 × d0recebido)
```

`[BIN]` **E `d0recebido` é o `maxDistance` do SDF da camada.** O primeiro
`Double` do chamador (`0x20B14`) vem do argumento de pilha em `[x29+0x38]`,
imediatamente depois dos dois words em `[x29+0x28]`/`[x29+0x30]` que viram `x0` e
`x1` da consulta de tamanho `0x84304`. A reflexão fecha o nome: `0xA2FF4` lista
um `[struct]` de dois campos — `texture`, `maxDistance Sd` — e ele ocupa
`FinalizedIcon.Layer+0x98..0xB0` (24 bytes, três words), exatamente o trio
`{x0, x1, Double}`. É o `IconRendering.SDF` que o campo `sdf` do `Layer` declara.

Dividir por `2 × maxDistance` só é a conversão certa numa codificação em que a
faixa inteira `±maxDistance` mapeia para `[0,1]` — o que a §4 confirma por outro
caminho.

---

## 3. O filtro é uma remapeação linear grampeada, não um passa-banda

Esta é a pergunta que decide tudo, e ela **não** se responde no `IconRendering`:
o `addAlphaThresholdFilter…` é um seletor, a implementação está no RenderBox. Só
que o `RenderBox.arm64` — ao contrário do `IconRendering` — **mantém os símbolos**
(11.996 deles).

`[BIN]` `RenderBox.arm64`, `RB::_GLOBAL__N_1::render_(AlphaThresholdEffect
const&, RenderPass&, Shader::AlphaEffectGlobals&, …)` em **`0x893A4`**:

```
0x000893F8  ldr   d1, [x23]        ; v1.s[0] = minAlpha, v1.s[1] = maxAlpha
0x000893FC  mov   s2, v1.s[1]
0x00089400  fmaxnm s2, s1, s2
0x00089404  fsub  s1, s2, s1       ; maxAlpha − minAlpha
0x0008940C  fdiv  s1, s2, s1       ; scale = 1 / (maxAlpha − minAlpha)
0x00089410  str   s1, [x25, #0x44]
0x00089414  ldr   d1, [x23]
0x00089418  fneg  s1, s1
0x00089420  fmul  s1, s1, s2
0x00089424  str   s1, [x25, #0x48] ; bias = −minAlpha × scale
```

Uma escala e um viés, nos globais de shader em `+0x44`/`+0x48`:

```
t = (alpha − minAlpha) / (maxAlpha − minAlpha)
```

e a saída é *a cor do filtro × t* (a cor passa por `RB::Fill::Color::prepare` em
`0x89464`, depois de ter o alpha multiplicado pelo alpha da chamada em
`0x89440`). O grampo em `[0,1]` não é uma instrução deste trecho: ele é o
formato do alvo — a camada é escrita num `unorm` e `clipLayerWithAlpha:` lê
cobertura.

**Uma escala e um viés é uma função monótona.** É o que derruba a coroa: nenhuma
escolha de `minAlpha`/`maxAlpha` faz essa expressão ser não-nula numa faixa e
nula dos dois lados dela.

*(O caso especial em `0x893EC` — `maxAlpha == +inf` ⇒ `scale = 1` — é a variante
de um argumento só, `addAlphaThresholdFilterWithAlpha:color:colorSpace:`. Não é
este caminho.)*

---

## 4. O sinal: `alpha > 0.5` é DENTRO

`[BIN]` Também do `RenderBox.arm64`.
`-[RBDisplayList addDistanceFilterWithZeroDistance:oneDistance:scale:flags:]`
(`0x3F390`) grava seus dois primeiros `Double` na ordem em que os recebe
(`stp d0, d1, [sp, #8]`, `0x3F3A4`). O irmão
`-[RBDisplayList addDistanceFilterWithMaxDistance:scale:flags:]` (`0x3F354`)
preenche a mesma struct assim:

```
0x0003F360  fneg  d2, d0
0x0003F368  stp   d0, d2, [sp, #8]     ; zeroDistance = +maxDistance
                                       ; oneDistance  = −maxDistance
```

Alpha `0` na distância `+maxDistance`, alpha `1` em `−maxDistance`, alpha `0.5`
no contorno. **O alpha sobe quando a distância com sinal cai**, e a distância com
sinal cresce para fora. Logo `alpha > 0.5` é o interior, e a banda
`[0.5, 0.5 + w/(2·maxDistance)]` é a faixa de profundidades `[0, w]` **dentro**
da silhueta.

Isso fecha a conta de §2 pelo outro lado: a divisão por `2 × maxDistance` e o
mapeamento `±maxDistance → [1,0]` são a mesma codificação lida de dois lugares
independentes.

Substituindo:

```
t = (alpha − 0.5) / (w / (2·maxDistance))
  = profundidade / ringWidth
```

`maxDistance` cancela. A máscara não depende da resolução nem da faixa do campo
de distância — só da largura em pontos.

---

## 5. As duas guardas, e o laudo-mãe só tinha nomeado uma

`[BIN]` `0x20C3C`–`0x20C50`:

```
0x00020C3C  mvn   w8, w25
0x00020C40  tst   x8, #0xff
0x00020C44  b.eq  #0x20cec          ; pula se (x25 & 0xff) == 0xFF
0x00020C48  ldrb  w8, [x27, #0x30]
0x00020C4C  cmp   w8, #1
0x00020C50  b.eq  #0x20cec          ; pula se ringWidth é nil
```

A segunda o laudo-mãe já tinha: tag do `Optional` em `Shadow+0x30`. A primeira
ele chamou de *"um flag do chamador"*; ela é mais específica que isso. `x25` é o
**segundo word do `SDF`** — o mesmo que `0x20CA8` mascara com
`0xffffffff000000ff` para virar o `x1` da consulta de tamanho — e `0xFF` no byte
baixo é o caso vazio dele. **Camada sem campo de distância não ganha anel.**

E o seletor de fatia, `0x20C60`–`0x20C80`, é a escada `csel` de sempre: `k=0 →
slot 3`, `k=1 → slot 2`, `k=2 → slot 1`, `k=3 → slot 0`. `ringWidth[3−k]`, a
mesma inversão que o `sizeBasedValue` do repositório já centraliza.

---

## 6. `[INF]` A ordem: o recorte vem ANTES do desfoque

Isto é leitura, não medição, e está marcado como tal no cabeçalho do
`GlassShadow.h`.

`beginLayer` (`0x20C88`) … máscara … `clipLayerWithAlpha:mode:` (`0x20CE8`)
instala a máscara como **recorte do estado de desenho** em que o
`drawDisplayList:` de `0x20CF4` então desenha a arte. O desfoque de `0x20C38` é
um **filtro** sobre a camada desse mesmo estado, e um filtro se aplica à camada
já composta. Logo o recorte está **dentro** dele.

A consequência é visível e é o que torna a leitura verificável no pixel:
recortando antes, o desfoque suaviza a borda esfumada; recortando depois, a
sombra ficaria com um corte duro exatamente sobre a silhueta.

O `mode` do `clipLayerWithAlpha:mode:` é `0`, e `RenderBox.arm64` mostra que ele
**não é** um bit de inversão: `_RBDrawingStateClipLayer` (`0x3BB64`) passa esse
argumento por `RB::aliasing_mode(RB::RenderingMode)` (`0x3BBC4`) antes de chamar
`DisplayList::Builder::clip_layer(Layer*, State&, float, ClipMode)`. É o modo de
antialiasing. Máscara direta.

---

## 7. `[OBS]` vizinho fechado: `vibrantBrightness` tem UM consumidor, agora por varredura

O laudo-mãe registrava *um* consumidor lido e não afirmava que era o único.
Agora é varredura, e a lição do dia (procurar o **imediato**, não só o literal do
pool) valeu do jeito inverso: `0.75` nem chega ao pool — o ARM64 o materializa
com `fmov`, e há zero ocorrências do padrão `0x3FE8000000000000` no arquivo
inteiro.

`[BIN]` Varrendo as **142.694** instruções de `__text` com capstone atrás de cada
`ldr dN, [xM, #0xa0]` (dez) e cada `ldr dN, [xM, #0x350]` (seis — `Shadow` fica
em `params+0x2B0`, então o campo tem duas grafias):

| sítio | o que é |
|---|---|
| `0x20BB4` | **o consumidor**: o cinza do `addColorMultiplyFilterWithColor` da §5.1 |
| `0x6E024`, `0x6E044` | os dois lados do `Shadow == Shadow` de `0x6DFE4` |
| `0x2E500`, `0x4E0D8`, `0x711C4`, `0x713FC`, `0x81540` | uma linha cada de uma **cópia campo a campo**, todas ladeadas por `+0x348` e `+0x358` |
| `0xFD8C` e os outros sete | outra struct — a base de `0xFD8C` indexa um byte em `+0x469F` e põe uma tabela de quatro fatias em `+0x330`, que cairia **dentro** do `neutralOpacity` se a base fossem os parâmetros |

**Nenhuma segunda aritmética.** O `[OBS]` fecha.

---

## 8. O que entrou no código

`Source/RenderBox/GlassShadow.h` / `.cpp`:

- **`shadowRingMask(art, w, h, ringWidth)`** — a linha da §0, uma escalar por
  texel. A silhueta é o contorno `alpha ≥ 0.5` da própria arte; a distância é
  uma transformada euclidiana **exata** (Felzenszwalb–Huttenlocher, `O(n)`,
  duas passadas 1D), menos o meio pixel que põe o contorno *entre* centros em
  vez de *sobre* um.
- `[BIN]` **Fora da tela conta como fora da forma** — não é um default, é a
  borda vazia de um texel do SDF do alvo (§2). Arte que sai do canvas esfuma na
  borda do canvas.
- `shadowImage` aplica o recorte **antes** da cor, do desfoque e da translação
  (§6), multiplicando **só o alpha**: `clipLayerWithAlpha:` mascara cobertura,
  não tinge.
- `ShadowGeometry::ringWidth` deixa de ser um número carregado-e-não-usado e
  passa a ser a profundidade positiva em pixels do alvo, na mesma unidade do
  `blurRadius`.
- `kShadowRingNote` **deixa de dizer que o anel não é desenhado**. Passa a dizer
  a única coisa ainda convencional debaixo dele, com a mesma forma que a
  `kShadowBlurKernelNote` já tem: a aritmética da banda está medida, o **campo de
  distância** que ela amostra é gerado por `sdfTextureWithBufferAllocator:` num
  `TXRTexture`, que não está nem no `IconRendering.arm64` nem no
  `RenderBox.arm64` deste dump.

Três casos novos em `Tests/test_glass_shadow.cpp`, e o do meio é o que importa:
uma barra horizontal num campo 41×41 com anel de 8, checando que a máscara sobe
e **não volta a descer** até o miolo. Uma coroa falharia nele.

---

## 9. O que mudou no pixel

`icrender <bundle> --out <png> --size 1024`, antes e depois, contando texels
RGBA diferentes:

| documento | texels mudados | % de 1024² | delta máx. por canal |
|---|---|---|---|
| `Apollo-Reborn__Apollo-Reborn__AppIcon` | **279.947** | 26,70 % | 196 |
| `CodeEditApp__CodeEdit__CodeEditAlphaIcon` | **119.765** | 11,42 % | 190 |

Suíte: **617 casos, 0 falhas** com `IC_CORPUS_DIR` apontado para o corpus (614
antes desta frente, mais os três novos).

---

## 10. O que continua aberto

1. `[OBS]` **A grade de texels do SDF do alvo.** O campo é gerado fora destes
   dois slices. Duas consequências: a resolução é desconhecida, então a
   transformada roda na resolução do alvo; e se `ringWidth` em texels algum dia
   passasse do `maxDistance` daquele campo, a banda sairia da faixa
   representável e a rampa truncaria — uma saturação que daqui não se vê.
2. `[INF]` **A ordem recorte/desfoque** da §6 é argumentada, não medida.
3. `[OBS]` **A passagem de overdraw** da §5.3 do laudo-mãe continua transcrita e
   não desenhada (`kShadowOverdrawNote`).
4. `[OBS]` **`Shadow.ignoreFillOpacity`** continua sem consumidor conhecido.
5. `[OBS]` **As seis palavras do portão** de `0x49F40`–`0x49F70` continuam sem
   nome.
