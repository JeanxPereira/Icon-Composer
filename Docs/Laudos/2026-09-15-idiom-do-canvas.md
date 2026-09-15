# Laudo — o idiom do canvas

*2026-09-15. Por que o usuário abria o ícone dele e via a proporção errada. Este
documento é laudo **e** implementação: o diagnóstico está nas §§1–3 e o que
mudou está na §4.*

Selos: `[BIN]` medido no binário (com endereço), `[ART]` medido no corpus,
`[OBS]` pergunta aberta, `[INF]` inferência marcada como tal.

Alvos: `References/2.0-125/out/slices/IconComposerFoundation.arm64` (fatia fina,
VA == offset de arquivo) e o corpus de 145 documentos em
`References/corpus/`.

---

## 0. A resposta, em uma frase

**O combo nunca esteve quebrado. O canvas abria no lugar errado.**

O canvas abria em `Idiom::Base`, e `Base` **não é uma plataforma** — é a entrada
de fallback do formato, a que casa apenas com as entradas que *não* nomeiam
idiom nenhum. `[ART]` **145 de 145** documentos do corpus declaram
`supported-platforms`, e **145 de 145** declaram `squares`; sob `Base`,
**nenhuma** das 84 entradas de especialização predicadas por idiom do corpus
resolve. Ou seja: abrir em `Base` mostra uma composição que **documento nenhum
declara suportar**, e que em particular não aplica o
`position-specializations` do usuário — o `scale: 0.8` que a linha de comando
já tinha medido como `360/450 = 0.800` exato.

O usuário não estava errando o combo. O combo estava obedecendo a um padrão que
escondia o trabalho dele.

---

## 1. O combo chega ao render? Chega — e agora há uma asserção que diz isso

O caminho inteiro, seguido arquivo por arquivo:

| passo | arquivo | o que faz |
|---|---|---|
| 1 | `Source/IconComposerKit/PanelCanvas.cpp` `contextBar` | escreve `s.view.context.idiom` e mais nada |
| 2 | `Source/IconComposerKit/MenuBar.cpp` (View ▸ Idiom) | escreve **o mesmo campo** — o controle existe em dois lugares |
| 3 | `Source/IconComposerKit/RenderCoordinator.cpp` `tick` | monta `Key{version, context, size}`; a chave mudou, então re-pede |
| 4 | `Source/IconComposerKit/Ports.h` `RenderRequest::context` | o pedido carrega o contexto |
| 5 | `Source/app/OnyxPorts.cpp` `renderNow` | `io.context = r.context` → `rb::renderIcon` |

Não há ponto de queda nesse caminho. A inspeção visual não é prova disso, então
a prova ficou escrita em `Tests/test_kit_idiom.cpp`, no caso
`kit_idiom_combo_reaches_the_render_request`, que dirige o **`RenderCoordinator`
de verdade** com um `RenderScheduler` que não desenha nada e só *grava o que lhe
pediram*:

```
tick                     -> 1 pedido, context.idiom == Square
responde -> pending false, e um tick a mais não re-pede
s->view.context.idiom = WatchOS
tick                     -> 2 pedidos, o segundo carrega WatchOS
                            e a version NÃO se moveu (a vista não é edição)
resposta ecoando Square  -> pending CONTINUA true   (não é a resposta deste pedido)
resposta ecoando WatchOS -> pending false
```

As duas últimas linhas são o que um coordenador chaveado só pela `version` não
conseguiria: com o mesmo `version()`, só o contexto distingue as duas respostas.

`[OBS]` O suite não linkava `IconComposer::Kit` — o comentário no
`Source/IconComposerKit/CMakeLists.txt` dizia "this is what `ic_tests` and the
selftest link", e era metade verdade: só o selftest linkava. Agora linka, atrás
de `if(TARGET IconComposerKit)`, para que `-DIC_BUILD_UI=OFF` continue verde.

---

## 2. O padrão: é o documento que escolhe, e o número é 145 de 145

### 2.1 `supported-platforms`, medido

`[ART]` Sobre os 145 documentos:

```
declaram supported-platforms          145/145
  conjuntos de chaves    {circles, squares}  97
                         {squares}           48
  squares  "shared"                117
           ["macOS"]                22
           ["iOS"]                   3
           ["iOS","macOS"]           3
  circles  ["watchOS"]              97       (ausente nos outros 48)
```

`squares` nunca falta. `circles` sozinho: **0 de 145**.

### 2.2 O mapeamento não foi inventado — foi lido dos autores

`[ART]` Dos 145 documentos, **23** carregam ao menos uma entrada de
especialização predicada por idiom (84 entradas no total, de 1.740). O idiom que
esses autores escrevem é exatamente o que a declaração deles nomeia:

```
squares "shared"      -> os autores escrevem  idiom: square    (14 dos 16 docs;
                         os outros 2 só especializam watchOS)
squares ["macOS"]     -> os autores escrevem  idiom: macOS     (7 de 7, 100%)
```

Os sete documentos que declaram `squares: ["macOS"]` **e** especializam por
idiom — `RodZill4/material-maker`, `alienator88/Viz`, `kushalpandya/Petrichor`
(×2), `munki/munki`, `sbarex/QLMarkdown`, `sbarex/SourceCodeSyntaxHighlight` —
escrevem `idiom: macOS`. Nenhum escreve `square` sozinho. Isso é o documento
escolhendo; a regra só está lendo.

Daí a regra implementada em `ick::declaredIdiom`:

```
squares "shared"             -> square    120 documentos
squares ["macOS"]            -> macOS      22
squares ["iOS"]              -> iOS         3
squares ["iOS","macOS"]      -> square      (dois membros: só a família cobre os dois)
sem squares, com circles     -> watchOS     0 documentos, mas um ícone só-redondo
                                            não pode abrir num squircle
sem supported-platforms      -> Base        0 documentos — o único caso em que
                                            Base é a resposta honesta
```

### 2.3 O que a regra alcança, contra o que `Base` alcançava

`[ART]` Das 84 entradas predicadas por idiom no corpus:

```
padrão = base            alcança   0 de 84  em  0 documentos
padrão = sempre square   alcança  57 de 84  em 15 documentos
padrão = a declaração    alcança  71 de 84  em 21 documentos
```

`0 de 84` é o número que condena o padrão antigo. Não é uma preferência entre
dois comportamentos defensáveis: é a diferença entre honrar o que o autor
escreveu e ignorá-lo inteiro.

### 2.4 Cuidado com a grafia — e por que não há um segundo entendimento

`[ART]` `squares` ≠ `square`. `squares`/`circles` são as **famílias de forma** do
documento; `base`/`square`/`iOS`/`macOS`/`watchOS` é o vocabulário de
**predicado** das especializações. O mapeamento entre os dois é o da §2.2,
medido, não suposto.

`Source/cli/Report.cpp` (`platformsText`) é o outro leitor de
`supported-platforms` em `Source/`, e ele **só imprime** a chave — não tem noção
de idiom, então não há entendimento concorrente a contradizer.
`Source/IconComposerKit/PanelInspectorDocument.cpp` já tinha medido as mesmas
formas (`squares` string ou lista de {iOS, macOS}; `circles` presente ou
ausente) para o *editor* da chave; `declaredIdiom` é essa mesma leitura
respondendo outra pergunta — não "o que posso escrever?", mas "em que composição
o canvas abre?".

---

## 3. `[BIN]` O alvo separa `Idiom` de `Platform`, e a nossa etiqueta estava errada

Descritores de campo de reflexão Swift em `IconComposerFoundation.arm64`:

| endereço | conteúdo |
|---|---|
| `0x12EFD3` | casos do que escrevemos como `Idiom`: `base`, `square`, `iOS`, `macOS`, `watchOS` |
| `0x121358` | os nomes dos casos **em Swift**: `unspecified`, `square`, `iOS`, `macOS`, `watchOS` |
| `0x12F02F` | casos de um enum **separado**, `Platform`: `iOS`, `macOS`, `watchOS` — e só |
| `0x12F235` | `SupportedPlatforms`: `squares`, `circles`; `Circles` tem `unique`/`shared` |
| `0x12F070` | campos de `RenderingConfiguration`: `unvalidatedPlatform`, `localization`, … , `previewSize`, … , `supportedPlatforms` |
| `0x1264A0` | `"Unknown idiom name: "` seguido de `square`/`iOS`/`macOS`/`watchOS` |
| `0x126590` | `"Unknown platform name: "` — mensagem própria, enum próprio |

E, no `IconComposerKit.arm64`, em `0x18DAF2`, um
`SwiftUI.State<IconComposerFoundation.Platform?>` dentro de uma view que também
referencia `SwiftUI.Picker`, `RadioGroupPickerStyle` e
`ForEach<…Platform…>` — o seletor do alvo é sobre **`Platform`**, não sobre
`Idiom`. Há também
`IconComposition.fullySpecialize(platform:) -> SpecializationResults`
(`0x123F30`).

`[INF]` A leitura que isso sugere — e está marcada como inferência porque não
desmontei `fullySpecialize` — é que no alvo `Idiom` é o vocabulário **do
arquivo** e `Platform` é o vocabulário **da pré-visualização**, com o
`unvalidatedPlatform` sendo validado contra o `supportedPlatforms` do documento
(daí o tipo `ConstrainedPlatform`, `0x122DEA`). Nós expomos o vocabulário do
arquivo direto na barra. **Não mudei o modelo por causa disso** — seria refazer
`icf::Context` e o `resolve`, fora desta frente, e não é preciso para o usuário
ver a proporção certa.

O que essa medição **rende de imediato** é uma etiqueta: `idiomLabel(Base)`
devolvia `"All"`. "All" lê como "todas as plataformas", que é o **oposto** do
que o caso faz — `Base` casa só com as entradas que não nomeiam idiom, e é a
única leitura que não mostra a composição de plataforma nenhuma. O alvo chama
esse caso de `unspecified` (`0x121358`). A etiqueta agora é **"Unspecified"**.

---

## 4. O que mudou

1. **`Source/IconComposerKit/ViewModel.h/.cpp`** — `ick::declaredIdiom(root)`, a
   regra da §2.2, e `ick::declaredPlatformsText(root)`, a declaração em uma
   linha. `idiomLabel(Base)` passa de `"All"` para `"Unspecified"` (§3).
2. **`Source/IconComposerKit/Session.cpp`** — `Session::open` põe
   `view.context.idiom = declaredIdiom(root())`. **Só a vista se move**: `scope`
   continua em `Base`, o documento não é tocado, `version()` não anda e a sessão
   não nasce suja. `Session::create` escreve um documento sem
   `supported-platforms`, logo um arquivo novo abre em `Base` — correto, porque
   não declarou nada ainda.
3. **`Source/IconComposerKit/PanelCanvas.cpp`** —
   - tooltip no combo de idiom, dizendo o que ele faz (`position`, `hidden` e
     `image-name` especializam por idiom — é *este* o combo que muda a
     proporção), o que o documento declara, e, quando diferem, que o que está na
     tela não é o que o documento declara. O tooltip vai logo depois de
     `BeginCombo`, que é onde o "último item" ainda é o botão do combo;
   - **uma linha nova no painel de Diagnósticos**, sempre presente:
     `view | idiom Square, appearance Default; the document declares squares: shared`.
     Era o que faltava: o idiom ativo não aparecia em lugar nenhum da tela a não
     ser em 90 px de texto sem rótulo entre dois vizinhos idênticos. "A proporção
     está errada" e "estou no idiom errado" são a mesma frase, e agora ela está
     escrita.
4. **`Tests/test_kit_idiom.cpp`** e **`Tests/CMakeLists.txt`** — quatro casos, e
   o suite passa a linkar `IconComposer::Kit` quando ele existe.

### O que **não** mudou, de propósito

- `Source/RenderBox/` — a resolução de especialização já estava certa, medida
  pela linha de comando antes desta frente começar. Não havia bug ali.
- `icf::Context` / `icf::Idiom` — o modelo `Platform` do alvo (§3) fica como
  `[OBS]`, não como refatoração especulativa.
- O combo continua aceitando as cinco leituras. A regra escolhe onde **abrir**,
  nunca o que a pessoa pode olhar depois.

---

## 5. Medições de fecho

```
ic_tests (IC_CORPUS_DIR apontado)          622 case(s), 0 failure(s)
ic_tests kit_                                4 case(s), 0 failure(s)

iconcomposer --selftest <corpus/Apollo-Reborn__AppIcon>
  5 frame(s), 4 group(s), 10 layer(s), 7 inspector section(s),
  10 diagnostic row(s); textured yes; bytes round-tripped yes;
  imgui errors 0                                                   exit 0

iconcomposer --selftest <GoWToolkit.icon>   (o documento do usuário)
  5 frame(s), 1 group(s), 1 layer(s), 7 inspector section(s),
  6 diagnostic row(s); textured yes; bytes round-tripped NO;
  imgui errors 0                                                   exit 1
```

O `exit 1` do documento do usuário é o CRLF do arquivo dele (85 quebras de linha
Windows), não regressão — o mesmo `NO` que já havia antes desta frente.
`imgui errors 0` nos dois é o que diz que o tooltip novo e a linha nova de
diagnóstico desenham limpos.

E o caso do usuário, medido sobre um documento da forma exata do dele
(`supported-platforms: {"squares": "shared"}` mais o
`position-specializations` com `scale: 0.8` e `translation-in-points: [0,-25]`):

```
Session::open           -> view.context.idiom == Square
resolve(position, view) -> scale 0.8            <- o que ele desenhou
resolve(position, Base) -> nullptr              <- o que ele via
```
