# O canto do chiclet — as quatro perguntas respondidas, e nenhuma mexe no raio

**Fatias:** `References/{27.0-129,2.0-125}/out/slices/*.arm64` e os três binários
completos do app · **Gabarito:** `References/27.0-129/out/apple-512.png`
**Continua:** `Docs/Laudos/2026-09-15-chiclet-geometria.md` (o laudo central),
`2026-09-15-oraculo-appicon.md` §6.1/§7/§8, `2026-09-15-chiclet.md` §3.5/§4.1
**Selos:** `[BIN]` medido no binário, com endereço · `[ART]` medido no corpus ·
`[INF]` inferência marcada · `[OBS]` pergunta aberta declarada

---

## 0. As respostas, em seis linhas

`[BIN]` **Não é idiom.** O `Assets.car` do Icon Composer **não tem eixo de
idiom**: o `KEYFORMAT` do catálogo é `[7,1,2,17,9,10,14,12,24,19,18]` e o token
de `Idiom` (15) **não está nele**. E do nosso lado o `--idiom` é um no-op
**bit a bit**: os cinco valores dão o **mesmo SHA-256**.

`[BIN]` **Não é aparência.** O bitmap do gabarito está em `NSAppearanceNameSystem`
(id **0**), e as três aparências do documento compilado são `DarkAqua` (1),
`Aqua` (8) e `Tintable` (10). `--appearance` também é no-op bit a bit aqui.

`[BIN]` **O recuo legado está ligado no gabarito** — e a conta nos dois sentidos
**refuta as duas leituras do nosso binário**: raio relativo ao corpo dá `0,26`
(RMS **5,355 px**, canto grande demais); raio absoluto com a correção
`cornerRadius − inset` do próprio *witness* dá `0,2018` (RMS **3,456 px**, canto
pequeno demais). **O erro troca de sinal entre as duas** e o gabarito está no
meio, em `0,2250` (RMS **0,088 px**).

`[BIN]` **O `0,26` não multiplica nada.** O raio é **absoluto** em pontos de um
espaço de 1024 e **não há um único `fmul`/`fdiv` sobre ele** entre o `movk` que o
cria (`0x5EAFC`) e o `setRoundedRect:` que o consome (`0x7E01C`) — um escritor,
um leitor, sem `SizeBasedValue` no caminho. **Nós já multiplicamos pela coisa
certa** (`ChicletShape.cpp:167`).

`[BIN]` **`0,225` não existe em binário nenhum**, em codificação nenhuma, com
controle positivo que acha `100/1024` e `266,24` sem esforço. **Logo o raio não
muda.**

`[BIN]` **E os quatro cantos não são o maior erro do projeto — são o maior PICO.**
A banda de silhueta são **2 524 px, 1,33 % dos visíveis**; tirá-la leva a média
por canal de **8,85/10,18/9,85/4,95** para **7,68/9,05/8,64/1,82**. Um canto
perfeito vale **1,2 nível de RGB**; vale **3,1 de 4,95 no alfa**.

---

## 1. Pergunta 1 — o gabarito é do mesmo idiom e da mesma aparência?

Era a pergunta mais barata e a que podia matar a missão. **Ela mata a hipótese,
não a missão:** o canto não é idiom, e não teria como ser.

### 1.1. O catálogo não tem eixo de idiom

`[BIN]` O `KEYFORMAT` do `Assets.car` é

```
[7, 1, 2, 17, 9, 10, 14, 12, 24, 19, 18]
 |  |  |  |   |  |   |   |   |   |   +- PreviousValue
 |  |  |  |   |  |   |   |   |   +----- PreviousState
 |  |  |  |   |  |   |   |   +--------- DisplayGamut
 |  |  |  |   |  |   |   +------------- Scale
 |  |  |  |   |  |   +----------------- PresentationState
 |  |  |  |   |  +--------------------- State
 |  |  |  |   +------------------------ Dimension2
 |  |  |  +---------------------------- Identifier
 |  |  +------------------------------- Part
 |  +---------------------------------- Element
 +------------------------------------- ThemeAppearance
```

**O token `Idiom` (15) não aparece.** Toda rendition do catálogo vive no mesmo
idiom implícito; não há duas variantes de forma para escolher entre elas. A
leitura dos nomes é confirmada por dois valores conhecidos: o token 12 assume
`1` e `2` nas renditions de escala 100 e 200, e o token 7 é o que o
`car_extract.py` já usava para resolver aparência.

### 1.2. A rendition do gabarito, inteira

`[BIN]` `AppIcon256x256_NSAppearanceNameSystem_B8C2BFC9-…png`, layout 12,
payload `MLEC`, 512×512, `scale=200`:

```
ThemeAppearance 0   Element 85   Part 220   Identifier 6849
Dimension2 1   State 0   PresentationState 0   Scale 2
DisplayGamut {0, 1}   PreviousState 0   PreviousValue 0
```

`[BIN]` **As duas renditions de 512 diferem só no token 24 (`DisplayGamut`), e
são a mesma imagem.** Medido: 3 724 dos 189 993 pixels visíveis diferem, e
**nenhum deles por mais de 1 nível de 255** — nos canais cromáticos a média da
diferença é 0,004/0,032/0,021. A de 8 bits (`bgra8-premul`) é o
`apple-512.png` que o projeto usa; a de 16 (`rgba16f-premul`) é a mesma coisa em
precisão maior. **Não há um segundo espaço de cor para escolher**, e a hipótese
de que a média de ~10 níveis viesse de comparar P3 contra sRGB está **refutada**.

### 1.3. A tabela de aparências, e por que ela não decide

`[BIN]` `0 = NSAppearanceNameSystem`, `1 = NSAppearanceNameDarkAqua`,
`8 = NSAppearanceNameAqua`, `10 = ISAppearanceTintable`.

O **bitmap** está em `System` (0). O **documento** compilado existe em 1, 8 e 10
— e o oráculo já mediu que 1 e 8 são byte a byte idênticos. Então o gabarito é
de uma aparência que o documento não tem, e as duas que ele tem não se
distinguem. **A aparência não pode explicar diferença nenhuma aqui**, e a escolha
de `--appearance dark` do oráculo é inócua, não sortuda.

### 1.4. Do nosso lado: cinco idioms, três aparências, um SHA-256

`[BIN]` `AppIcon-27-defs.icon` a 412 px, Release, seis combinações:

| `--idiom` | `--appearance` | SHA-256 (24 hex) |
|---|---|---|
| base | base | `9776C1F689E664D1D81342A5` |
| base | light | `9776C1F689E664D1D81342A5` |
| square | dark | `9776C1F689E664D1D81342A5` |
| iOS | dark | `9776C1F689E664D1D81342A5` |
| macOS | dark | `9776C1F689E664D1D81342A5` |
| watchOS | dark | `9776C1F689E664D1D81342A5` |

**Todas iguais, bit a bit.** `6 of 6 layer(s) drawn` nas seis.

E a razão está lida, não suposta: no nosso `Source/`, `idiom` é **chave de
especialização do documento** — `Edit.cpp:31`, `Coverage.cpp:23`,
`PanelCanvas.cpp:184` ("`position`, `hidden` and `image-name` can each be
specialized per idiom") — e nunca toca a forma. **No alvo a forma vem de outro
eixo**: `style.platform` (`Configuration + 0x60`, lido em `0x42098`) com os casos
`main`/`watchOS`/`tvOS` (`0xA25B2`–`0xA25BF`), e é ele que a tabela
`platformOverrides` de `0x5EB38`–`0x5EBA0` indexa para dar ao watchOS o
`cornerRadius = 512` (o círculo). **Idiom do documento e platform do
renderizador são eixos diferentes**, e macOS mora em `main`, que não tem override
nenhum.

> `[OBS]` **Nosso CLI não expõe `style.platform`.** O círculo do watchOS é
> inalcançável pelo `icrender` — `--idiom watchOS` desenha o chiclet de `main`.
> Não é o bug desta missão (macOS é `main`, sem override, então para este
> gabarito nada mudaria), mas é um botão que falta.

### 1.5. De quebra: o `SISM` e os `flags` do `0x3fc`, que o oráculo deixou `[OBS]`

`[BIN]` A rendition `AppIcon` (layout 1010) tem 24 bytes de payload:

```
53 49 53 4d  01 00 00 00  01 00 00 00  00 01 00 00  00 01 00 00  01 00 00 00
'SISM'       versao 1     count 1      256          256          1
```

`[INF]` É um *multisize image set* (`MSIS` com o 4cc invertido, como `DWAR`→`RAWD`
e `MLEC`→`CELM`) declarando **uma** entrada de 256×256 — o slot `AppIcon256x256`.
A TLV dele são só `0x3ec = (0,0; 0,0)` e `0x3ee = 1`. **Não há geometria aqui.**

`[BIN]` E os `flags` do `0x3fc` **são zero em todos os nós, nas três
aparências**: os 4 nós do `AppIcon.iconstack` e os 2 de cada um dos três
`IconGroup`, em `Aqua`, `DarkAqua` e `Tintable` — 30 registros, `flags = 0` nos
30. **Não há bit de idiom nem de forma escondido ali.** O `[OBS]` 7 do oráculo
fecha, negativamente.

---

## 2. Pergunta 2 — o recuo legado está ligado no gabarito, e a conta nos dois sentidos

`[BIN]` **Está ligado.** O `chiclet-profile.py` mede, no `apple-512.png`, corpo
de **412 em (50,50)** num quadro de 512, com bordas de pixel inteiro — é
exatamente `512 − 2 × piso(512 × 100/1024) = 412` de `0x4202C`. O gabarito **não
foi desenhado pelo app** (que grava `useLegacyInsetting = 0` em `0x1A844`); foi
desenhado por quem liga o modo.

Então a pergunta certa é a que o enunciado pediu: **com o recuo ligado, sobre o
que o raio incide?** O *witness* de `path(in:inset:)` do `DefaultIconShape`
(`0x4F9E8`) faz `CGRectInset(rect, inset, inset)` e passa
**`raio = cornerRadius − inset`** ao `setRoundedRect:cornerRadius:cornerStyle:`
— isto é, o raio é **absoluto**, e encolher o rect **não** o encolhe junto: ele
sofre só a subtração linear. Num quadro de 1024 com `inset = 100`, isso dá
`266,24 − 100 = 166,24` sobre um corpo de 824, ou `r/N = 0,2018`.

`[BIN]` **As duas leituras erram, e erram com sinais opostos.** Ajuste da curva
contínua transcrita contra o perfil por linha do `apple-512.png` (corpo 412,
`s = 0,1216`, 185 linhas por lado, média dos quatro lados):

| `r/N` | `r` em 824 | diag do modelo | RMS | leitura |
|---|---|---|---|---|
| 0,2018 | 166,24 | 24,233 px | **3,456 px** | `cornerRadius − inset` (`0x4F9E8`) — canto **pequeno demais** |
| **0,2250** | **185,40** | **27,025 px** | **0,088 px** | — |
| 0,2600 | 214,24 | 31,228 px | **5,355 px** | `0,26 × corpo` (o nosso) — canto **grande demais** |
| 0,3231 | 266,24 | 38,807 px | 16,054 px | `0,26 × quadro` — já refutado em `chiclet-geometria` §2.3 |
| 0,1623 | 133,77 | — | 8,736 px | `0,26 − 100/1024` (aritmética errada, testada por completude) |

**Medido no gabarito: diag `27,054 px` (`0,06567 N`).**

`[BIN]` Isto é novo e importa: `chiclet-geometria` §2.3 só tinha refutado a
hipótese do **quadro** (`0,3231`), que erra para o mesmo lado que a nossa. A
leitura do *witness* — a única que o binário oferece para "o recuo está ligado" —
erra para o **outro** lado, por 2,8 px de diagonal. **O `0,2250` não está numa
ponta do intervalo das leituras: está entre elas**, e nenhuma o produz.

### 2.1. E não adianta procurar o `inset` certo: um parâmetro, duas amarras

O *witness* de `0x4F9E8` é de **um parâmetro só**: o mesmo `inset` encolhe o rect
**e** subtrai do raio. Então o gabarito o amarra duas vezes, e as duas contas não
fecham juntas. No quadro de 512:

| amarra | o que o gabarito mede | o que o *witness* exige |
|---|---|---|
| corpo | **412 px** (bordas de pixel inteiro) | `512 − 2·inset = 412` → **`inset = 50`** |
| raio | `0,2250 × 412 = **92,70 px**` | `266,24 × 512/1024 − 50 = **83,12 px**` |

`[BIN]` **Faltam 9,58 px, e não há valor de `inset` que conserte os dois ao mesmo
tempo:** o que daria o raio certo (`inset = 40,42`) deixaria o corpo em 431,2, e
o que dá o corpo certo deixa o raio em 83,12. **Logo o `DefaultIconShape` não
desenhou este canto, seja qual for o `inset`** — o que torna irrelevante, para
esta pergunta, não termos rastreado de onde o `inset` vem.

`[INF]` `0,2250 × 824 = 185,40` é o raio de canto da **grade de ícone do
macOS**: corpo de 824 num molde de 1024. O ajuste bate em 0,088 px de RMS e a
diagonal em 0,03 px.

`[BIN]` **E aqui uma inferência antiga cai.** `chiclet-geometria` §2.4 e o
enunciado desta frente apontavam o
`renderedLegacyCompatibleIconWithConfiguration:forDeviceClass:maskToIconShape:`
como a porta por onde a forma entraria de fora. **Ela não é:** o tipo ObjC do
seletor é `^{CGImage=}36@0:8@16q24**B32**` (`__objc_methtype 0xA8CB0`, lido
direto da seção, e o nome em `__objc_methname 0xB2B9A`) — o quinto argumento é
`B`, um **`BOOL`**, não `@`. O chamador manda um **sim/não**, não uma forma; e o
*overload* de dois argumentos (`0x51310`) é um *thunk* que faz `mov w4,#0`, isto
é, **`maskToIconShape = NO` por omissão**. A única porta que carrega forma
continua sendo `GlobalConfiguration.iconShape` (`0x4296C`) com o *witness* de
`CGPath` de `0x4FA40`.

---

## 3. Pergunta 3 — sobre o que o `0,26` multiplica: sobre nada

`[BIN]` **O raio é absoluto em pontos de um espaço de desenho de 1024, e não há
um único `fmul`/`fdiv` sobre ele em todo o trajeto.** O caminho, ponta a ponta:

```
0x5EAEC  mov x8,#0x70a4 / movk #0xa3d,lsl16 / movk #0xa3d7,lsl32 / movk #0x4070,lsl48
0x5EAFC  str x8, [x19, #0x228]         ; 266.24 nasce por MOV+MOVK (armadilha 1)
   |
0x5E1B0  ldr d8, [x20, #0x228]         ; le o default
0x5E1B4  ldr x20,[x20, #0x248]         ; Dictionary de override por PLATAFORMA
0x5E1C0  ldrb w0, [x0]                 ; a chave e UM BYTE (main/watchOS/tvOS)
0x5E1D4  ldr d0,[x8] / ldrb w8,[x8,#8] ; Optional<Double>: payload + tag
0x5E1E0  fcsel d8, d8, d0, eq          ; none -> fica 266.24
   |                                   ; (o mesmo corpo, inlined, em 0x42A24-0x42A40)
0x42AAC  str d11, [x0, x8]             ; DefaultIconShape.cornerRadius = 266.24
   |
0x4FA08  ldr d9, [x20, x8]             ; o UNICO leitor do campo
0x4FA10  bl  0x8DA34                   ; _CGRectInset
0x4FA14  fsub d4, d9, d8               ; raio = cornerRadius - inset
   |
0x7DF50  ldr x9, [x20, #0x20]          ; o descritor entrega o raio
0x7DFB0  fmov d4, x9                   ; cru
0x7E01C  b   0x8FC20                   ; setRoundedRect:cornerRadius:cornerStyle: (1 chamador)
```

`[BIN]` **Um escritor e um leitor.** Varrendo todos os pares
`adrp #0xCF000` + `#0x700` (o *field-offset* de `DefaultIconShape.cornerRadius`),
existem exatamente dois sítios: a escrita em `0x42AA0` e a leitura em `0x4FA00`.
E dos dez `ldr d,[x,#0x228]` do binário inteiro, oito são cópia campo a campo de
struct; só o *getter* `0x62058` e o `0x5E1B0` são leitura semântica.
`[BIN]` A confirmação do *seed* de `0x429C0` por segundo caminho (armadilha 3): a
cópia da struct começa em `sp+0x4BF8`, e `0x4E20 − 0x4BF8 = 0x228`,
`0x4E40 − 0x4BF8 = 0x248` — exatamente o par de `0x5E1B0`/`0x5E1B4`.

`[BIN]` **O 1024 escala o CANVAS, não o raio.** Materialização de `1024.0` no
`__text` inteiro: **um sítio**, `0x42940`, dentro da mesma função do raio, no
bloco `0x4291C`–`0x42954` que renormaliza o tamanho do canvas
(`scvtf`/`fdiv`/`fmul 1024.0`/`fdiv`). E `824,0` e `0,8046875` não existem — §4.

> **Veredito:** se o rect de desenho encolhe para 824, o raio **não** encolhe
> junto. Rect 1024 com `inset = 100` dá rect 824 e raio `266,24 − 100 = 166,24`
> — e **não** `266,24 × 824/1024 = 214,24`. Idêntico em 2.0-125 nos cinco
> endereços (`0x4F9E8`, `0x5E1B0`, `0x5EAFC`, `0x42AA0`, `0x7E01C`).

### 3.1. Não há `SizeBasedValue` no caminho do raio

`[BIN]` O elemento do dicionário de `parameters + 0x248` tem *stride* 32
(`add x8,x8,x0,lsl #5`), mas o que se lê dele é `payload@+0` e `tag@+8` com
`cmp #1` (`0x5E1D4`–`0x5E1E0`) — é um **`Optional<Double>`**, não os quatro
*slots* de uma `SizeBasedValue`. Entre `0x5E1B0`/`0x42A24` e a escrita `0x42AAC`,
e entre a leitura `0x4FA08` e o `setRoundedRect:` de `0x7E01C`, **não há um único
índice por classe de tamanho**. `[BIN]` A armadilha do `valor[3 − classe]` existe
e está noutro lugar — `0x4BEB0`–`0x4BEF4`, seguido de `fadd`/`fmul`, que é
realce/especular — e a transcrição dela no projeto
(`Source/RenderBox/GlassTranslucency.cpp:17`) não tem nada a ver com o canto.
**A armadilha da inversão de índice não se aplica ao raio.**

### 3.2. E nós multiplicamos pela coisa certa

`Source/RenderBox/ChicletShape.cpp:167` faz
`266.24 * size / 1024.0`, com o rect de lado `size` (linhas 174–175). Isso é
exatamente a CTM de §3: espaço de 1024, raio absoluto, ida para o raster por
escala uniforme. **O que seria errado — e agora está lido, não suposto — é
aplicar `size/1024` com `size = 824`, ou aplicar o `0,26` sobre um corpo
recuado.** No alvo o recuo entra como `− inset` **linear** (`0x4FA14`), nunca
como escala.

---

## 4. Pergunta 4 — `0,225` não tem origem em binário

`[BIN]` Varredura dos **onze** binários — as quatro fatias de 27.0-129, as
quatro de 2.0-125 e as fatias arm64 de `MacOS/Icon Composer`,
`Executables/ictool` e `Executables/icrtool` — em quatro passadas independentes
por binário: (a) bytes crus de **todas** as seções, em todos os alinhamentos,
para o padrão double LE e float32 LE; (b) **imediatos de 64 bits reconstruídos
de `MOVZ`/`MOVK`/`MOVN` no `__text`** (a armadilha 1), testando o acumulador a
cada `movk`; (c) `FMOV (imm)` escalar; (d) strings decimais.

**Controle positivo — passou:** o varredor acha `0x3FB9000000000000` (=100/1024)
em `IconRendering.arm64 @ 0x4224C`, `MOVZ x8,#0x3FB9,lsl#48`, nas duas versões; e
acha `266,24` (`0x4070A3D70A3D70A4`) em quatro sítios — `@ 0x5EAEC` no `__text`
das duas versões, como `movz`+3×`movk` seguido de `str x8,[x19,#0x228]`, e
`@ 0x98820` (27.0) / `@ 0x987D0` (2.0) no `__TEXT,__const`.

| padrão | resultado |
|---|---|
| `0,225` — `3FCCCCCCCCCCCCCD` / `3E666666` | **não achado** |
| `185,4` — `40672CCCCCCCCCCD` / `43396666` | **não achado** |
| `230,4` (= 0,225 × 1024) | **não achado** |
| `0,1810546875` (= 185,4/1024) | **não achado** |
| `0,2225` e `0,2350` (os outros dois ajustes de `chiclet-geometria`) | **não achado** |
| `166,24` e `0,2018` (a leitura do *witness* de §2) | **não achado** |
| `824,0` e `0,8046875` | **não achado** — confirma `chiclet-geometria` §1.1 |

`[BIN]` A vizinhança mais próxima em todo o conjunto é `0,22` em
`IconRendering @ 0x98520` (`__TEXT,__const`, seguido de 0,0065 / 2,0 / 0,28 /
0,2 / 0,04 / 0,08 — tabela de material, não de canto) e `0,223989873661194` em
`RenderBox @ 0x161928`. Nenhum dos dois é `0,225`, e nenhum dos dois está no
caminho do canto.

`[BIN]` E uma vizinhança que vale registrar: em `IconRendering @ 0x98820`, o
`266,24` mora numa tabela de geometria — `0x98800 = 4,0`, `0x98808 = 6,0`,
`0x98810 = 16,0`, `0x98818 = 24,0`, **`0x98820 = 266,24`**, `0x98828 = 25,0`,
`0x98830 = 60,0`, `0x98838 = 256,0`; e em `0x5EAE8`–`0x5EB0C` o `266,24` escrito
em `[x19,#0x228]` é imediatamente seguido de `256,0` no campo vizinho. Os oito
*doubles* de `0x98800`–`0x98838` foram relidos byte a byte por um segundo
parser, independente do varredor. `[OBS]` Nenhum `adrp`+`ldr`/`add` do `__text`
aponta para `0x98820`, então essa cópia parece morta e o valor vivo é o `movk` de
`0x5EAFC`; a varredura cobriu `adrp`+`ldr`/`add` e não *chained fixups*.

> **Logo o raio não muda.** Trocar `0,26` por `0,2250` fecharia o diff e não teria
> uma única leitura de binário atrás. É o que o enunciado proíbe, e a medição de
> §5 mostra que o prêmio nem sequer era o que parecia.

---

## 5. O que os quatro cantos realmente valem

O enunciado desta frente abre com "o maior erro de pixel do projeto são os quatro
cantos", e os piores blocos de fato são eles. **Mas "pior bloco" e "maior erro"
não são a mesma coisa, e a diferença foi medida.**

`[BIN]` No diff alinhado (412 posto em (50,50) num quadro de 512):

```
pixels visiveis 189993
medio(vis)                     R 8,85  G 10,18  B 9,85  A 4,95
banda de silhueta (|dAlpha|>128)  2524 px  (1,33 % dos visiveis)
  medio na banda               R 95,4  G 94,3  B 99,8  A 237,3
  medio SEM ela                R 7,68  G  9,05 B 8,64  A 1,82
```

`[BIN]` São **2 524 px** — cerca de 631 por canto, uma banda de ~4 px de largura
ao longo de um arco de ~157 px, que é exatamente o que uma diferença de raio de
14 px produz. Dentro dela os quatro canais **saturam de uma vez** (Apple opaca,
nós com `α = 0`: em `(64,88)` a Apple lê `A=255, RGB 145/145/145` e nós lemos
`A=0`), e é isso que põe os doze piores blocos 8×8 todos nos cantos, com
~195 de média do delta máximo.

`[BIN]` **E fora da banda o canto não é pior que o resto.** Nos quatro quadrados
de 110×110 que contêm os cantos — 26,4 % dos pixels visíveis — mora **24,97 %**
da soma do erro RGB, com média 8,82/8,99/9,53 contra 8,86/10,60/9,97 no resto.
A região do canto carrega **a sua fatia de área e nada mais**.

**Portanto:** consertar o canto perfeitamente levaria a média de
`8,85/10,18/9,85` para **`7,68/9,05/8,64`** — 1,2 nível — e os cantos sairiam da
lista dos piores blocos. Os ~8 níveis restantes são os de sempre: blur-material
desligado, grampo `plusLighter` desligado, overdraw da sombra. **O alfa é a
exceção**: 3,1 dos 4,95 são a banda, e ali o canto é quase tudo.

---

## 6. O que mudou

**Nada no renderizador.** `ChicletShape.cpp`, `IconRenderer.cpp` e todo o resto
de `Source/` estão intocados; o render de controle é **SHA-256 idêntico** antes e
depois (`9776C1F689E664D1D81342A5…`).

- **`scripts/png-diff.py`** — passa a separar a **banda de silhueta**
  (`|ΔAlpha| > 128`) e a imprimir três linhas: quantos pixels ela tem, a média
  dentro dela e a média **sem** ela. Não é número novo: é o peso do número que já
  estava lá. Sem isso, "os piores blocos são os quatro cantos" se lê como "o
  maior erro do projeto são os cantos", e §5 mostra que não é.

---

## 7. O que continua `[OBS]`

1. **Quem desenha o canto de `0,2250`** continua fora do bundle, e a porta
   ficou mais estreita: `maskToIconShape:` é um `BOOL` (§2.1), então a única
   rota que carrega forma é `GlobalConfiguration.iconShape` (`0x4296C`) com o
   *witness* de `CGPath` de `0x4FA40`. Um segundo `.car`, de um app cujo ícone se
   conheça, diria se `185,4` é constante de plataforma.
2. **O corpo de `0x520E4` (legacy-compatible) e de `0x522C8` (full-bleed)** não
   foi lido além do prólogo: em que rect cada um chama a forma, e se a máscara
   legada usa outro raio, continua sem leitura. §2.1 mostra que **nenhum valor de
   `inset` fecha as duas amarras do gabarito**, o que torna a pergunta
   irrelevante para o veredito — mas não a responde.
3. **A chave do dicionário de `parameters + 0x248`** foi lida como 1 byte com
   entrada `Optional<Double>` (§3), mas o enum não foi identificado e não se sabe
   se alguma plataforma real traz override ≠ `nil`. Se trouxer, `266,24` deixa de
   ser universal.
4. **`style.platform` não tem botão no `icrender`** (§1.4). O círculo do watchOS,
   que está `[BIN]` em `0x5EB38`, é inalcançável pela linha de comando.
5. **O `[OBS]` 3 do oráculo** (as duas leituras do mapa de `0x3fd`) continua vivo:
   os `flags` do `0x3fc` serem todos zero (§1.5) não o desempata.
6. **Os três tamanhos do `.icns`** continuam sem obedecer à fórmula de `0x4224C`
   (`chiclet-geometria` §1.3). Esta frente não entrou neles.

---

## 8. Endereços e comandos, para reprodução

| onde | endereço | o que é |
|---|---|---|
| `IconRendering` | `0x4202C`–`0x4236C` | o retângulo de desenho (o recuo legado) |
| `IconRendering` | `0x42098` | `style.platform` (`Configuration + 0x60`) |
| `IconRendering` | `0x4224C` | `0x3FB9000000000000` = 100/1024 — **controle positivo da varredura** |
| `IconRendering` | `0x4F9E8`–`0x4FA3C` | *witness* `path(in:inset:)`: `0x4FA08` lê o raio, `0x4FA10` chama `_CGRectInset` (`0x8DA34`, pela tabela de símbolos indiretos), `0x4FA14` faz `raio − inset`, `0x4FA2C` grava tag 2 |
| `IconRendering` | `0x4FA40`–`0x4FA84` | o outro *witness*: `pathInRect:inset:` por `objc_msgSend`, tag 4 — a rota do `CGPath` de fora |
| `IconRendering` | `0x5E1A0`–`0x5E1F4` | a função-raiz do raio: default + override por plataforma, **sem `fmul`/`fdiv`** |
| `IconRendering` | `0x5EAEC`–`0x5EAFC` | `266,24` por `movz`+3×`movk` → `str x8,[x19,#0x228]` |
| `IconRendering` | `0x42940` | o **único** `1024.0` do `__text` — e ele escala o canvas (`0x4291C`–`0x42954`), não o raio |
| `IconRendering` | `0x42AA0`/`0x4FA00` | o **único** escritor e o **único** leitor de `DefaultIconShape.cornerRadius` |
| `IconRendering` | `0x7DF50`/`0x7DFB0`/`0x7E01C` | o descritor entrega o raio cru ao `setRoundedRect:cornerRadius:cornerStyle:` (1 chamador) |
| `IconRendering` | `0xA8CB0` / `0xB2B9A` | `__objc_methtype` `^{CGImage=}36@0:8@16q24**B32**` e o nome do `…maskToIconShape:` — o 5º argumento é **`BOOL`** |
| `IconRendering` | `0x520E4` / `0x51310` / `0x52154` | as IMPs de legacy-compatible (5 args), o *thunk* de 4 args que passa `mov w4,#0`, e o full-bleed |
| `IconRendering` | `0x5EB38`–`0x5EBA0` | `platformOverrides` (watchOS 512, tvOS 5/3) |
| `IconRendering` | `0x98520` | `0,22` — o vizinho mais próximo de `0,225` em todo o conjunto |
| `IconRendering` | `0x98820` | `266,24` no `__const`, com `256,0` em `0x98838` |
| `RenderBox` | `0x161928` | `0,223989873661194` — perto, e não é |

```
python scripts/png-diff.py References/27.0-129/out/apple-512.png ours-412.png \
       --legacy-inset --blocks 8
python scripts/chiclet-profile.py References/27.0-129/out/apple-512.png
```
