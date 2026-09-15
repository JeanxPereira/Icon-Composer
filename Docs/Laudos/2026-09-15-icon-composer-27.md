# Laudo — Icon Composer 27, extração e diff contra 2.0-125 (15/09/2026)

Laudo de **extração e triagem de alvo**, não de comportamento de render. Pergunta
do usuário: este DMG novo (`Icon_Composer_27.dmg`, 19.897.344 bytes) é a stable, e
o vidro mudou "bastante" com o Calistoga? Resposta curta, com os campos que
decidem: **não é stable — é internal como a 2.0-125 era** — e **o vidro medido por
este projeto não mudou um único byte de código ou de shader** entre 2.0-125 e este
build. O que mudou é outra coisa, pequena e nomeada abaixo.

Extração em `D:\CodingProjects\Icon-Composer\References\27.0-129\` (fora do git,
só o `provenance.json` é versionado). Pipeline seguido conforme
`References/README.md`, com `AquaKit/References/scripts/` só-leitura.

---

## 1. Identidade do build

A pasta chama-se pela versão do app (`CFBundleShortVersionString-CFBundleVersion`
de `Contents/version.plist`), não pelo nome do DMG: **`27.0-129`**.

| campo | 2.0-125 (referência) | **27.0 (129)** — este DMG |
| --- | --- | --- |
| arquivo | `Icon_Composer_2_beta_6.dmg` | `Icon_Composer_27.dmg` |
| sha256 do DMG | `484786f7…6abb2a` | `7e776732…7b5b3d1` |
| tamanho do DMG | 19.812.864 bytes | 19.897.344 bytes |
| `CFBundleShortVersionString` | `2.0` | `27.0` |
| `CFBundleVersion` | `125` | `129` |
| `BuildVersion` (version.plist) | `212` | `7` |
| `ProjectName` interno | `IconStudio` | `IconStudio` |
| `CFBundleIdentifier` | `com.apple.IconComposer` | `com.apple.IconComposer` |
| `DTSDKName` | `macosx27.0.internal` | **`macosx27.0.internal`** |
| `DTSDKBuild` | `26A5388g` | `26A384` |
| `DTXcodeBuild` | `27A5252e` | `27A262` |
| `DTPlatformVersion` | `27.0` | `27.0` |
| `LSMinimumSystemVersion` | `26.4` | `26.6` |
| `BuildMachineOSBuild` | `23A344017` | `23A344017` (igual) |

`[BIN]` Todos os valores acima, lidos de `Contents/Info.plist` e
`Contents/version.plist` extraídos via `plistlib`, sem parâmetro.

### Stable ou beta? O campo que decide não mudou.

O teste que a 2.0-125 já estabeleceu é o `DTSDKName`: um SDK **`.internal`** é
assinatura de build interno, não público. No 27.0 (129), **o campo é
`macosx27.0.internal`, idêntico** — não virou `macosx27.0` puro nem qualquer coisa
sem o sufixo. `[BIN]`

Os outros dois sinais empurram na mesma direção, mas com menos força sozinhos:

- `DTXcodeBuild` `27A5252e` → `27A262`: ambos são builds de seed interna do Xcode
  27 (prefixo `27A`, sufixo de letra de seed); nenhum dos dois tem a forma de um
  Xcode GM público. `27A262` é lexicamente **menor** que `27A5252e` — não dá para
  concluir cronologia a partir disto sozinho, porque trens de build da Apple não
  são um contador único; é um dado `[OBS]`, não uma prova de ordem temporal.
- `LSMinimumSystemVersion` `26.4` → `26.6`: subiu, consistente com o tempo
  passando dentro do mesmo ciclo de desenvolvimento — não é o salto para um
  número de versão pública "limpo" que normalmente acompanha GM.
- `BuildVersion` (do `version.plist`, campo diferente de `CFBundleVersion`) caiu
  de `212` para `7`. Isso não é um contador global — é coerente com um contador
  que reinicia por linha maior (`2.0` teve seu próprio track até 212; `27.0`
  começou um novo). Não uso isto como prova de nada sozinho; é contexto. `[OBS]`

**Veredito**: os campos dizem **build interno**, igual à 2.0-125, não stable. A
crença do usuário de que este é o stable não se sustenta no único campo que este
projeto já tinha calibrado como discriminador (`DTSDKName`). Se existe uma stable
27 em algum lugar, este arquivo não é ela.

---

## 2. As quatro provas

**1. CRC-32 de cada blkx** (`udif.py --verify`): **6/6 batem** — MBR, Primary GPT
Header, Primary GPT Table, Apple_HFS, Backup GPT Table, Backup GPT Header, todos
`crc32 declarado == crc32 recalculado`. `VERDICT: all blkx checksums match`. `[BIN]`

**2. Contadores do volume**: o header HFS+ declara **64 arquivos, 45 pastas**; a
caminhada pelo catálogo (`hfs.py`, sem flags) acha **exatamente 64 e 45** —
`counts 64 files, 45 folders` / `walked 64 files, 45 folders`. Mesmos números da
2.0-125. `--extract` confirma de novo na saída (`extracted 64 files, 45 folders`).
`[BIN]`

**3. `_CodeSignature/CodeResources`** — o manifesto da Apple. Este volume tem
**8 manifestos** (main app + 5 frameworks + 2 appex — mesma contagem que
2.0-125). Escrevi um verificador (`plistlib` + `hashlib`, não editei nada em
AquaKit) que lê `files`/`files2` de cada `CodeResources` e recalcula SHA-256/SHA-1
de cada recurso contra o base correto (`Contents/` ou `Versions/A/`):

```
TOTAL entries=59  ok=52  missing=0  mismatch=0
```

Os 7 entries sem hash são registros `files2` de **nested code** (frameworks e
appex referenciados por `cdhash`+`requirement`, não por hash de arquivo — formato
esperado, não uma falha). **52 de 52 hashes de recurso conferem, zero ausente,
zero divergência.** Rodei o mesmo script contra a extração já validada de
2.0-125 e ele devolveu **exatamente os mesmos números** (`59/52/0/0`) — o método
é auto-consistente com o baseline conhecido-bom. `[BIN]`

**4. Decodificadores independentes**:
- `metallib_extract.py` (precisa de `llvmlite`; instalado na venv do projeto em
  `AquaKit/References/tools/pyre/`) carvou **109 módulos** do `RenderBox`
  `default.metallib` e **14** do `IconRendering` — mesma contagem que 2.0-125.
  Todo módulo desmontou em LLVM IR sem erro.
- `car_dump.py` abriu os dois `Assets.car` (BOM v1 válido): `IconComposerKit`
  com CoreUI-**1007**, 547 renditions, `Xcode 27.0 (27A262)`, batendo com o
  `DTXcodeBuild` do `Info.plist`; o do app principal com CoreUI-1007, 26
  renditions, mesmo Xcode build. (2.0-125 tinha CoreUI-1008 — uma casa abaixo
  aqui; `[OBS]`, não investiguei a causa.)

`[BIN]` para os quatro. Nenhum instrumento falhou.

### Censo de compressão — as armadilhas do README não morderam

```
64 files: 16 uncompressed, 31 método 4 (zlib/rsrc), 17 método 9 (raw/xattr)
VERDICT: every method on this volume is decodable
```

Mesmos números da 2.0-125 (31+17=48 comprimidos de 64). **Zero LZVN, zero LZFSE,
zero LZBITMAP, zero método 3.** As armadilhas nomeadas no README continuam
teóricas para este volume. Extents overflow: 0 registros nos dois volumes de
novo. `[BIN]`

---

## 3. O diff contra 2.0-125 — o vidro não mudou

### 3.1 Tamanho e sha256, lado a lado

| arquivo | 2.0-125 | 27.0-129 | veredito |
| --- | ---: | ---: | --- |
| `RenderBox/…/default.metallib` | 2.004.420 B | 2.004.420 B | **sha256 idêntico** |
| `IconRendering/…/default.metallib` | 94.060 B | 94.060 B | **sha256 idêntico** |
| 6 `Backgrounds/*.jpeg` | — | — | **sha256 idêntico** (todos os 6) |
| `RenderBox` (Mach-O, arm64) | 5.639.376 B | 5.639.376 B | mesmo tamanho, hash difere — **ver 3.3: só LINKEDIT** |
| `IconRendering` (Mach-O, arm64) | 2.252.160 B | 2.252.160 B | mesmo tamanho, hash difere — **ver 3.3: só endereços** |
| `CoreSVG` | 890.992 B | 890.992 B | mesmo tamanho, hash difere — não aprofundado |
| `IconComposerFoundation` | 3.671.632 B | 3.707.424 B | **+35.792 B, cresceu** — ver 3.4 |
| `IconComposerKit` | 6.776.624 B | 6.864.992 B | **+88.368 B, cresceu** — não aprofundado (UI/inspetor, fora do escopo do render) |
| `Icon Composer` (executável) | 161.296 B | 161.296 B | mesmo tamanho, hash difere — não aprofundado |
| `icrtool`, `ictool`, os 2 `Assets.car`, os 2 appex | mesmo tamanho cada | idem | hash difere — CLIs/UI, não aprofundado |
| `.DS_Store`, `.background@2x.png`, `AppIcon.icns`, todos os `Info.plist`/`version.plist` | — | — | tamanho difere — metadado de empacotamento, esperado |

`[BIN]` (sha256 via `hashlib.sha256` streaming, tamanhos via `stat`). 25 dos 64
arquivos comuns são **byte-a-byte idênticos**; isso já poda a maior parte da
busca sozinho, como o README previu.

### 3.2 Os shaders: módulos e nomes, não só a contagem

109 módulos no `RenderBox` metallib, 14 no `IconRendering` — mesma contagem que
2.0-125. Extraí a lista de **nomes de função definidos em cada módulo** (grep
`define …@nome(` nos `.ll` desmontados) dos dois builds: **369 nomes únicos no
RenderBox, 63 no IconRendering, e o `diff` entre as duas listas é vazio nos dois
casos.** Nenhum shader novo, sumido ou renomeado. Combinado com os metallibs
sendo byte-idênticos (3.1), isto não é surpresa — é confirmação pelo lado do
símbolo do que o hash já disse pelo lado do byte. `[BIN]`

### 3.3 Os dois Mach-O do render engine: o que "hash diferente, tamanho igual" realmente é

Carvei os slices arm64 (`scripts/slice_arm64.py`, thin de dentro do FAT) dos dois
builds e fiz diff byte a byte, classificando cada byte divergente pela seção
Mach-O que o contém (offsets de `LC_SEGMENT_64` lidos com `scripts/macho.py`):

**`RenderBox.arm64`** (2.772.176 bytes cada) — **413 bytes divergem em todo o
arquivo**, em 13 blocos contíguos, todos entre os offsets `0x29b24b` e
`0x2a17cb`. A última seção mapeada (`__thread_vars`) termina em `0x19eff8`
(1.697.960) — **os 413 bytes ficam inteiramente no LINKEDIT** (tabela de
símbolos/string table/assinatura de código), bem depois de qualquer seção de
código ou dado. **Zero bytes de `__text`, `__data`, `__const`, `__objc_*` ou
`__got` divergem.** O engine C++ do RenderBox é **byte-idêntico** entre os dois
builds — só a assinatura/linkedit muda, o que é esperado de qualquer
recompilação/reassinatura, mesmo sem uma linha de código mudando. `[BIN]`

**`IconRendering.arm64`** (2.252.160 bytes cada, mesma seção layout) — aqui
162.758 de 1.121.664 bytes divergem, mas **9.742 desses (1,7%) caem em
`__text`**; o resto está em `__objc_methname`/`__objc_methtype` (nomes/tipos de
seletor), `__const`, `__swift5_fieldmd`/`__swift5_reflstr`/`__swift5_typeref`
(metadado Swift), `__eh_frame`/`__unwind_info`, `__cstring`, `__oslogstring`. A
causa raiz aparece ao normalizar os endereços do `fieldmd` (abaixo): **toda
descriptor Swift no arquivo novo está exatamente +0x50 (80 bytes) à frente da
mesma descriptor no arquivo velho** — um deslocamento uniforme, não uma reescrita.
Isso empurra ADRP/imediatos de toda função que referencia uma string ou símbolo
Swift, produzindo exatamente o padrão observado: milhares de diffs de 1–2 bytes
espalhados por `__text`, sem tocar a opcode em si.

Conferi isto diretamente nos **nove endereços `[BIN]` citados nos commits mais
recentes deste projeto** (highlights/chiclet — a superfície que este trabalho
mais fixou em selo): `0x35DF0` (init `lightAngle`), `0x4266C`/`0x42884`
(`lightIntensity`/leitor de ctx), `0x1263C`/`0x12650` (reescrita da direção),
`0x494D8` (`glyphHighlightsUseVCM`), `0x12550` (`spatialHighlighting`),
`0x475A0`/`0x5E590`/`0x5E59C`/`0x62588`/`0x62A78`/`0xD904` (portão do chiclet,
thunk, closure, Default/Bright/Dim, gradiente cônico) e `0x63608`/`0x63624`
(`chicletClear`). **Sete das nove janelas de 32 bytes são byte-idênticas.** As
duas exceções, `0x49C78` e `0x63624`, desmontam para a **mesma instrução** nos
dois builds — só o imediato de um `adrp`/offset de tabela mudou, e no caso de
`0x63624` confirmei com `scripts/macho.py dis` que é literalmente
`ldr q0, [x8, #0x740]` → `ldr q0, [x8, #0x790]`, um deslocamento de **exatamente
0x50** — o mesmo deslocamento uniforme do `fieldmd`. Mesma tabela de constantes,
uma posição adiante. `[BIN]`

**Conclusão da seção**: nenhuma das duas superfícies de código do render engine
mudou semanticamente. RenderBox é byte-idêntico em código; IconRendering
recompilou com um deslocamento de layout uniforme que reordena tabelas sem
alterar lógica, valores ou estrutura.

### 3.4 Os descritores de reflexão Swift — o equivalente do `fieldmd_iconrendering.txt`, gerado e diffado

Gerei `References/27.0-129/out/fieldmd_iconrendering.txt` com `swift_meta.py
--fields` (auto-detecta `__swift5_fieldmd`, sem parâmetro manual — mesmo comando
implícito da 2.0-125). **197 descriptors nos dois builds, 1002 linhas nos dois,
127 campos externos/indiretos nos dois.** Normalizando o endereço (regex
`^0x[0-9a-f]+` → placeholder), o diff do conteúdo é vazio a menos de **10 linhas**
de bytes crus embutidos em tipos `Say…` (array com ponteiro indireto), que são
exatamente o resíduo esperado do deslocamento de +0x50 bytes descrito acima —
não uma mudança de campo. `[BIN]`

Gerei também `fieldmd_foundation.txt` para `IconComposerFoundation.arm64`
(**267 descriptors contra 264, +3**). Isolei os 3 novos, ignorando o ruído
cosmético do demangler para contextos anônimos (`<anon>`/`<opaque>`/prefixo de
extensão, que trocam de nome entre builds sem mudar de conteúdo):

- `IconComposerFoundation.PreviewSizeCustomization` (struct, 2 campos)
- `IconComposerFoundation.AllPreviewSizeCustomizations` (struct, 1 campo)
- `IconComposerFoundation.PreviewSizeCustomization.<anon>.CodingKeys` (enum, 2 campos)

Isso é uma feature nova e nomeada — **customização de tamanho de preview** — sem
relação com vidro. Fora isso, todos os outros 264 descriptors batem tipo a tipo,
campo a campo, **incluindo os dois estruturalmente citados pelo brief**:
`Group` (16 campos, `_specular` na mesma posição) e a classe de `Icon.Layer`
(12 campos, `_isGlass` na mesma posição, mesmo tipo `<indirect-external>` nos
dois). `[BIN]`

### 3.5 Veredito do vidro

Tudo que este projeto mede sobre "o vidro" mora em três lugares: o metallib
compilado (3.1: **idêntico**), o código C++/ASM que monta os parâmetros e
rasteriza (3.3: **RenderBox idêntico em código; IconRendering idêntico em
lógica, deslocado em endereço**), e a estrutura Swift que descreve os campos
(3.4: **idêntica salvo uma feature não relacionada**). **Nenhuma das três mudou
entre 2.0-125 e este build.** A frase do usuário — "a parte do vidro mudou
bastante no 27 com o Calistoga" — não se confirma nestes dois arquivos
específicos. Não afirmo que a frase esteja errada em geral: talvez descreva uma
build ainda não capturada, ou uma mudança em outro lugar do app (UI, não
render engine) que este laudo não foi instruído a auditar a fundo
(`IconComposerKit` cresceu 88 KB e não foi aprofundado — `[OBS]`). O que afirmo,
com endereço e byte, é que **estes dois arquivos específicos, comparados,
não mudaram o vidro.**

---

## 4. Os números concretos — o que reconferi e o que não

Dado que RenderBox está byte-idêntico em código (3.3) e o `fieldmd` do
IconRendering confere campo a campo (3.4), **todos os números `[BIN]` do
render engine citados nos laudos anteriores continuam válidos sem reconferência
individual** — eles vêm dos mesmos bytes. Não há necessidade de re-medir
`ICRRenderingParameters.Highlights` (`0x3EF1`), os passos `0x630`/`0x108`,
`chicletHighlightsAppearanceMode`/`maxDimChicletLuminance` 0.2/`minBrightChicletLuminance`
0.99, `ICRRenderingParameters.Shadow` (`0xF8`, 15 campos), `ringWidth`
`[16,16,16,16]`, `vibrantBrightness` 0.75, o `×1.275`/`0.7843137` do
`set_rounded_rect`, `defaultChicletCornerRadius` 266.24, o código de
interpolação 4, `blurStrengthMax` 64.0 e a escada `3.5/5.25/7.0` — todos vivem em
bytes que não mudaram.

O que **reconferi diretamente** aqui, e não apenas por herança do byte-idêntico:

- `Icon.Layer._isGlass.defaultValue` e `Group._specular.defaultValue`: os
  campos existem nas mesmas posições estruturais (3.4). **Não reli o valor do
  default** (isso pede desmontar o inicializador, não só o `fieldmd`) — sinalizo
  como `[OBS]` porque herdar do byte-idêntico vale para RenderBox/IconRendering,
  não para IconComposerFoundation, que **cresceu** 35.792 bytes.
- `chicletClear` (`0x63608`/`0x63624`): reconferido por desmontagem direta —
  mesma instrução, offset de tabela deslocado em 0x50. Não é o `[BIN]` "novo"
  citado no commit mais recente (o primeiro `Bright`/`Dim` real de verdade) —
  isso segue precisando de leitura própria, porque o deslocamento por si não diz
  se o *conteúdo* daquela posição de tabela é igual; só diz que a instrução que
  o lê é igual. Não abri esse ponto neste laudo.

## 5. O custo de adoção, com número

**A favor de trocar o alvo para 27.0-129:**
- Toda a superfície que este projeto fixou em selo `[BIN]` — RenderBox inteiro,
  os campos de `IconRendering` que o `fieldmd` cobre, os dois metallibs — **não
  precisa de re-trabalho**. Não é "quase igual": é byte-idêntico ou
  estruturalmente idêntico, medido, não assumido.
- O gate de round-trip byte-exato deste projeto testa contra o comportamento do
  render, e o render não mudou nos dois arquivos centrais. Não há sinal de que
  ele quebraria.

**Contra, ou o custo real:**
- Este **não é o alvo estável** que o usuário queria adotar como novo padrão —
  é outro build interno, da mesma família de seed que 2.0-125. Adotá-lo como
  "o alvo 27" tem o mesmo status epistêmico que 2.0-125 já tinha: uma leitura de
  build interno, sujeita a mudar de novo antes do GM.
- `IconComposerFoundation` cresceu 3 tipos novos (preview size) e
  `IconComposerKit` cresceu 88 KB — nenhum dos dois foi auditado a fundo aqui.
  Se a decisão for trocar o alvo dos **145 documentos do corpus**, essas duas
  superfícies precisam de uma passada própria antes de reselar qualquer coisa
  que dependa delas (o painel de camadas, o inspetor, a exportação).
- Re-selar 145 documentos por causa de um build que, medido, não mudou o vidro,
  é custo sem ganho correspondente **agora**. O ganho apareceria se e quando
  aparecer uma stable de verdade, ou uma build onde `IconComposerFoundation`/
  `IconComposerKit` realmente mexerem em algo que os laudos citam.

**Recomendação, não decisão**: não trocar o corpus por este build. Guardar a
extração (já feita, já provada pelas quatro provas) como referência de que "27.0
(129) não muda o vidro contra 2.0-125", e esperar por um DMG com `DTSDKName` sem
`.internal` antes de pagar o custo de reselar os 145 documentos.

---

## 6. O que NÃO foi feito (honestidade)

- `CoreSVG`, `Icon Composer` (executável principal), `icrtool`, `ictool`, os
  dois `Assets.car`, os dois appex: tamanho idêntico, hash diferente,
  **não desmontados nem checados byte-a-byte por seção**. `[OBS]`.
- `IconComposerKit` (+88 KB): não gerado `fieldmd` equivalente, não procurado
  o que cresceu. `[OBS]`.
- O valor do default de `_isGlass`/`_specular` (não só a existência do campo):
  não desmontado. `[OBS]`.
- O byte novo em `0x63624` (a tabela de cores `Default`/`Bright`/`Dim` do
  chiclet): confirmei que a INSTRUÇÃO é igual e o deslocamento é o mesmo +0x50
  uniforme visto em todo o binário — não confirmei que o CONTEÚDO da tabela
  naquele offset é igual. Tratando isso como consistente com o resto do
  binário (que é), não como provado byte a byte.
- Não toquei `Docs/03-o-motor-de-render.md` nem `Docs/README.md`, conforme
  instrução.

---

## Instrumentos

`AquaKit/References/scripts/udif.py`, `hfs.py`, `metallib_extract.py`,
`car_dump.py`, `swift_meta.py` (só-leitura); `scripts/slice_arm64.py` e
`scripts/macho.py` deste repo; um verificador de `CodeResources` e três
comparadores de bytes escritos para este laudo (hash de árvore, ranges de diff
por seção, janelas pontuais) — nenhum grava em `References/corpus` nem em
AquaKit.
