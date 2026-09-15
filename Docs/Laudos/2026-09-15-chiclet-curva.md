# A curva do chiclet — o `[OBS]` do `×1.275` fecha, e o fundo passa a ser recortado

**Fatia:** `References/2.0-125/out/slices/RenderBox.arm64`
**Continua:** `Docs/Laudos/2026-09-15-chiclet.md` §6 e §6.1
**Selos:** `[BIN]` medido no binário, com endereço · `[ART]` medido no corpus ·
`[INF]` inferência marcada · `[OBS]` pergunta aberta declarada

---

## 0. A resposta, em uma linha

`[BIN]` **O `×1.275` é convenção de armazenamento e NÃO alcança o avaliador da
curva.** O raio que chega ao `add_rounded_rect` é o raio que o chamador pediu —
266.24, não 339.456 —, logo `t = 1.746 ≥ 1` e o canto do chiclet é o **contínuo
canônico**, não o misturado. O laudo anterior estava certo em recusar o chute: as
duas leituras davam cantos diferentes, e a que ganha é a que ele não tinha.

| pergunta de §6.1 | resposta | selo |
|---|---|---|
| o elemento de caminho guarda o raio cru ou o multiplicado? | **cru** — o codificador divide | `[BIN]` |
| qual regime de curva o chiclet usa? | `t = 1.746` → **canônico** | `[BIN]` |
| quanto o canto come de cada aresta? | `1.5286649465560913 × r = 406.99` px em 1024 | `[BIN]` |
| o que sobra de aresta reta entre dois cantos? | `1024 − 2×406.99 = 210.016` px | `[BIN]` |

---

## 1. O experimento que o laudo anterior nomeou, e onde ele estava

§6.1 pediu "o codificador que transforma o `RBShape` tipo 4 no `RBPathElement` 9
que `0x80DCC` lê. Uma função, não uma campanha." Ele existe, e é
**`RB::Coverage::Primitive::add_path(RB::CGContext&)`**, `0x96720`–`0x969EC`.

A busca que o achou foi mecânica e vale registrar, porque §6.1 tinha declarado
que o recíproco era lido **uma única vez**: essa contagem valia só para o
*constant pool*. Varrendo todo o `__text` atrás do **imediato** `0x3F48C8C9`
(`0.7843137f`, materializado por `mov`+`movk` em vez de `ldr`) aparecem mais
três leitores, e um deles é o codificador.

`[BIN]` Os **cinco** sítios do fator, e nada mais no binário:

| função | endereço | o que faz | guarda |
|---|---|---|---|
| `Coverage::Primitive::set_globals` | `0x95A94` | `ldr s1, [x9,#0x1b8]` → **÷1.275** | `cmp w26,#4` (`0x95A88`) |
| `Coverage::Primitive::make_shadow` | `0x94EA4` | `ldr s1, [x8,#0x1a4]` → **×1.275** | constrói um `Primitive` novo |
| `Coverage::Primitive::encode` | `0x94550` | imediato `0.7843137f` → **÷1.275** | `cmeq` contra `4` (`0x94538`) |
| `Coverage::Primitive::decode` | `0x94838` | imediato `1.275f` → **×1.275** | `cmp w9,#4` (`0x94828`) |
| **`Coverage::Primitive::add_path`** | **`0x9682C`** | imediato `0.7843137f` → **÷1.275** | tipo `≠ 3` no ramo `{3,4}` |

**Todo leitor desfaz; todo escritor refaz.** Não sobra um consumidor que veja o
valor multiplicado — o que era exatamente a forma da dúvida.

---

## 2. `add_path`, instrução por instrução

`[BIN]` O despacho pelo byte de tipo, `0x96754`–`0x9677C`:

```
0x96754  ldrb w8, [x0, #0x2c]      ; o byte de tipo de forma
0x96758  cmp  w8, #4
0x9675C  b.gt #0x96780             ; 5..8  -> elipse, linha, …
0x96760  cmp  w8, #2
0x96764  b.gt #0x967f0             ; 3 e 4 -> o ramo do retângulo arredondado
```

`[BIN]` E dentro do ramo `{3, 4}`, `0x967F0`–`0x96884`:

```
0x967F0  ldr  q2, [x20, #0x10]     ; os quatro raios, como guardados
0x96814  mov  w9, #3
0x96820  cmeq v4.4s, v5.4s, v4.4s  ; (tipo == 3) ?
0x96824  mvn  v4.16b, v4.16b       ; -> máscara (tipo != 3), i.e. tipo == 4
0x9682C  mov  w9, #0xc8c9
0x96830  movk w9, #0x3f48, lsl #16 ; 0.78431374f  =  1 / 1.275
0x96838  fmul v5.4s, v2.4s, v5.4s  ; raios / 1.275
0x96840  bsl  v16.16b, v5.16b, v2.16b  ; tipo 4 -> divididos; tipo 3 -> crus
0x96844  cmp  w8, #3
0x96848  cset w0, ne               ; o booleano "contínuo"
0x96880  bl   _RBPathMakeRoundedRect
```

`[BIN]` **O tipo 3 (canto circular) passa o raio verbatim; o tipo 4 (contínuo)
divide.** A assimetria é a prova de que o `×1.275` pertence ao tipo 4 e a mais
nada — a mesma guarda aparece, com o mesmo `4`, no `encode` e no `decode`.

`[BIN]` E o `0x9673C`–`0x96750` desfaz a outra convenção de armazenamento do
`set_rounded_rect` — a normalização anisotrópica que o `0x217BC`–`0x217E0`
aplicou (`x` e `width` divididos pela razão de aspecto do canto, a razão guardada
como `float` em `+0x40`). `v2 = (aspecto, 1.0)`, e origem e tamanho voltam a
multiplicar por ela antes da chamada.

### 2.1. E o elemento sai com o bit certo

`[BIN]` `RBPathMakeRoundedRect` (`0x1132EC`) guarda o booleano em `args+0x40`, e o
emissor do elemento único (`0x118E10`) faz:

```
0x118E1C  ldrb w10, [x0, #0x40]    ; contínuo?
0x118E2C  orr  w1, w10, #8         ; RBPathElement = 8 | contínuo  ->  8 ou 9
0x118E30  add  x2, x0, #0x10       ; a carga: rect (4 doubles) + raios (2)
```

**`8 | 1 = 9`** — o `RBPathElement` 9 que `Mapper::apply_callback` (`0x80DCC`) lê
e entrega ao `add_rounded_rect`. A cadeia fecha ponta a ponta:

```
RBShape.setRoundedRect(r=266.24, style=1)
  -> set_rounded_rect 0x21780 : guarda 266.24 × 1.275 = 339.456, tipo 4
  -> Coverage::Primitive::add_path 0x96838 : 339.456 ÷ 1.275 = 266.24
  -> RBPathMakeRoundedRect 0x96880 : RoundedRectArgs{rect, 266.24, contínuo}
  -> emissor 0x118E2C : RBPathElement 9
  -> Mapper::apply_callback 0x80DCC/0x80E20 : add_rounded_rect(rect, 266.24, …)
```

---

## 3. O regime, com o número

`[BIN]` A folga é **por ARESTA**, e é calculada em `float` (`0x7F664`–`0x7F690`):

```
t = (|aresta| − (r_perto + r_longe)) / ((r_perto + r_longe) × 0.528664947)
```

Para o chiclet — aresta 1024, dois raios de 266.24, tudo em `float32`:

```
soma       = 532.47998046875
denominador= 532.47998046875 × 0.528664947 = 281.5035095214844
t          = 491.52002 / 281.50351          = 1.746052861213684   >= 1
```

`[BIN]` `0x7F6C4` (`b.pl`) leva ao ramo canônico. Os parâmetros são os do pool,
sem mistura:

| papel | valor | endereço |
|---|---|---|
| `extent` (quanto o canto come da aresta, em raios) | `1.5286649465560913` | `0x15EBC0` faixa 0 |
| `control` | `1.0884900093078613` | `0x15EBC0` faixa 1 |
| `shoulder` | `0.8684070110321045` | `0x15EC78` |

`[BIN]` E o contraste que trava a leitura: com `r' = 339.456` a mesma fórmula dá
`t = 0.9615 < 1`, caindo na mistura. **Os dois lados do impasse produziam cantos
diferentes; só um deles é alcançável pelo caminho que o codificador escreve.**

### 3.1. A folga é recalculada no meio do canto

`[BIN]` Detalhe que o laudo anterior não podia ver e que a transcrição precisa
honrar: `0x7F7BC`–`0x7F824` recalcula `t` **entre a segunda e a terceira cúbica
do mesmo canto**, agora com a largura e os dois raios horizontais. Cada canto
usa a folga da aresta de **entrada** nas suas duas primeiras cúbicas e a da
aresta de **saída** na terceira. Num quadrado de raios iguais dá no mesmo; num
retângulo estreito, não.

---

## 4. A curva, transcrita

`[BIN]` `add_rounded_rect` `0x7F580`–`0x7FE58`. Emissões contadas: `elt_moveto`
uma vez, `elt_lineto` quatro, `elt_cubeto` **doze** — três por canto, a
assinatura do contínuo (o circular emite uma, `0x7FCB4`–`0x7FE54`).

`[BIN]` O `elt_moveto` (`0x7F654`) sai do **meio de uma aresta**, não de um
canto: `y = ((minY + r) + (maxY − r)) / 2`, com os raios crus, `0x7F618`–`0x7F634`.

Um canto, em coordenadas locais — `e` é a direção para dentro ao longo da aresta
de entrada (raio `re`), `f` a da aresta de saída (raio `rf`), `C` o vértice:

| ponto | `e` × `re` | `f` × `rf` | endereço |
|---|---|---|---|
| início (fim do `lineto`) | `extent_e` | 0 | `0x7F708` |
| cúbica 1 · cp1 | `control_e` | 0 | `0x7F72C` |
| cúbica 1 · cp2 | `shoulder_e` | 0 | `0x7F73C` |
| cúbica 1 · fim | `0.63149398565292358` | `0.074911400675773621` | `0x7F758` / `0x15EBD0` |
| cúbica 2 · cp1 | `0.37282401323318481` | `0.16906000673770905` | `0x7F774` / `0x15EBE0` |
| cúbica 2 · cp2 | `0.16906000673770905` | `0.37282401323318481` | `0x7F78C` / `0x15EBF0` |
| cúbica 2 · fim | `0.074911400675773621` | `0.63149398565292358` | `0x7F7A4` / `0x15EC00` |
| cúbica 3 · cp1 | 0 | `shoulder_f` | `0x7F82C` |
| cúbica 3 · cp2 | 0 | `control_f` | `0x7F83C` |
| cúbica 3 · fim | 0 | `extent_f` | `0x7F84C` |

`[BIN]` **A tabela é simétrica em torno da diagonal do canto** — só dois números
distintos aparecem nos quatro pares. E os outros três cantos leem
`0x15EC10`–`0x15EC48`, que é `0x15EBD0`–`0x15EC00` com o sinal trocado
(`0x7F8C8`–`0x7F934`): é a mesma curva, girada.

`[BIN]` Confirmação numérica do pool, lida dos bytes:

```
0x15EBC0  1.5286649465560913     0x15EC78  0.86840701103210449
0x15EBD0 -0.074911400675773621   0x15EBD8 -0.63149398565292358
0x15EBE0 -0.16906000673770905    0x15EBE8 -0.37282401323318481
0x15EC80  floats (0.528664947, 0.128490031)   ; a inclinação da mistura
0x15EC88  floats (1.0,          0.959999979)  ; a base da mistura
0x15ECB8  float   0.528664947                 ; o divisor da folga
0x15ECBC  float   0.819999993                 ; base do shoulder
0x15ECC0  float   0.0484070182                ; inclinação do shoulder
```

`[BIN]` E a mistura reproduz o trio canônico em `t = 1` exatamente
(`1 + 0.528664947`, `0.96 + 0.128490031`, `0.82 + 0.0484070182`) — as duas poças
se validam uma à outra, como §6 já tinha notado.

---

## 5. O que foi implementado

**Arquivos novos**, para não disputar região com as frentes irmãs:

- `Source/RenderBox/ChicletShape.h` / `.cpp` — os parâmetros por aresta
  (`continuousCornerParams`), o contorno (`continuousRoundedRect`: 4 linhas + 12
  cúbicas), o raio (`chicletCornerRadius`), a cobertura antialiasada
  (`chicletCoverage`) e o recorte (`clipToChiclet`).
- `Tests/test_chiclet_shape.cpp` — seis casos que travam os números medidos.

**Toque em `IconRenderer.cpp`: um `#include`, uma linha, e a nota.** A linha é
`clipToChiclet(acc, options.size);` logo depois do `paintBackground`. A ordem
importa: pintar o quadrado inteiro e **depois** cortar mantém o parâmetro da
rampa mapeado ao canvas, que é o que os testes de `automatic` medem.

`[BIN]` O raio vem de `GlassMaterial.h:240`
(`defaultChicletCornerRadius = 266.24`, `+0x228`) contra o canvas de 1024 de
`Platform.extendedCanvasBounds` (`IconComposerFoundation 0x385D8`).
`266.24 / 1024 = 0.26` exatamente, então o raio escala com o canvas sem uma
segunda constante — um render de teste de 16 px e um de 1024 px são a mesma forma.

### 5.1. O que mudou no pixel

Área do contorno, integrada sobre a poligonal achatada em 256 segmentos por
cúbica:

```
área do chiclet  =   984 696.005 px²
área do canvas   = 1 048 576.000 px²   (1024²)
recortado        =    63 879.995 px²   =  6.0921 % do canvas
```

**O recorte tira 6.0921 % do quadrado, todo ele nos quatro cantos.** O meio de
cada aresta continua cheio — é por isso que os testes de rampa que amostram a
coluna central na primeira e na última linha continuam lendo as duas pontas da
rampa e não um buraco.

`[BIN]` Para referência, o canto come `1.5286649465560913 × 266.24 = 406.992` px
de cada aresta, sobrando `210.016` px de aresta reta entre dois cantos.

### 5.2. Suíte

**606 casos, 0 falhas** (era 600/0; os 6 novos são deste laudo), com
`IC_CORPUS_DIR=D:/CodingProjects/Icon-Composer/References/corpus`.

> **Armadilha registrada:** sem `IC_CORPUS_DIR` a suíte dá **600 casos, 25
> falhas** — `dir != nullptr` e `!files.empty()` em cinco arquivos. Não é
> regressão; é o corpus ausente. O contador de casos não muda, então o "verde
> vazio" aqui é vermelho vazio, e vale o mesmo cuidado.

---

## 6. O que continua `[OBS]`

- `[OBS]` **O recorte alcança o FUNDO e só ele.** Se o alvo corta também a arte
  das camadas ao mesmo contorno não foi lido. Uma arte que transborde o chiclet
  continua transbordando neste renderizador. A nota `kBackgroundShapeNote` diz
  isso em voz alta em vez de deixar a diferença invisível.
- `[OBS]` **A ordem de índice dos quatro raios** (`double4 a`, `double4 b` em
  `0x7F5B4`/`0x7F5B8`) foi lida só o bastante para ver que `[2]` é um canto e
  `[3]` o seguinte no giro. Para raios iguais não importa; para
  `setRoundedRect:cornerRadii:` importaria, e não foi fechada.
- `[OBS]` **O SDF do shader não foi comparado com a cúbica.** `set_globals`
  (`0x95434`) alimenta um primitivo analítico da GPU com o mesmo raio dividido;
  se a forma que o shader resolve coincide pixel a pixel com estas doze cúbicas é
  uma pergunta separada, e este laudo não a tocou.
- `[OBS]` **O `chicletOutset` de 32 não entrou no código.** `[BIN]` §5.1 do laudo
  anterior fixou `1088 = 1024 + 2×32`, e o `chicletBoundingPath` do macOS é um
  squircle em `(0,0,1024,1024)` — o chiclet preenche o quadrado do canvas, e o
  1088 pertence ao quadro estendido do watchOS. Nada aqui precisou dele; se o
  quadro estendido alguma vez virar o canvas de render, precisará.
- `[OBS]` **O rasterizador de cobertura é deste repositório, não do alvo.** Uma
  varredura por linhas com quatro sub-linhas e cobertura horizontal exata. O
  alvo resolve cobertura na GPU com os shaders que o `CoveragePass` já espelha; o
  que se afirma aqui é a **geometria**, não o filtro de antialiasing dela.

---

## 7. Endereços, para reprodução

| fatia | endereço | o que é |
|---|---|---|
| `RenderBox` | `0x216F8`–`0x21870` | `set_rounded_rect` (estilo 1 em `0x21780`) |
| `RenderBox` | `0x96720`–`0x969EC` | **`Coverage::Primitive::add_path`** — o codificador |
| `RenderBox` | `0x96838`/`0x96840` | o `÷1.275` e o `bsl` guardado pelo tipo |
| `RenderBox` | `0x96880` | `RBPathMakeRoundedRect` |
| `RenderBox` | `0x1132EC`–`0x11342C` | `RBPathMakeRoundedRect` |
| `RenderBox` | `0x118E2C` | `orr w1, w10, #8` — o `RBPathElement` 9 |
| `RenderBox` | `0x80CF8`–`0x80FA8` | `Mapper::apply_callback` (chama em `0x80E20`) |
| `RenderBox` | `0x7F580`–`0x7FE58` | `Mapper::add_rounded_rect` |
| `RenderBox` | `0x7F664`–`0x7F690` | a folga `t` da aresta |
| `RenderBox` | `0x7F704`–`0x7F860` | o primeiro canto: 1 linha + 3 cúbicas |
| `RenderBox` | `0x94538`/`0x94828` | as guardas `== 4` no `encode` e no `decode` |
| `RenderBox` | `0x16B88` / `0x16DF0` | `Coverage::visit_rounded_rect` — o mesmo `×1.275` fora do `RBShape` |
| `RenderBox` | `0x15EBC0`–`0x15EC48`, `0x15EC78`–`0x15ECC0` | as poças de constantes |
