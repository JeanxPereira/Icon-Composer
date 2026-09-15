# O portão `glass`: o que uma chave ausente quer dizer

2026-09-15. Fatias lidas: `References/2.0-125/out/slices/IconComposerFoundation.arm64`
e `IconRendering.arm64`, mais os despejos de reflexão `fieldmd_foundation.txt` e
`fieldmd_iconrendering.txt`. Corpus: 145 documentos.

**A resposta curta: ausente é `true`.** E ausente em `specular` é `automatic`,
não `off`. O portão anterior lia os dois ao contrário, e por isso mais da metade
dos efeitos autorados do corpus nunca desenhava.

---

## 1. O sintoma

O usuário abriu `GoWToolkit.icon` e disse: *"segue com proporção errada e sem
nenhum specular ou efeito. só tá o ícone chapado dentro de um shape"*. O
documento dele pede, por escrito:

```json
"shadow"      : { "kind": "neutral", "opacity": 0.5 },
"translucency": { "enabled": true, "value": 0.5 }
```

e a camada não tem a chave `glass`. `IconRenderer.cpp` lia

```cpp
const bool isGlass = boolOr(layer.resolve("glass", options.context), false);
```

e esse `isGlass` gateia quatro coisas — refração, translucidez, realce e sombra.
Ausente ⇒ `false` ⇒ os quatro sumiam juntos. O usuário estava certo: o documento
dele pedia sombra e translucidez e recebia um ícone chapado.

---

## 2. A chave se chama `glass`, e isso é `[BIN]`

O modelo de documento vive em `IconComposerFoundation`. O campo é
`IconComposition.Layer.Snapshot.isGlass`, e o `CodingKey` dele soletra o nome
como uma **small string do Swift** — materializada por imediatos, ausente do
*constant pool*. No getter de `stringValue` (função `0xBF654..0xBF860`):

```
0xBF84C  mov  x1, #-0x1b00000000000000   ; 0xE5 << 56 -> small string, contagem 5
0xBF850  mov  x0, #0x6c67                ; 'g','l'
0xBF854  movk x0, #0x7361, lsl #16       ; 'a','s'
0xBF858  movk x0, #0x73,   lsl #32       ; 's'
```

`x0 = 0x7373616C67` = `"glass"`. A mesma tabela de salto dá
`"glass-specializations"` em `0xBF68C` (esse sim um ponteiro, para `0x127980`,
porque 21 caracteres não cabem numa small string) e, no caminho, confirma a
grafia hifenizada de todas as dezenove chaves da camada: `kind, name, position,
fill, material, opacity, blend-mode, hidden, glass, asset-mirroring, image-name`
(onze) e as oito `*-specializations` — `position`, `fill`, `opacity`,
`blend-mode`, `hidden`, `glass`, `asset-mirroring` e `image-name`, todas lidas
nos seus ponteiros (`0x127900`, `0x1266D0`, `0x1276C0`, `0x127890`, `0x1276A0`,
`0x127980`, `0x1278D0`, `0x1279A0`), não deduzidas.

`[OBS]` Existe também um `"is-glass"` em `0x12689E`, com oito sítios no `__text`
(`0x3D53C`, `0x3D9D0`, `0x4D5EC`, `0x503B4`, `0x55FEC` e três em `0xE6B60..`).
**Não é a chave do JSON** — a chave do JSON é a do `CodingKeys`, lida acima. Fica
registrado porque quem varrer os literais vai achá-lo e se perguntar; ele
acompanha o `specular` no mesmo conjunto de funções, o que tem cara de nome de
propriedade da UI e não de grafia de documento. Não foi seguido até o dono.

> **A lição que o brief avisou, e que se confirmou aqui.** Varrer o `__cstring`
> atrás de `"glass"` acha o literal em `0x127977` e **nenhum xref** — porque
> ninguém aponta para ele. Varrer o `__text` atrás do IMEDIATO acha o único sítio
> que importa. Das três chaves procuradas (`glass`, `is-glass`,
> `glass-specializations`), só a longa tinha ponteiro.

---

## 3. Ausente não é `false`: o campo é `Optional`

O descritor de campos do `Layer.Snapshot` está em `0x12D2E4`. Lendo os nomes
manglados crus (e não o que o despejo de reflexão desiste de imprimir como
`<indirect-external>`):

```
name            SSSg
position        SpecializableProperty.Snapshot<Position>   Sg
fill            SpecializableProperty.Snapshot<Fill>       Sg
opacity         SpecializableProperty.Snapshot<Double>     Sg
blendMode       SpecializableProperty.Snapshot<BlendMode>  Sg
isHidden        SpecializableProperty.Snapshot<Bool>       Sg
isGlass         SpecializableProperty.Snapshot<Bool>       Sg   <-- Sg
assetMirroring  SpecializableProperty.Snapshot<AssetMirroring> Sg
imageName       SpecializableProperty.Snapshot<AssetName?>  Sg
```

O `Sg` final é `Optional`. **Toda** propriedade do snapshot é opcional: uma chave
ausente decodifica para `nil`, não para o zero do tipo. E a distinção é legível
neste mesmo despejo, não presumida — `AssetMirroring.mirrorable` aparece como
`SbSg` (`Bool?`) três linhas adiante, enquanto `Icon.Element.participatesInGlass`
do `IconRendering` aparece como `Sb` puro. Quando a fatia quer dizer opcional,
ela diz.

`SpecializableProperty<T>` (descritor `0x12D710`) tem dois campos:
`specializations: [Specialization]` e `defaultValue: T`.

---

## 4. Então quem responde é o `defaultValue` do modelo vivo — e ele é `true`

O modelo vivo **não** é opcional. O descritor da classe `Layer` (`0x12C604`) dá
`_isGlass` como `SpecializableProperty<Swift.Bool>`, sem `Sg`. Logo um snapshot
`nil` deixa de pé o que o inicializador instalou.

O inicializador é `IconComposition.Layer.init()` em **`0x95C10`**. Ele foi achado
pelo sítio de alocação: em `0xC0570` o código chama o acessor de metadados da
`Layer` (`0x94944`), passa `instanceSize`/`alignMask` para `swift_allocObject`, e
chama `0x95C10` no objeto recém-alocado.

`Layer.init()` escreve **toda** propriedade armazenada, em ordem de declaração,
com `x19 = __swiftEmptyArrayStorage` e `x21 = 1.0`:

| offset | escrito | propriedade |
|---|---|---|
| `+0x38` / `+0x40` | array vazio / 0 | `_imageName` = nil |
| `+0x50` / `+0x58`,`+0x68` | array / (0,0), 1.0 | `_position` = translada (0,0), escala 1.0 |
| `+0x70` / `+0x78..0xEF` | array / blob de `0x170AC8` | `_fill` |
| `+0xF0` / `+0xF8` | array / **0** | `_blendMode` = `.normal` |
| `+0x100` / `+0x108` | array / **1.0** | `_opacity` = 1.0 |
| `+0x110` / `+0x118` | array / **1** | **`_isGlass` = `true`** |
| `+0x120` / `+0x128` | array / byte de `0x155BE8` | `_assetMirroring` |
| `+0x130` / `+0x138` | array / **0** | `_isHidden` = `false` |

As duas instruções que decidem esta frente:

```
0x95CDC  mov  w8, #1
0x95CE0  strb w8, [x20, #0x118]      ; _isGlass.defaultValue
```

**O offset não é chute.** Os vizinhos se identificam sozinhos e todos batem com
o que já se sabia do formato: `_blendMode == .normal`, `_opacity == 1.0`,
`_isHidden == false`, `_position == (0,0) × 1.0`. Um mapa de layout que acerta
quatro âncoras não erra o campo entre a quinta e a sexta. E o mapa fecha: da
borda do cabeçalho (`+0x10`) até o registrar da observação não sobra byte
inexplicado.

### 4.1 E o caminho de decodificação é esse mesmo

O sítio que alocou a `Layer` é a função `0xC0544`, e ela é o decodificador:
recebe um `Layer.Snapshot`, lê o `name` em `[x23]`, aloca, chama `0x95C10`, e
então aplica campo a campo. Cada campo do snapshot é testado antes:

```
0xC0660  ldrb w26, [x23, #0x28]     ; a tag do Optional do campo
0xC0664  cmp  w26, #0xff            ; 0xff == nil
0xC0668  b.eq #0xC0760              ; nil -> PULA a aplicacao inteira
```

Um campo `nil` não escreve nada. É o que amarra as duas metades: o snapshot
opcional (§3) não tem valor para dar, e o que fica de pé é o que `Layer.init()`
instalou (§4). Ausente não cai num `?? false` escrito em algum lugar — **não há
`??` nenhum**; há um desvio por cima da escrita.

---

## 5. A mesma pergunta, para `specular` — e a resposta é `automatic`

`Group.init` está em **`0x9090C`**, achado do mesmo jeito (alocação em `0xB8318`,
acessor de metadados do `Group` em `0x87FE4`). Ele escreve os onze
`SpecializableProperty` do grupo em ordem de declaração:

| offset | escrito | propriedade |
|---|---|---|
| `+0x38` / `+0x40` | array / 0 | `_isHidden` = `false` |
| `+0x48` / `+0x50` | array / 0 | `_lighting` = `.individual` |
| `+0x58` / `+0x60` | array / byte de `0x155BE8` | `_assetMirroring` |
| `+0x68` / `+0x70` | array / 0 | `_blendMode` = `.normal` |
| `+0x78` / `+0x80`,`+0x88` | array / 0, 0.5 | `_blurMaterial` = (enabled false, strength 0.5) |
| `+0x90` / `+0x98`,`+0xA0` | array / 0, const | `_refractivity` = (enabled false, …) |
| `+0xB0` / `+0xB8`,`+0xC0` | array / **1**, **0.5** | **`_shadow` = `Shadow(.neutral, 0.5)`** |
| `+0xC8` / `+0xD0`,`+0xD8` | array / **1**, **0.3** | **`_translucency` = `(enabled true, 0.3)`** |
| `+0xE0` / `+0xE8` | array / **1** | **`_specular` = `.automatic`** |
| `+0xF0` / `+0xF8` | array / 1.0 | `_opacity` = 1.0 |
| `+0x100`..`+0x11F` | array / (0,0), 1.0 | `_position` |
| `+0x120` | array vazio | `_layers` = `[]` |

A instrução:

```
0x909B8  mov  w9, #1
0x909D4  strb w9, [x19, #0xe8]       ; _specular.defaultValue
```

`IconComposerFoundation.SpecularHighlight` (descritor `0x12CFF0`) lista os casos
`off, automatic, inside, outside`. O `1` armazenado é **`automatic`**.

### 5.1 A conferência cruzada, e ela fecha sem sobra

Os dois inicializadores acima têm os valores **inlinados**. Mas o compilador
emitiu também as funções autônomas de inicialização de propriedade (as `vpfi`),
e elas estão todas juntas no começo do `__text`, entre `0x1B78` e `0x1C30`. Cada
uma devolve `x0 = __swiftEmptyArrayStorage` (as `specializations` vazias) mais o
`defaultValue` nos registradores seguintes. Lidas uma a uma:

| thunk | devolve | só pode ser |
|---|---|---|
| `0x1B78` | array, `w1=0` | `SP<Bool>(false)` / `SP<enum caso 0>` — `_isHidden` ×2, `_blendMode` ×2, `_lighting` |
| `0x1B88` | array, `w1=[0x155BE8]` | `SP<AssetMirroring>` — default de runtime, ×2 |
| `0x1B9C` | array, `d0=0.5`, `w1=0` | `SP<BlurMaterial>(enabled false, 0.5)` |
| `0x1BB0` | array, `d0=0.5`, `d1=0`, `w1=0` | `SP<Refractivity>(enabled false, 0.5, 0.0)` |
| `0x1BC8` | array, `d0=0.5`, `w1=1` | `SP<Shadow>(.neutral, 0.5)` |
| `0x1BDC` | array, `d0=0.3`, `w1=1` | `SP<Translucency>(enabled true, 0.3)` |
| **`0x1BF8`** | array, `w1=1` | **`SP<Bool>(true)` e `SP<SpecularHighlight>(.automatic)`** |
| `0x1C08` | array, `d0=1.0` | `SP<Double>(1.0)` — `_opacity` ×2 |
| `0x1C18` | array, `0,0`, `d2=1.0` | `SP<Position>` ×2 |
| `0x1C30` | array, `x1=0`, `x2=0` | `SP<AssetName?>(nil)` — `_imageName` |

**Nenhum thunk sobra e nenhum falta.** Os dez cobrem exatamente os dezenove
`SpecializableProperty` das duas classes, e cada valor bate com a escrita
inlinada correspondente — o `(0.5, .neutral)` da sombra, o `(true, 0.3)` da
translucidez, o `1.0` da opacidade, o `false` do `hidden`. O linker funde funções
idênticas, e é por isso que `_isGlass` e `_specular` caem no MESMO thunk
`0x1BF8`: os dois defaults são o byte 1. Se `glass` fosse `false`, ele teria
fundido com `0x1B78` e `0x1BF8` teria um dono a menos — e `0x1BF8` existe.

Duas leituras independentes (o `init` inlinado e o `vpfi` autônomo) dando a mesma
resposta é o que separa isto de um offset bem-adivinhado.

> **A âncora que fecha o layout do grupo.** `+0xB8 = 1` com `+0xC0 = 0.5` é
> `_shadow == Shadow(kind: .neutral, opacity: 0.5)` — e `[ART]` `neutral/0.5` é
> exatamente a sombra dominante do corpus, **146 dos 271 grupos**. Um mapa de
> layout que cai em cima do valor mais escrito do corpus, e ao mesmo tempo em
> `_opacity == 1.0`, `_isHidden == false` e `_layers == []`, não está deslocado.

---

## 6. O que o corpus diz — e por que ele não decidia sozinho

`[ART]` Varredura dos 145 documentos (271 grupos, 437 camadas):

| | |
|---|---|
| `"glass": true` | 135 camadas |
| `"glass": false` | **90 camadas, explícito** |
| chave ausente | 212 camadas |
| `glass-specializations` | 55 camadas |
| `"specular": true` | 64 grupos |
| `"specular": false` | **36 grupos, explícito** |
| `"specular": "inside"` | 3 grupos |
| `specular` ausente | 168 grupos |

Dois achados estruturais que o binário depois explicou:

1. **`glass` e `glass-specializations` são perfeitamente disjuntos** — as 55
   ocorrências de `glass-specializations` estão todas em camadas sem `glass`, e
   nenhuma camada tem as duas. É a mesma forma que o documento do usuário mostra
   em `fill-specializations`: quando há especialização, a entrada SEM qualificador
   carrega o valor base e a chave simples some.
2. **A omissão é local, não é marca de documento legado** — 46 documentos
   misturam camadas com e sem a chave. 115 das 212 ausências convivem com uma
   camada que escreve `glass` no mesmo arquivo.

E os 90 `false` e 36 `false` explícitos são o que o corpus tinha a dizer: **sob a
leitura antiga essas grafias não compravam nada**; sob esta, são o único jeito de
desligar. Mas o corpus não decidia sozinho — 212 ausências também são muita
gente, e "ausente = ligado" faria vidro aparecer onde ninguém pediu. Quem decidiu
foi `0x95CE0` e `0x909D4`.

---

## 7. O que mudou no código

Dois literais e um `optional`. O `isGlass` continua gateando os mesmos quatro
sítios (refração, translucidez, realce, sombra); só o default virou.

- `IconRenderer.cpp` — o portão da camada e o mesmo teste na varredura do
  `groupWouldRefract`: `boolOr(..., false)` → `boolOr(..., true)`.
- `GlassMaterial.cpp` — `glassMaterialFrom`: um `specular` ausente passa a valer
  `icf::SpecularHighlight::Automatic` em vez de deixar `hasSpecular` no `false`
  do struct.
- `Source/cli/Report.cpp` — a árvore do `ictool` marcava `[glass]` só com a chave
  presente e `true`. Deixar assim faria o relatório discordar do renderizador
  sobre 212 camadas do corpus, e é a pior forma que uma ferramenta de relatório
  pode tomar: é a árvore que alguém lê para descobrir por que a figura ficou
  daquele jeito.

Três fixtures de teste passaram a **soletrar** o que antes herdavam do silêncio,
e é por isso que a mudança delas não é maquiagem:

- `test_automatic_fill.cpp::oneLayer` diz `"glass": false` — aqueles casos medem
  a RAMPA, e um realce por cima da rampa é exatamente a planura que eles aferem.
- `test_glass_layer.cpp::rampAndGlass` diz `"glass": false` no fundo (é um PNG, e
  vidro sobre raster é uma lacuna nomeada deste renderizador: sem a chave o fundo
  sumia para dentro de `skipped` e a fixture deixava de ser "uma rampa atrás") e
  `"specular": false` no grupo da lente (aqueles casos medem a REFRAÇÃO).
- `test_glass_material.cpp` troca `CHECK(!m.hasSpecular)` por `CHECK(m.hasSpecular)`
  num grupo que não diz nada, e ganha um caso novo,
  `an_explicit_specular_false_still_switches_the_highlight_off`, para que
  "ausente é automatic" não possa ser confundido com "o interruptor de desligar
  parou de funcionar".

---

## 8. A medição: antes e depois, contada

Dois binários `icrender`, um de cada lado da mudança, com hashes distintos
(`FD83D969…` antes, `29314E1B…` depois). Nenhum outro arquivo diferente entre as
duas compilações.

### O ícone do usuário — `GoWToolkit.icon`, 1024, `--idiom square`

```
antes  EEE827A84893AD660533F23606212A430D7ACB5AB50F1AEF54790B42F4C30E7E
depois F5D9CEA2A76388C6F6C20F6EA619D9A70FDE6BE6C39D194153C0E8B2C7275A5B
```

Mudou. E o `stderr` do lado "depois" diz **quais** dos quatro portões abriram,
com as palavras do próprio renderizador:

- *"a mascara de translucidez corre uma rampa VERTICAL dentro de um retangulo"*
- *"sombra: o anel de Shadow.ringWidth ([16,16,16,16], 0x5EC58) E aplicado"*
- *"especular desenhado: CINCO realces do conjunto `glyphs*`"*

Antes dessa mudança o mesmo documento não imprimia nenhuma das três. O usuário
disse *"sem nenhum specular ou efeito"*; ele estava descrevendo exatamente isto.
A refração é a quarta e continua parada, porque este documento não traz
`refractivity` — e identidade é identidade.

### O corpus — 145 documentos, 128 px, contexto base

| | |
|---|---|
| documentos renderizados dos dois lados | 145 |
| **com pixel diferente** | **33** |
| idênticos byte a byte | 112 |
| falhas de render | 0 |

Os 112 idênticos não são 112 documentos que a mudança não alcança. **Só 55 dos
145 têm uma pasta `Assets` não-vazia**; os outros 90 são `icon.json` sozinho, sem
arte, e desenham apenas o fundo — que nenhum destes dois portões toca. E os 33
que mudaram caem **todos** dentro dos 55: nenhum documento sem arte mudou, o que
é a checagem de sanidade que uma contagem de pixels precisa passar.

**Então a conta que importa é 33 de 55 — 60% dos documentos do corpus que têm
arte para desenhar.** Bate com a leitura estrutural: `[ART]` 57% dos grupos com
efeito autorado não têm nenhuma camada `glass: true`.

E os três documentos que a varredura do corpus tinha apontado de antemão como
"grupo com efeito autorado e nenhuma camada com `glass`" —
`ARMSX2__ARMSX1__icon`, `Bunn__PiStats__pistats` e
`Emanuele-web04__remodex__RemodexContrast` — estão os três na lista dos 33. A
previsão foi feita a partir do JSON, antes de qualquer render.

---

## 9. O que ficou de pé, e marcado

`[INF]` **A ligação `glass` → `Icon.Element.participatesInGlass` continua
inferência.** O que subiu de nível foi outra coisa, mais estreita e suficiente: o
default do PRÓPRIO documento para uma chave que falta. O que se leu a mais sobre
a ponte é que `Icon.Element.participatesInGlass` é `Sb` — `Bool` puro, não
`Bool?` (descritor `0xA3790` em `IconRendering.arm64`) — o que derruba a
hipótese de o consumidor testar `!= nil`. A função que constrói um `Icon.Element`
a partir de um `Composition.Layer` continua não localizada.

**Mas a pista estreitou, e fica anotada para quem pegar essa frente.** O nome
`participatesInGlass` existe em UMA fatia só, como string de reflexão em
`0xA10F0` do `IconRendering.arm64` — não aparece no `IconComposerKit`, no
`IconComposer`, no `ictool` nem no `icrtool`. Logo a ponte não monta o elemento
por nome; monta posicionalmente. E o `IconComposerKit.arm64` **é** o lado que
importa esses tipos: seus typerefs simbólicos nomeiam
`IconRendering.Icon.Element` (`0x1B9754`), `Icon.Layer` (`0x1B9744`),
`Icon.GlassMaterial` (`0x1B974C`), `GlassMaterial.SpecularPlacement`
(`0x1B975C`), `GlassMaterial.ShadowStyle` (`0x1B9764`) e
`ICRRenderingParameters` (`0x1B977C`). A ponte está ali dentro. Achá-la é a
mesma técnica que achou `Layer.init` aqui — o acessor de metadados e quem o
chama — só que sobre um `struct`, sem a âncora do `swift_allocObject`.

`[BIN]` **Dois outros defaults do grupo estão lidos e NÃO foram aplicados**, e
ficam registrados aqui porque a frente que os mudar precisa medir o próprio
antes/depois:

- `shadow` ausente deveria ser `Shadow(kind: .neutral, opacity: 0.5)` — hoje o
  `GlassMaterial` fica em `(Automatic, 0.0)`. `[ART]` 31 dos 271 grupos não têm a
  chave.
- `translucency` ausente deveria ser `(enabled: true, value: 0.3)` — hoje fica em
  `(false, 0.0)`. `[ART]` 68 dos 271 grupos não têm a chave.

Aplicar os quatro defaults de uma vez tornaria as contagens de pixel deste laudo
ilegíveis. Estão medidos e datados; mudá-los é uma frente com a sua própria prova.

`[OBS]` **A UI não foi tocada.** `IconComposerKit/PanelLayers.cpp`,
`PanelInspector.cpp` e `MenuBar.cpp` leem `glass` por `booleanUnderBase`, que
devolve `false` para uma chave ausente — então a caixinha "Glass" aparece
desmarcada numa camada que o renderizador agora desenha como vidro. É a mesma
correção de uma linha em três sítios, mas é código de outra frente e mexer nele
aqui seria conflito por nada.
