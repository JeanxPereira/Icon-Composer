# A geometria do chiclet contra a Apple — o recuo tem dono, e o canto tem medida

**Fatias:** `References/2.0-125/out/slices/IconRendering.arm64` (e a de 27.0-129,
idêntica nos endereços citados) · **Gabarito:** `References/27.0-129/out/`
**Continua:** `Docs/Laudos/2026-09-15-oraculo-appicon.md` §6.1 e §8.1–8.2, e
`Docs/Laudos/2026-09-15-chiclet-curva.md`
**Selos:** `[BIN]` medido no binário, com endereço · `[ART]` medido no corpus ·
`[INF]` inferência marcada · `[OBS]` pergunta aberta declarada

---

## 0. As duas respostas, em cinco linhas

`[BIN]` **O recuo de 824/1024 é um MODO do renderizador que o Icon Composer
nunca liga.** Ele é `FinalizedIcon.Configuration` em `0x4202C`, guardado pelo
campo `useLegacyInsetting` (`+0x59`), e o `100/1024` é o imediato
`0x3FB9000000000000` de `0x4224C` — **o único do bundle inteiro**. O app importa
só o init que grava esse campo em **zero**, então canvas e exportação desenham de
borda a borda, como este renderizador. **Nada foi implementado no desenho**; o
alinhamento foi para o comparador (`png-diff.py --legacy-inset`).

`[BIN]` **O canto da Apple é a MESMA curva com OUTRO raio.** Ajustada a curva
contínua transcrita, o gabarito de 512 dá **`r = 0,2250 × corpo`** com erro RMS
de **0,088 px**; o nosso `0,26` dá **5,36 px** de RMS no mesmo perfil. Na
diagonal: **27,05 px** (Apple) contra **31,18 px** (nosso) num corpo de 412 — é
exatamente o "entra mais cedo" que o oráculo viu em `(70,420)`.

`[OBS]` **E nenhuma constante do bundle produz `0,225`.** A plataforma `main`
(iOS/macOS) não tem override de raio, e o único mecanismo que troca a forma é o
`GlobalConfiguration.iconShape` fornecido pelo chamador. Então a medida entra e a
correção **não**: mexer no raio até o canto casar seria overfitting.

---

## 1. O recuo: endereço, fórmula e dono

### 1.1. A função, e o imediato

`[BIN]` `IconRendering.arm64 0x4202C`–`0x4236C` é um método de
`FinalizedIcon.Configuration` que recebe um `CGRect` (`v0..v3`) e uma escala
(`v4`) e devolve **o retângulo em que o ícone é desenhado**. O despacho é o campo
`useLegacyInsetting`:

```
0x42094  ldrb w9, [x20, #0x59]     ; Configuration.useLegacyInsetting
0x42098  ldrb w19, [x20, #0x60]    ; style.platform  (chave dos overrides)
0x420A0  cmp  w9, #1
0x420A4  b.ne #0x42104             ; FALSO -> nao recua nada
```

`[BIN]` No ramo verdadeiro, `0x42240`–`0x42274`:

```
0x42240  ldr  d0,  [sp, #0x3b8]        ; globalConfig._relativeIconInset  (+0x30)
0x42244  ldrb w8,  [sp, #0x3c0]        ; a tag do Optional                (+0x38)
0x4224C  mov  x8, #0x3fb9000000000000  ; 0.09765625 == 100/1024
0x42254  fcsel d0, d1, d0, eq          ; nil -> o default
0x42258  fmul d1, d9, d0               ; largura * recuo
0x4225C  frintm d1, d1                 ; PISO
0x42260  fadd d1, d1, d1
0x42264  fsub d13, d9, d1              ; largura' = largura - 2*piso(largura*recuo)
                                       ; (0x42268-0x42274 faz o mesmo na altura)
0x422F4  fsub d12, d13, d15            ; menos o meio pixel, quando a plataforma pede
0x4232C  ; x' = CGRectGetMidX(rect) - largura'/2   (CGRectGetMidY para y)
```

**`lado' = lado − 2 × piso(lado × (relativeIconInset ?? 100/1024))`, recentrado.**

`[BIN]` Os *stubs* foram resolvidos pela tabela de símbolos indiretos, não pelo
nome de chute: `0x8D9F8 = _CGRectGetMidX`, `0x8DA04 = _CGRectGetMidY`,
`0x8DA28 = _CGRectGetWidth`, `0x8D9D4 = _CGRectGetHeight`,
`0x8DA34 = _CGRectInset`.

`[BIN]` **E o `0.09765625` é único.** Varredura do imediato
(`mov`/`movk`, que não passa pelo *constant pool*) e dos bytes de todas as seções
de dados nas fatias `IconRendering`, `IconComposerKit`, `IconComposerFoundation`,
`RenderBox`, `ictool`, `icrtool` e `IconComposer` do 2.0-125 **e** nas quatro do
27.0-129: **uma ocorrência, `0x4224C`**, a mesma nas duas versões. Não há
`0.8046875`, nem `824`, nem `185.4`, nem `0.225` em lugar nenhum — nem como
imediato, nem em poça de constantes.

### 1.2. Quem liga o modo — e o app não é ninguém dele

`[BIN]` `useLegacyInsetting` (`Configuration + 0x59`) tem **três** escritores em
todo o `__text`, e a varredura de `strb …, #0x59` é exaustiva:

| escritor | função | o que grava |
|---|---|---|
| `0x14BC0` | `Configuration(icon:style:useLegacyInsetting:parametersOverride:)` `0x142BC` | o argumento |
| `0x1A844` | `Configuration(icon:style:parametersOverride:)` `0x19F4C` | **`wzr` — zero** |
| `0x27EA4` | `FinalizedIcon(serialized:device:)` `0x26704` → `0x26F58` | o que veio serializado |

`[BIN]` O init com a bandeira **não tem um chamador dentro do `IconRendering`**
(`xref 0x142BC`: 0 sítios, contando `B` de *tail call*). E o
`IconComposerKit.arm64` importa, dos dois, **só o sem bandeira**
(`…V4icon5style18parametersOverride…`, `0x2185DE`) — o mesmo em 27.0-129. Nas
outras fatias do bundle a *string* `useLegacyInsetting` nem aparece.

> **Logo o recuo não é regra do desenho: é um modo que só um cliente EXTERNO do
> `IconRendering` liga.** `[INF]` Esse cliente é o compilador de assets que
> produziu as *renditions* legadas do `Assets.car` e o `AppIcon.icns` — o nome do
> campo, o seletor `legacyFinalizedIconWithSize:scale:deviceClass:appearance:renderingMode:`
> e o par `renderedFullBleedIconWithConfiguration:` × `renderedLegacyCompatibleIconWithConfiguration:forDeviceClass:maskToIconShape:`
> dizem a mesma coisa, mas o compilador está fora do bundle e não foi lido.

`[BIN]` Isto também fecha, de lado, um `[OBS]` do laudo do chiclet §4.1: o
`rectB` cujo nome semântico ficou em aberto **é o retângulo recuado deste
`0x4202C`**, e por isso o default de `chicletDropShadow` (`min(ΔL,ΔA)/2 ≥ 1`) quer
dizer literalmente "há margem para a sombra": com `useLegacyInsetting` ligado há
100/1024 de margem, sem ele há zero.

### 1.3. A fórmula contra os quatro tamanhos

| saída | quadro | corpo medido (sub-pixel) | `lado − 2·piso(lado·100/1024)` |
|---|---|---|---|
| `.car` (o gabarito) | 512 | **412,000** (bordas em `x = 50,000` exatas) | **412** ✔ |
| `ic13` | 256 | 205,60 | 206 |
| `ic07` | 128 | ~102,5 | 104 |
| `ic11` | 32 | 25,07 | 26 |

`[BIN]` **A saída de 512 bate exatamente e com bordas de pixel inteiro** — a
linha central vai de `α=31` (sombra) em `x=49` para `α=255` em `x=50`, sem pixel
parcial. `[OBS]` As três do `.icns` **não** batem: têm bordas sub-pixel e larguras
que a fórmula não produz com nenhuma das duas arredondações. O mais provável é
que o `.icns` venha de reamostragem de um mestre maior, mas isto não foi lido —
é `[OBS]`, e a afirmação "824/1024 nos quatro tamanhos" do oráculo §7 deve ser
lida como "dentro de ±1 px", não como a fórmula.

### 1.4. O meio pixel, e os overrides por plataforma

`[BIN]` `0x422DC`: `d15 = 2 × (0,5 / escala)` é subtraído dos dois lados **só
quando** `platformOverrides[plataforma].usesHalfPixelInset` é verdadeiro
(`0x422BC`–`0x422D8`, entrada de 32 bytes, byte `+0x19`). E o dicionário
`ICRRenderingParameters.platformOverrides` (`params + 0x248`) tem, nos defaults
(`0x5EB38`–`0x5EBA0`), **duas** entradas:

| chave | `cornerRadius` | `aspectRatio` | `usesHalfPixelInset` |
|---|---|---|---|
| `1` = `watchOS` | **512,0** (o círculo de 1024) | `nil` | **true** |
| `2` = `tvOS` | `nil` | **5/3** | `nil` |

`[BIN]` Os nomes dos casos saem da reflexão: `main`, `watchOS`, `tvOS`
(`0xA25B2`–`0xA25BF`). **A plataforma `main` — onde macOS e iOS vivem — não tem
override nenhum**, então nem meio pixel nem raio próprio: o raio dela é o
`defaultChicletCornerRadius` de `parameters + 0x228`, que é **266,24**.

`[BIN]` E isso fecha outro `[OBS]` do laudo do chiclet §3.5: o `d11` que vira
`DefaultIconShape.cornerRadius` em `0x42AAC` é carregado de `[sp,#0x4E20]`, que é
`parameters + 0x228` na cópia de `parameters` que começa em `[sp,#0x4BF8]` — isto
é, **`defaultChicletCornerRadius` É o default**, e a tabela consultada em
`0x42A24` é o `platformOverrides` acima.

---

## 2. O canto: a medida, e um instrumento para refazê-la

### 2.1. Como se mede sem discutir antialiasing

`scripts/chiclet-profile.py` (novo, versionado) mede **por linha**: a soma da
cobertura de uma linha dentro da caixa do corpo é o comprimento de dentro, e pela
simetria o recuo da borda naquela linha é `(N − L)/2`. Área é área — o número não
depende do filtro de antialiasing de nenhum dos dois lados, e o modelo é
integrado do mesmo jeito (média sobre 64 sub-linhas). Da sombra externa ele
desconta `c = (a − s)/(1 − s)`.

### 2.2. Os números

`--appearance dark`, `AppIcon-27-defs.icon`, corpos de 412/206/102:

| fonte | corpo | diagonal (px) | diagonal / N | melhor `r/N` (RMS) | RMS em `r/N = 0,26` |
|---|---|---|---|---|---|
| **Apple** `.car` | 412 | **27,05** | 0,06567 | **0,2250** (0,088 px) | **5,36 px** |
| **nosso** | 412 | **31,18** | 0,07567 | 0,2600 (0,365 px) | 0,365 px |
| Apple `ic07` | 102 | 6,61 | 0,06478 | 0,2225 (0,085 px) | 1,48 px |
| nosso | 102 | 7,68 | 0,07530 | 0,2550 (0,173 px) | 0,238 px |
| Apple `ic13` | 206 | 13,70 | 0,06651 | 0,2350 (1,131 px) | 2,33 px |
| nosso | 206 | 15,59 | 0,07570 | 0,2575 (0,237 px) | 0,295 px |

`[BIN]` **Um número por tamanho, e eles concordam:** o canto da Apple cruza a
diagonal em `0,0648`–`0,0665` do lado, o nosso em `0,0753`–`0,0757`. O ajuste do
`ic13` é o pior (RMS 1,13 px) porque o corpo dele tem 205,6 px e a caixa inteira
usada na medida tem 206 — o desalinhamento de 0,2 px por lado entra no perfil.

`[BIN]` **O gabarito é a mesma FAMÍLIA de curva.** Com o raio livre, o canto
contínuo transcrito reproduz o perfil da Apple com 0,088 px de RMS a 412 e
0,085 px a 102. Uma família rival testada — o canto **circular** (o tipo 3 do
`add_path`) — não passa de 1,77 px de RMS com o melhor raio dela. Não é a curva
que muda: **é o raio**.

### 2.3. As quatro hipóteses, uma de cada vez

| hipótese | veredito | por quê |
|---|---|---|
| **o raio é `0,26` do QUADRO de 1024, não do corpo de 824** | **refutada** | daria `r/N = 0,26 × 1024/824 = 0,3231` — um canto **maior**, entrando ainda **mais cedo**. A diferença medida tem o sinal contrário |
| **o `×1.275` não é desfeito neste caminho** | **refutada** | `r/N = 0,3315` dá RMS de **17,6 px** |
| **o rasterizador (`kSubRows = 4`)** | **não é ele** | o nosso render de 412 cruza a diagonal em **31,175 px** e o modelo contínuo de `r = 0,26` em **31,228**: **0,05 px** de diferença, contra os **4,1 px** que faltam para a Apple |
| **a folga por aresta recalculada no meio do canto (`0x7F7BC`)** | **sem efeito aqui** | o chiclet é um quadrado de quatro raios iguais: as duas arestas dão o mesmo `t = 1,746`. E o render bate com o modelo, que já a implementa |

### 2.4. O que sobra, e por que não vira correção

`[BIN]` O raio que o **nosso** caminho usa está lido e é `266,24 / 1024 = 0,26`
(`GlassMaterial.h:240`, `params + 0x228`), sem override para a plataforma `main`
(§1.4). O raio que a saída da Apple **tem** é `≈ 0,2225–0,2250` do corpo
(`≈ 183–185` num corpo de 824). **Não há constante no bundle que produza isso.**

`[BIN]` E existe exatamente um mecanismo, no binário, capaz de trocar a forma sem
trocar constante: `GlobalConfiguration.iconShape` (`+0x58`, o último campo, um
`(any ICRIconShape)?`). Em `IconRenderer.init`:

```
0x42938  ldr  x20, [sp, #0x4fb8]   ; a copia de globalConfig, +0x58 = iconShape
0x4296C  cbz  x20, #0x429c0        ; NULO  -> constroi o DefaultIconShape
                                   ; nao-nulo -> usa a forma do chamador
```

e a forma do chamador chega como **`CGPath`** (`0x4FA40`: o *witness* chama
`pathInRect:inset:` por `objc_msgSend` e embrulha, tag 4 → `setPath:`), enquanto
o `DefaultIconShape` chega como retângulo arredondado (`0x4F9E8`: `CGRectInset`
+ `raio = cornerRadius − inset`, tag 2 → `setRoundedRect:cornerRadius:cornerStyle:`).
`[BIN]` O `IconComposerKit` **não importa acessor nenhum de `iconShape`** — ele
só escreve `relativeIconInset`, `lightDirection`, `lightIntensity`,
`chicletDropShadow`, `effectsAreEnabled` e `drawMitigatedVersion`.

> `[INF]` **A leitura honesta:** a rendição legada foi desenhada com uma forma
> fornecida por quem a compilou, e não com o `DefaultIconShape` de 266,24. Isso é
> inferência: o compilador está fora do bundle.
>
> **E por isso o raio não muda aqui.** Trocar `0,26` por `0,225` casaria o
> gabarito e contradiria a única leitura `[BIN]` que existe do caminho que este
> renderizador desenha. A regra do projeto é essa: o gabarito desempata leitura,
> não escolhe número.

---

## 3. O que mudou

**Nada no renderizador.** `ChicletShape.cpp` não foi tocado; `IconRenderer.cpp`
não foi tocado.

- **`scripts/chiclet-profile.py`** (novo) — o perfil do canto por linha, o
  cruzamento da diagonal e o ajuste do raio, com a curva contínua transcrita do
  `add_rounded_rect` dentro dele.
- **`scripts/png-diff.py`** — `--legacy-inset` (e `--relative-inset`): põe um
  render menor no lugar que o recuo legado lhe daria dentro do quadro maior,
  **copiando pixel a pixel**, sem reamostrar. A conta de `0x4202C` está no
  *docstring* da função, com endereço. Era isto que o oráculo fazia à mão.
- **`Source/RenderBox/ChicletShape.h`** — uma nota: qual dos dois caminhos do
  alvo esta transcrição é, e o que o gabarito mede no outro.

O diff alinhado agora sai de um comando só:

```
python scripts/png-diff.py References/27.0-129/out/apple-512.png ours-412.png \
       --legacy-inset --blocks 16
```

```
pixels visiveis 189993   diferentes 160327 (84,39 %)
delta medio  R 8,95  G 10,03  B 9,89  A 4,95
piores blocos 16x16: (80,432) 123,08 · (416,64) 123,03 · (416,432) 122,15 ·
                     (80,64) 121,20 · (432,416) 120,77 · (64,416) 120,70 …
```

— os **oito** piores blocos são os quatro cantos, vistos duas vezes (o bloco do
lado de dentro e o do lado de fora de cada um). É o canto, e é simétrico.

---

## 4. Corpus e suíte

`[ART]` **O efeito no corpus é zero, e isso é verificável sem rodá-lo:** nenhuma
linha de `Source/` mudou. Os seis casos de `Tests/test_chiclet_shape.cpp`
continuam travando os mesmos números (`extent = 1,5286649…`, `t = 1,746`,
`r = 0,26 × canvas`), e continuam verdes pelo mesmo motivo.

Suíte em Debug, com `IC_CORPUS_DIR=D:/CodingProjects/Icon-Composer/References/corpus`:
**639 casos, 0 falhas** — a mesma contagem de `cc97cc1`.

---

## 5. O que continua `[OBS]`

1. **Quem desenha o canto de `0,225`.** O mecanismo está lido (`iconShape`,
   `0x4296C`); o valor e o cliente estão fora do bundle. Um segundo `.car` de um
   app com ícone diferente diria se `0,225` é constante de plataforma ou algo
   derivado do tamanho.
2. **`relativeIconInset` é escrito pelo `IconComposerKit` e, em `0x4202C`, só é
   lido no ramo legado.** Se ele tem outro leitor em outra função não foi
   varrido, então "o app escreve um campo que o modo dele não usa" está `[BIN]`
   para `0x4202C` e `[OBS]` para o resto do binário.
3. **Os três tamanhos do `.icns` não obedecem à fórmula de `0x4224C`** (§1.3) e
   têm bordas sub-pixel. De onde eles saem — reamostragem, outro caminho, outra
   escala — não foi lido.
4. **A sombra externa** (até 448/512, deslocada +2 px em y) continua sem
   transcrição: ela vive na passada `0x433D0`, que só roda com
   `drawMitigatedVersion`, e esta frente não entrou nela.
5. **O SDF do shader** continua sem comparação com a cúbica — mas agora há um
   número contra o qual comparar: o perfil de linha do `chiclet-profile.py`.

---

## 6. Endereços, para reprodução

| fatia | endereço | o que é |
|---|---|---|
| `IconRendering` | `0x4202C`–`0x4236C` | `Configuration` → o retângulo de desenho (o recuo) |
| `IconRendering` | `0x42094` | `useLegacyInsetting` (`Configuration + 0x59`) |
| `IconRendering` | `0x4224C` | `0x3FB9000000000000` = **100/1024**, o único do bundle |
| `IconRendering` | `0x4225C` | `frintm` — piso, não arredondamento |
| `IconRendering` | `0x422DC` | o meio pixel, guardado por `usesHalfPixelInset` |
| `IconRendering` | `0x142BC` / `0x19F4C` | os dois inits de `Configuration` (com e sem a bandeira) |
| `IconRendering` | `0x26704` → `0x26F58` | `FinalizedIcon(serialized:device:)`, o terceiro escritor |
| `IconRendering` | `0x5EB38`–`0x5EBA0` | os defaults de `platformOverrides` (watchOS 512, tvOS 5/3) |
| `IconRendering` | `0x42938` / `0x4296C` | `globalConfig.iconShape` e o `cbz` que escolhe o `DefaultIconShape` |
| `IconRendering` | `0x42A24`–`0x42AAC` | o override de raio por chave e a escrita de `DefaultIconShape.cornerRadius` |
| `IconRendering` | `0x4F9E8` / `0x4FA40` | os dois *witnesses* de `path(in:inset:)`: retângulo arredondado × `CGPath` |
| `IconRendering` | `0x8D9D4`/`0x8D9F8`/`0x8DA04`/`0x8DA28`/`0x8DA34` | `CGRectGetHeight`/`MidX`/`MidY`/`Width`/`CGRectInset` |
| `RenderBox` | `0x7F580`–`0x7FE58` | `add_rounded_rect` — a curva que o `chiclet-profile.py` ajusta |
