# Laudo — para onde vai o tempo de um render (29/09/2026)

Task R1 de `Docs/Plans/2026-09-29-render-tempo-real.md`. Mede o **nosso**
renderizador, nesta máquina, com o binário da árvore `ba3b9ff`. Não lê o alvo,
então não há `[BIN]` nem `[ART]`: o que está aqui é `[INF]` (medição minha) e
`[OBS]` (o que a medição não fecha).

---

## 0. A frase

**Um quarto do render é ler a cobertura de volta da GPU, e quase nada disso é
a GPU.** Dos 0,39 s do `readBack` no Apollo a 1024 px, 0,24 s são um `memcpy`
de memória mapeada sem cache e 0,13 s são uma conversão de half para float
feita um texel por vez. A GPU em si (criar o pipeline, desenhar, copiar)
custa 0,04 s no documento inteiro.

---

## 1. Método

- Release, `-DIC_BUILD_UI=OFF`, na worktree descartável criada de `ba3b9ff`.
  **Nada da instrumentação entrou no repo.**
- Um cronômetro RAII (`steady_clock`) na primeira linha de cada função da
  cadeia, inserido por script. Ele mede tempo **inclusivo**, conta as chamadas,
  mede só a thread principal e, quando a função chama a si mesma, só a chamada
  de fora conta. Uma função que roda dentro de outra aparece nas duas linhas.
- O `readBack` foi dividido em quatro fases por cronômetros no meio dele.
- Uma execução por número (a regra da memória: medir com uma execução, não
  com um laço). A soma das partes bate com o total de `renderIcon` dentro de
  poucos centésimos, e o total bate com o `icrender` sem instrumentação
  (1,69 s contra 1,71 s da decomposição por variantes do plano).
- Documento: `Apollo-Reborn__Apollo-Reborn__AppIcon`, o mais pesado do corpus.
  7 das 10 camadas desenham (3 são puladas pela própria cadeia, com motivo).

---

## 2. O perfil, Apollo a 1024 px

`renderIcon` = **1,670 s**.

| etapa | s | fatia | chamadas | o que tem dentro |
|---|---|---|---|---|
| **`readBack` da cobertura** | **0,391** | **23 %** | 13 | `memcpy` da memória mapeada 0,239 · half→float 0,132 · alocar o staging 0,012 · copiar na GPU e esperar 0,008 |
| **`shadowImage`** | **0,377** | **23 %** | 7 | `gaussian` 0,213 · `shadowRingMask` 0,062 · cópia da arte, zerar cor e `translate` ~0,10 |
| **campo de distância** | **0,325** | **19 %** | 8 | EDT (`fieldFromInsideMask`) 0,278 · cobertura dos contornos 0,040 · rasterizar 0,005 |
| `drawChicletHighlights` | 0,117 | 7 % | 1 | inclui um dos 8 campos: o da pastilha |
| `drawSpecular` | 0,109 | 7 % | 7 | |
| `blendOver` | 0,104 | 6 % | 21 | ~5 ms por chamada: um passo sobre 4 M floats |
| resto do `renderSvgPlaced` | ~0,10 | 6 % | 7 | acumular a cobertura na CPU, forma a forma |
| GPU de verdade | 0,037 | 2 % | | `CoveragePass::create` 0,006 · `draw` 0,023 · `Image::create` 0,010 |
| fundo | 0,047 | 3 % | | `paintBackground` 0,031 · `clipToChiclet` 0,016 |
| translucidez | 0,035 | 2 % | | máscara 0,027 · furos 0,008 |
| `shadowOverdrawImage` | 0,030 | 2 % | 7 | |

A 512 px o total é 0,504 s e a ordem é a mesma: `renderSvgPlaced` 0,17
(`readBack` 0,11), sombra 0,11, campo 0,10. **Tudo escala com a área**, o que
diz que nada disso é custo fixo por chamada. A exceção é o pipeline e a imagem
da GPU, recriados a cada camada, que custam pouco.

### 2.1. O documento leve não sofre disso

Jellify, 1 camada, 1024 px, 0,314 s: `drawChicletHighlights` 0,115 (37 %),
campo 0,096, sombra 0,055. **Zero `readBack`**: a arte dele não passa pela
cobertura. Por isso o custo do vaivém com a GPU nunca apareceu em quem media o
documento leve.

---

## 3. Três fatos que decidem as tasks seguintes

**`[INF]` 3.1. O `memcpy` lento é a memória sem cache.** O staging é alocado
com `HOST_VISIBLE | HOST_COHERENT` e sem `HOST_CACHED`. São 4 MB por chamada em
~18 ms, uns 220 MB/s, a velocidade típica de ler memória combinada para
escrita pela CPU. Pedir `HOST_CACHED` (com um `vkInvalidateMappedMemoryRanges`
se ela não for coerente) deve levar essa linha para perto de zero. **Os bytes
lidos são os mesmos**, então a saída não muda.

**`[INF]` 3.2. A conversão half→float é exata e pode ser vetorizada.** Um half
IEEE vira float sem arredondamento, então a instrução F16C (`vcvtph2ps`) dá
exatamente o mesmo float que o `_Float16` escalar. `[OBS]` Não medi quanto o
compilador já vetoriza hoje; os 10 ms por milhão de texels sugerem que não
vetoriza.

**`[INF]` 3.3. O que se repete entre duas edições é quase tudo.** Numa edição
de opacidade ou de cor de um grupo, a arte, a posição e o tamanho de toda
camada continuam os mesmos. O que depende só desses três: a cobertura
(`readBack` e acúmulo, ~0,49 s), o campo (0,33 s), a sombra antes da mistura
(0,38 s) e os realces da pastilha, que dependem só do tamanho, da plataforma
e do fill (0,12 s). Isso soma **~1,3 s dos 1,67 s**. Com cache, uma edição
dessas no Apollo a 1024 px custaria na ordem de 0,3 s. `[OBS]` Esse número é
uma subtração, não uma medição. Ele sai medido na R3.

---

## 4. O que o canvas pede, e quanto custa

- O canvas pede **512 px** por padrão (`Session.h:62`); ladrilho na
  resolução da tela quando há zoom.
- A barra de renditions pede miniaturas de **128 px**. Apollo a 128 px:
  **0,098 s**.
- `SharedScheduler` dá prioridade ao canvas, mas **não interrompe** um render
  em voo. Uma edição que chega com uma miniatura sendo desenhada espera até
  ~0,1 s por ela no Apollo. `[OBS]` Não medi com que frequência isso acontece.

Apollo, render inteiro: 0,10 s a 128 px, 0,22 s a 256, 0,50 s a 512, 1,76 s
a 1024.

---

## 5. O que isto autoriza, em ordem de ganho por esforço

1. **R4a — `readBack` com memória `HOST_CACHED` e conversão vetorizada.**
   Duas mudanças locais em `Image.cpp`, e a saída continua idêntica byte a
   byte. Ganho esperado: ~0,3 s de 1,67 no Apollo a 1024 px.
2. **R3 — cache entre quadros**, por camada: cobertura, campo e sombra antes
   da mistura, com a chave sendo arte + posição + tamanho + viewport. Os
   realces da pastilha entram com a chave tamanho + plataforma + fill. É o
   maior ganho, mas só vale para edições; o primeiro render não ganha nada.
3. **R2 — prévia progressiva a 256 px** quando o canvas pede 512. Custa ~0,2
   s no documento mais pesado do corpus e dá imagem já no primeiro quadro.
4. **R4b — `gaussian` da sombra (0,21 s) e EDT (0,28 s).** Já rodam em várias
   threads desde 19/09 (`Parallel.h`). O que sobra é algoritmo. Precisa de
   leitura antes de virar task.

**Não autoriza** mexer na GPU: ela custa 2 % do render. O custo está em
transportar a saída dela para a CPU e em tudo que roda na CPU depois disso.

---

## 6. Depois da R3 — o cache medido, e o que o gate dele pega

### 6.1. O ganho

Apollo, Release, um `RenderCache` para a sequência inteira. Cada linha é uma
edição sobre a anterior, como no canvas:

| | 512 px | 1024 px |
|---|---|---|
| primeiro render (frio) | 0,40 s | 1,35 s |
| o mesmo render de novo | 0,09 s | 0,40 s |
| opacidade de uma camada | 0,09 s | 0,39 s |
| cor de uma camada | 0,12 s | 0,48 s |
| posição de uma camada | 0,14 s | 0,52 s |
| translucidez de um grupo | 0,18 s | 0,65 s |
| fundo do documento | 0,12 s | 0,48 s |
| memória retida | 88–132 MB | 352–528 MB |

`[OBS]` O piso quente (0,09 s / 0,40 s) não foi decomposto. Ele contém o hash
das entradas grandes (a arte de cada camada entra inteira na chave da
sombra), as cópias de volta do cache e o que não é cacheado: `blendOver`,
`drawSpecular`, a máscara de translucidez.

### 6.2. A detecção, executada

A regra 3 do plano: o gate (`Tests/test_render_cache.cpp`) tem de reprovar
quando a chave é mutilada. Treze mutilações, uma por vez, cada uma aplicada
sobre o arquivo intocado e restaurada com SHA-256 conferido:

| tirado da chave | resultado |
|---|---|
| svg: a posição | pega |
| svg: as opções (e a tinta dentro delas) | pega |
| svg: o texto do arquivo | pega, **depois** de um caso novo (abaixo) |
| tinta: a cor | pega |
| campo: os contornos | pega |
| campo: as `FieldOptions` | pega, **depois** de um caso novo (abaixo) |
| campo raster: a arte | pega |
| sombra: a arte | pega |
| sombra: o estilo | pega |
| sombra: a geometria | **não pega** |
| pastilha: o acumulador | pega |
| pastilha: a plataforma | **não pega** |
| pastilha: a grade | **não pega** |

Duas lacunas eram do gate, e foram fechadas:

- **O texto do svg.** Todo svg do corpus tem `viewBox` próprio, e o `viewBox`
  entra na posição, então trocar de arquivo já mudava a chave pela posição.
  O caso novo escreve uma variante de `Eyes 3.svg` com o mesmo `viewBox` e a
  GEOMETRIA mexida. A primeira tentativa trocou uma COR dentro do arquivo e
  não pegou: a tinta que o render aplica por cima cobre as cores do arquivo, e
  a troca não mudava um pixel.
- **A origem do campo.** A 256 px a margem do plano (o alcance da sombra)
  engolia o canvas inteiro, e os dois ladrilhos viravam o mesmo buffer. O caso
  passou para 1024 px, com dois ladrilhos de mesmas dimensões no interior.

As três que ficaram são **redundantes na prática**, e ficam na chave mesmo
assim:

- a **geometria da sombra** sai de `size` e `sizeClass`; `size` já está nas
  dimensões e na arte, e as quatro classes têm os mesmos números nesta versão
  dos parâmetros;
- a **plataforma** e a **grade** da pastilha mudam o acumulador ANTES dos
  realces (o recorte e o degradê do fundo dependem delas), e o acumulador
  inteiro está na chave. `[OBS]` Um fundo de alfa zero, que zeraria o
  acumulador nas duas plataformas, também não separou: os realces não
  desenham nada ali.
