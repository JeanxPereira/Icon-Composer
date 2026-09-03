# 01 — O formato `.icon`

O documento que o Icon Composer edita, lido do binário que o escreve e conferido
contra os documentos que ele escreveu.

## As duas fontes

**O binário.** `IconComposerFoundation.framework`, fatia arm64, de
`Icon Composer 2.0 (125)`. O instrumento é o `swift_meta.py` sobre
`__TEXT.__swift5_fieldmd` (13.236 bytes, 264 descritores): um tipo `Codable` do
Swift declara um `enum CodingKeys` cujos casos são as chaves do JSON, e a
metadata de reflexão os carrega em claro.

```powershell
python <AquaKit>\References\scripts\swift_meta.py `
    References\2.0-125\out\slices\IconComposerFoundation.arm64 --fields
```

**O corpus.** 145 documentos `.icon` reais, de 145 projetos públicos distintos —
Sparkle, Gifski, Transmission, VLC, Signal, Zotero, Nextcloud, Shopify, Delta,
Tropy e outros. Escritos por gente diferente, pelo app real, exercitando cantos
diferentes. Reproduzir com `scripts/fetch-icon-corpus.py`; a proveniência de cada
um está em `References/corpus/corpus.json`.

As duas são independentes: uma diz o que o formato **pode** ter, a outra o que
ele **tem**. Onde discordam, o documento diz qual venceu.

## Selos

| selo | significa |
|---|---|
| `[BIN]` | lido do `__swift5_fieldmd`. Exato. |
| `[ART]` | contado nos 145 documentos do corpus, com a contagem junto. |
| `[INF]` | leitura minha ligando as duas. Marcado onde ocorre. |

## 1. Como um nome do Swift vira uma chave no disco

`[BIN]`+`[ART]` As chaves são **kebab-case**, e o prefixo `is` de booleano cai:

```
supportedPlatforms    -> supported-platforms      isGlass  -> glass
translationInPoints   -> translation-in-points    isHidden -> hidden
```

`[ART]` **E os valores seguem a mesma regra**: `layerColor` → `layer-color`,
`plusDarker` → `plus-darker`, `systemLight` → `system-light`,
`specularLocation` → `specular-location`.

**A regra tem pelo menos uma exceção, e o corpus a achou.** O `CodingKey` selado
`assumedSVGColorSpace` apareceria como `assumed-svg-color-space`; o que os
documentos escrevem é **`color-space-for-untagged-svg-colors`** (20 de 145).
`[INF]` É o mesmo campo — os dois são de raiz, os dois valem `display-p3`, e
nenhum outro candidato existe — mas o nome no disco é um raw value explícito, não
uma derivação. Logo: **a regra prevê, não decide.** Onde um `CodingKey` selado
não aparece no corpus, o nome dele em disco é pergunta aberta, não dedução.

## 2. O documento

`[BIN]` A raiz, de `JSONContent.CodingKeys`: `features`, `groups`, `fill`,
`fill-specializations`, `specializable-language-ids`,
`color-space-for-untagged-svg-colors`, `supported-platforms`,
`implicit-asset-mirroring`.

`[ART]` As cinco combinações de raiz que ocorrem, e só elas:

| n | chaves de raiz |
|---|---|
| 102 | `fill`, `groups`, `supported-platforms` |
| 20 | `fill-specializations`, `groups`, `supported-platforms` |
| 14 | `color-space-for-untagged-svg-colors`, `fill-specializations`, `groups`, `supported-platforms` |
| 6 | `color-space-for-untagged-svg-colors`, `fill`, `groups`, `supported-platforms` |
| 3 | `features`, `fill`, `groups`, `supported-platforms` |

`groups` e `supported-platforms` estão em 145 de 145 — são o núcleo obrigatório.
`fill` e `fill-specializations` são alternativos: nunca aparecem juntos.
`specializable-language-ids` e `implicit-asset-mirroring` **não ocorrem nenhuma
vez**.

`[BIN]` `Snapshot.Feature`: `refractivity`, `specularLocation`. `[ART]` As duas
ocorrem, em 3 documentos: `specular-location` (3), `refractivity` (2).

## 3. O grupo

`[BIN]` Todo campo vem em par — o valor e a lista que o sobrescreve:

| propriedade | `[ART]` n | par de especialização | `[ART]` n |
|---|---|---|---|
| `name` | 524 | — | |
| `layers` | 271 | — | |
| `hidden` | 268 | `hidden-specializations` | 102 |
| `opacity` | 364 | `opacity-specializations` | 161 |
| `blur-material` | 123 | `blur-material-specializations` | 36 |
| `refractivity` | 5 | `refractivity-specializations` | 0 |
| `shadow` | 240 | `shadow-specializations` | 31 |
| `translucency` | 203 | `translucency-specializations` | 68 |
| `specular` | 103 | `specular-specializations` | 51 |
| `blend-mode` | 119 | `blend-mode-specializations` | 87 |
| `lighting` | 82 | `lighting-specializations` | 17 |
| `position` | 338 | `position-specializations` | 38 |
| `asset-mirroring` | 0 | `asset-mirroring-specializations` | 0 |

`[BIN]` O binário também declara `legacy-refractivity-strength`,
`legacy-refractivity-depth` e `legacy-specular-highlight`, cada um com o seu par.
`[ART]` **Nenhum dos três ocorre no corpus.** O app 2.0 lê o legado e escreve o
novo; um escritor nosso não precisa emiti-los.

## 4. A camada

`[BIN]` `kind`, `name`, `position`, `fill`, `material`, `opacity`, `blend-mode`,
`hidden`, `glass`, `asset-mirroring`, `image-name`, cada um com o seu
`*-specializations` exceto `kind` e `name`.

`[ART]` `image-name` 408, `glass` 225, `glass-specializations` 55,
`image-name-specializations` 29. **`material` não ocorre** — a camada tem o campo
no binário e nenhum documento o usa; quem carrega o número é o `blur-material` do
grupo (123).

## 5. A especialização

`[BIN]` `idiom`, `appearance`, `localization`, `language-direction`, `value`.

`[ART]` 890 listas de especialização, 1.740 entradas. Os predicados que ocorrem:
`appearance` sozinho (1.040), **nenhum** (616), `idiom` sozinho (82), e
`appearance` + `idiom` (2). `localization` e `language-direction` **não ocorrem
uma única vez**.

### A regra de resolução

`[ART]` Medida, não suposta:

- Uma lista tem **no máximo uma** entrada sem predicado, e quando ela existe está
  **sempre no índice 0** — 616 de 616.
- **Zero listas repetem um predicado.** Não há duas entradas com o mesmo
  `(idiom, appearance)`.
- **Zero listas são ambíguas.** Varrendo as 890 listas contra todas as 20
  combinações de `appearance` × `idiom`, nunca acontece de duas entradas de mesma
  especificidade casarem o mesmo contexto.

Logo a regra é determinística sobre este corpus: **entre as entradas cujo
predicado casa o contexto, vence a mais específica; a entrada sem predicado é o
default**. E ela é exercitada — duas listas contêm uma entrada `appearance` ao
lado de uma `appearance`+`idiom`, que é exatamente onde a especificidade decide.

`[ART]` 190 listas **não têm** entrada sem predicado. Para um contexto que não
casa nenhuma delas, não há valor especializado, e vale a propriedade base.

`[INF]` Que a regra seja "mais específica vence" e não "a última que casa vence"
não é distinguível por este corpus: sem repetição de predicado e sem ambiguidade,
as duas leituras dão o mesmo resultado em 890 de 890 listas.

## 6. Os vocabulários

`[BIN]` fechados pelo binário; `[ART]` o que o corpus efetivamente usa.

| tipo | `[BIN]` casos | `[ART]` observados |
|---|---|---|
| `Appearance` | base, light, dark, tinted | dark(585), tinted(425), light(32) — `base` nunca escrito |
| `Idiom` | base, square, iOS, macOS, watchOS | square(57), macOS(20), watchOS(7) |
| `BlendMode` (**18 casos**, doc 03 §17.3; estes 10 ocorrem) | normal, plusLighter, plusDarker, overlay, multiply, softLight, hardLight, darken, lighten, screen | normal(112), screen(2), plus-darker(2), plus-lighter(1), soft-light(1), multiply(1) — e em `value`: overlay, hard-light, darken |
| `Fill.Kind` | none, automatic, solid, automaticGradient, linearGradient, systemLight, systemDark | automatic(34), none(16), system-light(10), system-dark(1), mais as formas de objeto (§7) |
| `Shadow.Kind` | automatic, neutral, layerColor, none | neutral(190), layer-color(87), none(25) |
| `SpecularHighlight` | off, automatic, inside, outside | inside(3) |
| `Lighting` | individual, combined | individual(64), combined(18) |
| `SupportedPlatforms` | squares, circles; cada um shared ou unique | `shared`(117) como string, ou array de plataformas |
| `Platform` | iOS, macOS, watchOS | macOS(25), iOS(6) em `squares[]`; watchOS(97) em `circles[]` |
| `AssumedSVGColorSpace` | displayP3 | display-p3(20) |
| `EffectsRenderMode` | disabled, designGeneration26, designGeneration27 | não é campo do documento |
| `Color.SystemColorName` | 13 casos, de systemRed a systemCyan | não ocorre |

**Nenhum valor fora dos vocabulários selados, em 145 documentos.** O binário é
superconjunto estrito do corpus, que é exatamente o que deveria acontecer.

`[BIN]` **`BlendMode` tem dez casos, não dezessete.** O trabalho anterior deste
projeto implementou dezessete modos em WGSL; sete não são alcançáveis a partir de
um `.icon`, porque o formato não sabe nomeá-los.

## 7. A gramática dos valores

`[ART]` A forma que cada chave aceita, contada no corpus. Uma chave com mais de
uma forma é polimórfica de verdade, não erro de leitura.

| chave | formas observadas |
|---|---|
| `fill` | `string`(61) · `{solid}`(57) · `{automatic-gradient}`(50) · `{linear-gradient}`(23) |
| `solid` | `string` — uma cor |
| `automatic-gradient` | `string` — **uma** cor; o resto do gradiente é derivado |
| `linear-gradient` | `array` de cores |
| `orientation` | `{start, stop}`, cada um `{x, y}` numéricos |
| `position` | `{scale, translation-in-points}` |
| `translation-in-points` | `array` de dois números |
| `shadow` | `{kind, opacity}` |
| `translucency` | `{enabled, value}` |
| `refractivity` | `{enabled, strength, depth}` |
| `blur-material` | `number`(75) · **`null`(48)** |
| `specular` | `bool`(100) · `string`(3) |
| `squares` | `string`(117) · `array`(28) |
| `circles` | `array`(97) |
| `opacity`, `scale`, `x`, `y`, `depth`, `strength` | `number` |
| `hidden`, `glass`, `enabled` | `bool` |
| `value` (na especialização) | `number`(730) · `bool`(410) · `string`(324) · `{solid}`(222) |

Três dessas merecem nome:

- **`blur-material` é anulável.** 48 de 123 são `null`, e `null` não é o mesmo
  que ausente: o campo está lá, dizendo explicitamente "sem material".
- **`specular` tem duas formas.** Booleano em 100 casos, string em 3. `[INF]` O
  booleano é o campo novo (ligado/desligado) e a string é o enum
  `SpecularHighlight` de quatro casos — o único valor visto, `inside`, é caso
  dele. Não confirmado.
- **`value` é polimórfico pela propriedade que especializa**, não por si. Um
  `opacity-specializations` traz número; um `hidden-specializations` traz
  booleano; um `image-name-specializations` traz o nome do arquivo
  (`background-dark.png`, `squircle-light.svg`); um `fill-specializations` traz
  string ou `{solid}`. Um parser tem de saber qual lista está lendo antes de
  saber o que `value` é.

## 8. As cores

`[ART]` Uma cor é a string `"<espaço>:<componentes>"`, com cinco casas decimais.
Os espaços observados e a aridade de cada um:

| espaço | n | componentes |
|---|---|---|
| `display-p3` | 267 | 4 (r, g, b, a) |
| `extended-gray` | 131 | 2 (luminância, a) |
| `srgb` | 87 | 4 |
| `extended-srgb` | 59 | 4 |
| `gray` | 32 | 2 |

**Cinco espaços, não um**, e os dois `gray` têm dois componentes em vez de
quatro — um parser que assuma RGBA quebra em 163 de 576 cores.

`[BIN]` **A Apple diz o mesmo, com as próprias palavras.** O
`IconComposerFoundation` carrega as duas mensagens de erro, lado a lado:

```
Expected four comma separated color components from "
Expected two comma separated color components from "
Invalid color encoding, missing ':' delimiter
```

A regra medida sobre 576 cores e a regra escrita no código do decodificador
concordam. Isso é a confirmação mais forte que este documento tem de qualquer
coisa: as duas fontes são independentes e dizem a mesma frase.

Cuidado de leitura: nem toda string com `:` é cor. O corpus tem quatro nomes de
grupo na forma `"Group: Body"`. A cor é reconhecida **pela posição** — em `solid`,
`automatic-gradient`, `linear-gradient[]` e no `value` de um
`fill-specializations` — nunca por casar um padrão em qualquer string.

## 9. A confrontação

O cruzamento mecânico — toda chave de todo documento contra todo `CodingKey`
selado, sob a regra do §1:

- **145 de 145 documentos legíveis**, zero ilegíveis.
- **Todo valor de enum observado está no vocabulário selado.** Zero valores
  órfãos em 145 documentos.
- **Uma chave não derivável da regra**: `color-space-for-untagged-svg-colors`
  (§1).
- O binário declara chaves que o corpus nunca usa: `material`,
  `asset-mirroring`, `specializable-language-ids`, `implicit-asset-mirroring`, e
  os quatro `legacy-*`. Isso é o esperado — é o superconjunto.

Isso corrige o levantamento anterior deste projeto, que trabalhava com um
`ui-spec` incompleto: faltavam famílias inteiras de especialização, o chaveamento
por `idiom`, o `automatic-gradient` e o espaço `gray`. As aparências não são duas
e sim quatro.

## 10. O que NÃO está resolvido

1. **Os nomes em disco das chaves que o corpus não usa.** `material`,
   `asset-mirroring`, `specializable-language-ids`, `implicit-asset-mirroring` e
   os `legacy-*` só existem como `CodingKey`. Depois do
   `color-space-for-untagged-svg-colors`, aplicar a regra do §1 a eles é palpite.
   O instrumento que fecharia isso é achar os raw values no binário, fora do
   `fieldmd`.
2. **`specular` bool contra string.** Medido, não explicado (§7).
3. **`material` na camada contra `blur-material` no grupo.** Os dois estão
   selados, um deles nunca é escrito, e a relação entre eles é desconhecida.
4. ~~**O que `automatic-gradient` faz com uma cor só**~~ — **FECHADA em
   2026-09-01.** `[BIN]` É caso próprio do `Icon.Fill.Contents`, e a regra vive
   em `ICRRenderingParameters.Fills.AutomaticGradient`: **seis números**, com
   estes defaults — `basePosition` `0.0`, `saturationBoost` `0.2`, e quatro
   clareamentos por faixa de brilho, `0.04`, `0.08`, `0.15` e **`-0.05`**. Uma
   rampa paramétrica sobre a cor base, não uma tabela de stops; a faixa mais
   clara é **escurecida**, não clareada. Doc 03 §18.4, §19 e **§24 — a REGRA, lida**: luminância Rec.709, quatro faixas com fronteiras **fixas no código** em 0,25/0,50/0,75, boost que empurra o canal para longe da luminância, e duas paradas ordenadas por posição.
5. ~~**Dez modos de mescla contra os dezessete implementados antes**~~ —
   **FECHADO em 2026-09-01 pelo caminho que este item previu.** O
   `RB::Shader::blend` do `default.metallib` tem **56 casos**, selecionados por
   `(palavra1 >> 16) & 16383`. E a contagem tem **três** números, não dois: o
   enum do formato tem **18** casos (`IconRendering.Icon.BlendMode`, doc 03
   §17.3), os 145 documentos usam **10**, e o `RenderBox` implementa **56** —
   boa parte deles composição Porter-Duff e não mistura separável. Os
   "dezessete implementados antes" eram dezessete de dezoito. Doc 03 §15 e §17.3.
   `[OBS]` A tradução entre a numeração do formato e a do `RenderBox` **não
   existe ainda**: as duas não coincidem nos três pontos ancorados.
   **Atualização:** os 56 foram lidos inteiros e **16 dos 18** nomes têm
   candidato único (doc 03 §26.3). Dois seguem com mais de um candidato, e
   dois dos dezesseis contradizem a leitura anterior — o que decide é o
   código do `IconRendering` que empacota o modo, ainda não lido.
6. ~~**O bundle em volta do `icon.json`**~~ — **FECHADO.** O `.icon` é uma pasta,
   e ela tem documento próprio: **doc 02**, com `Assets/`, a resolução de
   `image-name` e as referências penduradas. Os SVGs têm o **doc 04**. Os dois
   passam o gate — 55 bundles sem arquivo morto, 149 SVGs lidos em geometria.
   Este item ficou aberto na página depois de o trabalho estar feito.
7. **A gramática exata da CLI.** O §11 levanta os comandos e as mensagens; o
   conjunto completo de argumentos por comando, não.

## 11. As ferramentas de linha de comando

`[BIN]` `ToolKind` tem dois casos, `ictool` e `icrtool`, e eles não são pares:
o `IconComposerFoundation` carrega as strings `"Could not determine ictool's
executable path"`, `"icrtool not found at "` e
`"icrtool-should-not-be-invoked-directly"`. **O `ictool` é a ferramenta; o
`icrtool` é um auxiliar que ele lança.**

`[BIN]` A hierarquia de comandos, de `__swift5_fieldmd`:

```
ToolCommand : NSObject          arguments, bufferedOutput
├── Help
├── Version
├── ExportIntermediateRepresentation
└── IconMutatingCommand
    └── Upgrade
```

mais um `ToolCommandRegistry`. Os nomes dos arquivos-fonte Swift sobreviveram e
nomeiam um comando que a hierarquia acima não mostra:
`ToolCommand.swift`, `ToolCommand.Arguments.swift`,
`ToolCommand.IconMutatingCommand.swift`, **`ToolCommand.ExportBuildIntermediary.swift`**.

`[BIN]` A gramática, lida das mensagens de erro:

```
First argument should be a command name starting with …
Missing required input document path, it should be an unnamed first argument
Expected argument name at position …
Unexpected value for … argument. Expected one of …
Missing required argument …
Invalid command name …
```

Logo: **`ictool --<comando> <documento.icon> [--<arg> <valor>…]`**, com o
documento como primeiro argumento sem nome depois do comando. O `Arguments`
guarda `keyedValues` (`[String: String]`) e `signals` (`Set<String>`), o que
casa com o `ArgumentKind` de quatro casos — `signal`, `value`, e as duas formas
`retired*`, que são argumentos aposentados que o parser ainda aceita.

`[BIN]` Comandos e argumentos vistos em claro:

| token | |
|---|---|
| `--export-intermediate-representation` | comando |
| `--export-build-intermediary` | comando |
| `--export-preview` | comando |
| `--output-directory`, `output-file` | argumento |
| `canvas-size`, `fully-specialize-for`, `matching-style` | argumento |
| `icon-studio-appearance`, `icon-studio-idiom`, `icon-studio-localization` | argumento |

`[INF]` Os três `icon-studio-*` são o contexto de especialização do §5 passado
pela linha de comando, e o prefixo é o nome interno do projeto (`IconStudio`).

`[BIN]` Duas strings soltas que o corpus não explica: **`is-glass` e
`is-hidden`**. O documento escreve `glass` e `hidden` (§1), e estas duas grafias
existem no binário mesmo assim. Onde elas são usadas é **pergunta aberta** — o
candidato é a exportação para asset catalog, que tem chaves próprias (§9).
