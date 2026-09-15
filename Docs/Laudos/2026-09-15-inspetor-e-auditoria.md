# Os controles do Inspetor, e a auditoria do "nem citei tudo"

15/09/2026. Frente: `PanelInspector*.cpp`, `InspectorSection.h`, `MenuBar.cpp`.
As frentes irmãs estão em `PanelCanvas.cpp` (clip e zoom/pan) e `PanelLayers.cpp`
nesta mesma rodada; nada aqui entrou nesses dois arquivos.

O que o usuário disse: *"app completamente bugado. mexer nos controles, nas
layers, péssimo. [...] tem muita coisa errada, muita mesmo, nem citei tudo aqui"*.

---

## 0. O que o verde não vê

O selftest headless imprime `imgui errors 0`, `textured yes` e `bytes
round-tripped yes`. Ele imprimiria exatamente isso com **todos os sliders do
painel quebrados**, porque ele não move nenhum, não aperta nenhuma tecla e não
olha para nada. Foi acreditar nesse verde que produziu a situação atual, e nada
neste laudo é justificado por ele. O que ele prova, e só isso: o frame ainda
desenha sem erro de ImGui e a ida-e-volta em bytes continua exata depois das
mudanças.

O que foi medido de verdade está na §4; o que só um humano julga está na §5.

---

## 1. Âncoras

Duas, e cada conserto abaixo diz qual usou.

**`[BIN]` O alvo.** `References/2.0-125/out/slices/IconComposerKit.arm64`,
4.811 símbolos, **167 tipos nomeados de `IconComposerKit`** extraídos do símbolo
mangled. Os que mandam nesta rodada:

| símbolo | ocorrências | o que diz |
|---|---:|---|
| `SpecializablePropertyInspector` | 31 | o envelope de todo inspetor especializável |
| `InspectorSection` | 33 | a seção, com `SelectionIndicatorRectangle` |
| `MultiNumericTextField` | 13 | **número de inspetor é campo de TEXTO**, multi-componente |
| `InspectorTextFieldStyle` (+ `InlineLabel`) | 11 | o campo tem **rótulo embutido** |
| `Scrubbable` | 4 | e o rótulo dele se **arrasta** |
| `BufferedTextField` | 2 | commit no enter, não a cada tecla |
| `MultiPicker` / `MultiToggle` | 7 / 6 | os combos e os toggles |
| `SpecializationSlice`, `EnumeratedSpecializationSlice` | 10, 3 | as fatias por escopo |
| `AllVariantsContextMenu` | 6 | **ver as outras variantes sem sair da atual** |
| `ColorChip`, `ColorPickerGrid`, `ColorSlider` | 32, 10, 8 | a cor |
| `AxisChoice`, `Axis2D`, `GradientPlacementView` | 29, 4, 7 | o eixo do gradiente |
| `InspectorScrollView`, `InspectorPane` | 8, 6 | o painel rola, e tem **abas** |
| `LayerSpecularInspector` + `GroupSpecularInspector` | 3 + 3 | **dois** especulares, não um |
| `PasteboardPropertySet`, `InspectorCopyablePropertyTypeKey` | 27, 3 | copiar/colar propriedades |
| `LayerOutlineView`, `PasteboardMemberItemDragSource`, `ConstrainedDragGesture` | 35, 8, 11 | arrastar camada |

Reproduzível com
`python scripts/macho.py syms <slice> <substring>` (caminho absoluto; `References/`
é gitignored e não existe no worktree).

**O Onyx.** `D:/CodingProjects/OnyxSDK/` — somente leitura, nada foi escrito lá.
`Include/Onyx/App/UIHelpers.h` **não tem widget numérico nenhum** (é formatação,
mapa de tipo→ícone e dois diálogos de arquivo), e `Source/App/Widgets.cpp` são
oito invólucros de estilo (`IconButton`, `ColoredTreeNode`, `Button`,
`SmallButton`, `Selectable`, `CollapsingHeader`, `MenuItem`, `BeginTabItem`) —
nenhum slider, nenhum campo. **Não havia controle do Onyx para reusar**; o que o
Onyx dá é o *padrão*: `SettingsWindow.cpp:210`, `CameraPanel.cpp:69-111` e
`UiGallery.cpp` usam sempre **rótulo visível no próprio controle** e **unidade no
format** (`"%.2fx"`, `"%.1f px"`, `"%.0f pt"`), e `SetNextItemWidth` explícito
(220 / 160 / 120). Os rótulos e formatos adicionados abaixo seguem isso.

---

## 2. As suspeitas do briefing, confirmadas ou derrubadas

| suspeita | veredito | medida |
|---|---|---|
| Coalescência de arrasto | **derrubada — todo controle contínuo já passava `coalesce=true`** | os 8 controles contínuos do painel; todos com `IsItemDeactivatedAfterEdit → endCoalescing` |
| Digitar o valor | **confirmada** | zero menção a Ctrl+clique em qualquer lugar do Kit; o alvo usa campo de texto em 13 símbolos |
| Largura e rótulo | **confirmada** | 7 combos com id `##...` e rótulo nenhum; 2 do escopo com 100 px lado a lado |
| O escopo não está na tela | **confirmada, e é a pior** | `scopeSelector` desenhava só `Session::scope`; `Session::view.context` nunca aparecia no painel |
| Feedback de escrita (`hasOwnEntry`) | **parcial** | dizia `inherited`/`not set`, **não** dizia que o próximo arrasto cria uma entrada nova |
| Passos, limites, precisão | **confirmada, e havia um bug de bytes junto** | `Scale (%)` com passo 1.0 e piso 1 %; toda a Geometry passando por `float` |

### 2.1 A coalescência estava certa — e por isso o teste mede o que ela vale

Auditei os oito controles contínuos (`opacity`, `geometry` ×2, cor RGBA, cor
cinza, eixo ×2, `shadow.opacity`, `translucency.value`, `blur-material.strength`,
`refractivity` ×2): **todos** já chamavam `x.write(..., true)` e todos já
fechavam o arrasto no `IsItemDeactivatedAfterEdit`. A suspeita não se confirmou.

Mas ela não era barata de derrubar no olho, e é regressão fácil de causar, então
virou teste — `Tests/test_kit_inspector.cpp`, §4. O caso que dá sentido aos
outros é o segundo: um arrasto de 214 quadros **sem** a flag são **214 comandos**,
com a flag é **1**.

Um detalhe que o teste encontrou e que vale registrar: `Session::apply` não
registra comando quando a edição não muda byte nenhum, então o primeiro quadro de
um arrasto que "escreve" o valor que já estava lá é invisível para a pilha. A
contagem é 213, não 214, se o arrasto começar no valor de abertura. O teste começa
um passo abaixo, de propósito, e o comentário diz por quê.

---

## 3. O que foi consertado

Todos com âncora, exceto onde está escrito **SEM ÂNCORA**.

### 3.1 O escopo editado contra a composição na tela — `PanelInspector.cpp:653`

O defeito de maior alcance do painel, e é exatamente o que o briefing suspeitou.

`Session::view.context` é o que a **canvas renderiza**; `Session::scope` é o que
este painel **escreve**. São independentes de propósito, e `Session::open` move a
*view* para o idiom declarado deixando o *scope* em Base de propósito
(`ViewModel.h`, e o caso `kit_canvas_opens_on_the_idiom_the_document_declares`
prende isso). O problema é que o painel desenhava **só o `scope`**, em dois combos
de 100 px sem rótulo, e **nunca mencionava a view**.

`[ART]` **145 de 145** documentos do corpus declaram `supported-platforms`. Logo,
em praticamente todo documento real os dois discordam **desde o primeiro quadro**:
a canvas está em `square`, o inspetor escreve a chave simples, e a pessoa olha uma
composição e edita outra sem nada na tela dizendo isso. É literalmente a mentira
que o cabeçalho deste arquivo diz que o painel existe para impedir — *"a lie the
document only confesses on save"* — e o painel a estava contando.

Agora: os dois combos têm rótulo (`Appearance`, `Idiom`) e 140 px; quando os dois
pares discordam, uma linha âmbar diz **"The canvas is showing Dark / square — you
are editing Base / Base."**; e há **um botão, `Edit what the canvas shows`**, que
move só o `scope`. Não o contrário: puxar a view para o scope arrastaria a canvas
para fora do idiom que o documento declara, que é a coisa que `Session::open`
trabalhou para acertar.

*Âncora:* o `SpecializablePropertyInspector`/`SpecializationSlice` do alvo para o
conceito; **o texto e o formato do aviso são meus — SEM ÂNCORA no alvo**. O que
não é meu é o diagnóstico: está escrito no cabeçalho do próprio arquivo.

### 3.2 "Escrito em outros escopos" — `InspectorSection.h`

`[BIN]` O alvo não mostra um escopo por vez: `SpecializablePropertyInspector`
(31 símbolos) é construído sobre `SpecializationSlice` e
`EnumeratedSpecializationSlice` e carrega um **`AllVariantsContextMenu`** (6), cujo
propósito inteiro é deixar quem edita uma variante ver que as outras existem.

Cada seção agora conta os escopos **que não são o atual** e que têm entrada
própria, e diz *"also written under 3 other scopes"* com a lista no tooltip. Não
há API de enumeração — `Edit.h` só responde `hasOwnEntry(node, prop, scope)` — então
os 20 escopos são perguntados um a um. 20 buscas por seção por quadro não é nada
ao lado do render ao lado do qual elas rodam, e é a resposta honesta em vez de um
cache que pode envelhecer contra uma edição.

Isto é a outra metade de 3.1: 3.1 diz *"você está olhando outra coisa"*, isto diz
*"e a coisa que você está olhando tem um valor próprio bem aqui"*.

### 3.3 Digitar o valor, em todo controle contínuo — `InspectorSection.h`

`[BIN]` 13 símbolos `MultiNumericTextField`, 11 `InspectorTextFieldStyle` +
`InlineLabel`, 2 `BufferedTextField`, 4 `Scrubbable`: no alvo um número do
inspetor **é um campo de texto** com rótulo embutido cujo rótulo também se
arrasta. O ImGui aceita valor digitado, mas só atrás de **Ctrl+clique**, e este
editor nunca disse isso em lugar nenhum.

Dois helpers novos, `dragNumbers` e `sliderNumber`, por onde passam **todos** os
controles contínuos. Cada um faz três coisas que nenhum call site precisa mais
lembrar:

1. diz *"Ctrl+click (or double-click) to type an exact value"* no próprio controle;
2. passa **`ImGuiSliderFlags_AlwaysClamp`** — sem ele o ImGui deixa um valor
   **digitado** passar de min/max, e as duas maneiras de mexer na mesma propriedade
   discordam sobre o que ela pode valer;
3. lê `IsItemDeactivatedAfterEdit` **antes** do tooltip. `SetItemTooltip` abre uma
   janela e escreve texto nela, e o "último item" a partir daí é esse texto e não
   o controle — quem faz tooltip primeiro e pergunta depois **nunca vê o arrasto
   terminar**, nunca fecha a coalescência, e deixa o comando aberto para a próxima
   edição cair dentro. Não havia esse bug hoje; o helper garante que não apareça.

E **doubles, não floats**, em todos eles — que é o item seguinte.

### 3.4 A Geometry destruía coordenadas — `PanelInspector.cpp`

O bug de bytes desta frente.

A seção lia o `position` inteiro em `float`, fazia a escala ir a porcentagem e
voltar em `float`, e reescrevia o objeto **todo** a cada quadro de arrasto. Isso
significa que **arrastar a escala reimprimia as duas coordenadas da translação em
precisão de float**. `[ART]` o corpus escreve coordenadas como
`0.5000000000000001`; o `json::Value` carrega o texto-fonte do número justamente
para a ida-e-volta ser exata (`Json.h`); um float não segura esse número e não o
imprime de volta.

Nada disso aparece na tela. Aparece como um documento que não bate mais com os
bytes de onde foi aberto — a mesma coisa que o `writeAxis` do fill, dez linhas
abaixo, tem um parágrafo inteiro explicando que não se faz.

Consertado com a mesma regra do `writeAxis`: **doubles em toda a seção**, e escrita
**membro a membro no nó que o documento carrega** — `writeScale` toca `scale`,
`writeTranslation` toca só a coordenada que **mudou**. Provado em
`kit_a_float_round_trip_rewrites_a_coordinate_nobody_touched` e
`kit_the_position_round_trip_is_exact_in_doubles_and_lossy_in_floats` (§4).

Passo e limites junto: `0.25` por pixel em vez de `1.0` (um arrasto cujo passo é a
própria unidade não consegue **parar** num número redondo — cai onde o pixel
caiu), formato `%.4f` e `%.4f %%`, e o piso da escala desceu de 1 % para **0**: um
fator zero é um valor que o formato guarda, e o piso em um por cento não era nada
que o corpus ou o binário tivessem pedido. O teto de 1000 % ficou onde estava.

### 3.5 Os atalhos de teclado eram desenho — `MenuBar.cpp`

**O maior (a) desta auditoria, e não é do inspetor.**

`ImGui::MenuItem(label, shortcut, ...)` **desenha** o terceiro argumento alinhado à
direita e **não liga nada**: a string é uma *foto* de uma tecla. A barra anunciava
nove acordes — `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+W`, `Ctrl+Q`,
`Ctrl+Z`, `Ctrl+Y`, `Del` — e uma varredura por `ImGuiKey`, `IsKeyPressed` ou
`Shortcut` em **todo** `Source/IconComposerKit/` e `Source/app/` voltava **vazia**.
Nenhum deles fazia nada. Inclusive o `Ctrl+Z`, que é o desfazer para o qual toda a
coalescência do inspetor existe.

Ligados agora com `ImGui::Shortcut(..., ImGuiInputFlags_RouteGlobal)`, no mesmo
arquivo dos rótulos que os prometem para os dois não se separarem. `RouteGlobal`
não é um atalho por cima do foco: uma rota **perde para um item ativo**, então
`Ctrl+Z` dentro do campo de import do painel de asset continua editando o texto, e
`Del` durante o rename de uma camada continua apagando um caractere — o campo de
texto do ImGui reivindica o teclado inteiro enquanto está ativo, e é isso que
torna essa frase verdadeira.

`Redo` responde a `Ctrl+Y` **e** `Ctrl+Shift+Z`; as duas chamadas rodam sempre, sem
curto-circuito, porque `Shortcut` **registra** a rota além de lê-la, e uma rota
registrada só nos quadros em que o outro acorde não bateu é uma rota que
intermitentemente não existe.

> **Aviso que sai daqui e vira o item #1 da lista:** `Ctrl+W` e `Ctrl+Q` agora
> funcionam, e `Source/app/Window.cpp:102-103` fecha e sai **sem checar
> `isDirty()` e sem perguntar nada**. Esse buraco de perda de dados já existia
> (o item de menu é igualmente desprotegido); ligar a tecla o torna muito mais
> fácil de acertar por acidente. Não está no meu conjunto de arquivos. **É o
> primeiro conserto da próxima rodada.**

### 3.6 O documento não tinha porta — `PanelInspector.cpp:728`

`drawInspector` sempre teve um ramo `NodeKind::Root` — o `fill` de fundo, e
Platforms / SVG Color Space / Features de `PanelInspectorDocument.cpp` — e
`nodeTitle` sempre respondeu `"Document"` para um path sem grupo
(`ViewModel.cpp:29`). **Nada no editor rodando conseguia produzir esse path:** a
árvore de Layers lista grupos e camadas e nenhuma linha de raiz,
`Session::selection` começa vazio, e nenhum item de menu a escreve.

Ou seja: **quatro seções do painel — incluindo o único controle sobre
`supported-platforms`, a chave que `[ART]` 145 de 145 documentos carregam e que
decide se o ícone é squircle ou círculo — estavam inalcançáveis no programa
construído**, enquanto plenamente escritas, testadas e desenhadas no selftest (que
seleciona por `firstSelectable`, um grupo).

A linha certa é na árvore de Layers; há uma frente nesse arquivo nesta rodada,
então a porta foi aberta aqui, no painel onde as seções vivem: um `RadioButton
"Document"` no topo do inspetor.

### 3.7 Rótulos, larguras e a cor

- Sete combos ganharam rótulo: `Mode` (blend), `Kind` (fill), `Kind` (shadow),
  `Highlight` (specular), `Mode` (lighting), `Value` (blur-material), `File`
  (image-name). *Âncora:* o padrão do Onyx (§1) — rótulo no controle, sempre.
- `Remove override` virou **`Remove Dark / square override`**: o botão **apaga uma
  entrada**, e quem vai apertá-lo não devia ter que olhar noutro lugar para
  descobrir qual.
- `inherited` / `not set` viraram **`inherited — editing writes a new override
  here`** (ou `the plain key`, sob Base), com o escopo nomeado no tooltip. Mexer
  num valor herdado **acrescenta um membro que o documento não tinha**, e isso é um
  byte que o gate compara; quem move o slider é quem precisa saber que vai
  acontecer.
- Cada parada de cor agora é rotulada com **o espaço de cor dela** (`sRGB`,
  `Display P3`, ...) e o tooltip diz que o picker e a amostra são sRGB e que uma
  parada de gama larga desenha aqui mais fosca do que a canvas desenha. O controle
  não converte — `colorToString` reescreve os números no espaço em que chegaram —
  então o mínimo que ele pode fazer é **nomear o espaço que não está honrando**.
- Erro de import não fica mais preso na tela: digitar no campo o limpa. Os buffers
  são estáticos de arquivo, então a mensagem de um caminho digitado contra **outra**
  camada ficava vermelha debaixo da seguinte.

---

## 4. O que ficou provado por teste

`Tests/test_kit_inspector.cpp`, seis casos novos, atrás de
`if(TARGET IconComposerKit)` em `Tests/CMakeLists.txt` como o `test_kit_idiom.cpp`
(uma build com `-DIC_BUILD_UI=OFF` não tem Kit e tem que continuar verde).

| caso | o que prende |
|---|---|
| `kit_a_coalesced_drag_is_one_undo` | 214 quadros de arrasto + `endCoalescing` = **1** comando; um `undo` devolve o valor de abertura e a pilha esvazia |
| `kit_a_drag_without_the_flag_is_one_command_per_frame` | os mesmos 214 quadros sem a flag = **214** comandos. É o número que faz o caso acima significar alguma coisa |
| `kit_releasing_the_drag_closes_the_command` | soltar e arrastar de novo = **2** comandos, e o primeiro `undo` desfaz só o segundo gesto |
| `kit_the_fold_is_keyed_on_property_and_scope` | duas propriedades no mesmo nó não se fundem; a **mesma** propriedade sob Base e sob Dark também não |
| `kit_a_float_round_trip_rewrites_a_coordinate_nobody_touched` | `0.5000000000000001` sobrevive ao double e **não** sobrevive ao float |
| `kit_the_position_round_trip_is_exact_in_doubles_and_lossy_in_floats` | o mesmo dito sobre `positionFrom`/`positionToJson`, a aritmética exata que a Geometry fazia |

**Suíte: 633 casos, 0 falhas** com `IC_CORPUS_DIR` (eram 627 antes; +6).
**Selftest headless:** `5 frame(s), 3 group(s), 4 layer(s), 7 inspector
section(s), 9 diagnostic row(s); textured yes; bytes round-tripped yes; imgui
errors 0`.

Os dois executados **uma vez** cada, pelo PowerShell, na árvore do worktree — a
`iconcomposer.exe` da árvore principal não foi construída nem rodada.

---

## 5. O que só um humano julga

Nada abaixo tem teste e nada abaixo o selftest enxerga:

- **Se o passo de arrasto é bom na mão.** `0.25` pt/px e `0.25` %/px são um
  argumento (parar em número redondo), não uma medida. Só usando se sabe.
- **Se o "Ctrl+click to type" é descoberto.** Está num tooltip. O alvo resolve
  isso com um campo de texto de verdade (`MultiNumericTextField`), que é mais
  trabalho e é o teto; isto é o piso.
- **Se o aviso âmbar de escopo é lido ou vira ruído** por estar sempre lá em
  documento que declara plataforma — que é todo documento real.
- **Se "also written under N other scopes" é informação ou barulho** nas seções
  em que quase sempre há outra.
- **Se o rótulo do espaço de cor ao lado do `ColorEdit4` é entendido** como "a
  amostra está mentindo um pouco" e não como "aqui se escolhe o espaço" — não se
  escolhe.
- **Se os acordes globais atrapalham.** A teoria é que a rota perde para item
  ativo; quem digita em campo com `Ctrl+Z` na mão é que descobre.
- **Todo o visual**: densidade, hierarquia, se o painel cabe na largura real da
  janela do usuário, se rolar 8 seções abertas é aceitável (o alvo tem
  `InspectorScrollView` **e** `InspectorPane` — abas; nós temos uma coluna só).

---

## 6. A auditoria: a lista priorizada

Varredura dos quatro painéis, da barra de menu e dos atalhos. Consertado o que
coube sem entrar em `PanelCanvas.cpp` e `PanelLayers.cpp`. Os dois defeitos que o
briefing já deu por confirmados (canvas sem `PushClipRect`; zoom/pan em
`static ImVec2` com botão do meio, `PanelCanvas.cpp:165,178`) **não** estão
listados — são da frente irmã.

### (a) Quebrado — faz a coisa errada, ou não faz nada

| # | onde | o quê | estado |
|--:|---|---|---|
| **A1** | `Source/app/Window.cpp:102-103` | `close()` e `quit` **sem checar `isDirty()` e sem perguntar**. Perde trabalho sem aviso. O `*` no título (`:113`) é a única pista, e não impede nada. **Agora alcançável por tecla** (§3.5). | **aberto — #1 da próxima rodada** |
| A2 | `MenuBar.cpp:67-89` | nove atalhos anunciados, nenhum ligado | **consertado** §3.5 |
| A3 | `PanelInspector.cpp` (Geometry) | `position` inteiro por `float`; arrastar a escala reescrevia as coordenadas | **consertado** §3.4 |
| A4 | `PanelInspector.cpp` + `PanelLayers.cpp` | a **raiz do documento não era selecionável**; 4 seções inalcançáveis | **consertado** §3.6 (porta provisória no inspetor; a linha definitiva é na árvore) |
| A5 | `PanelInspectorEffects.cpp:106` | o comentário promete que "voltar a ausente é *Remove override*" — e `Section::begin` só desenha esse botão **fora de Base**. Sob Base, escrito um `blur-material` **não há como removê-lo**. Vale para toda propriedade opcional. | **aberto** — mexer nisso muda o contrato compartilhado de `Section`; é decisão de design, não conserto de uma linha |
| A6 | `PanelLayers.cpp:106-107` | comentário diz **o contrário** do código na frase seguinte ("Visible again REMOVES this scope's entry" vs. "writes the boolean, it does not remove the key"). Sobra de uma versão anterior; é o tipo de comentário que faz o próximo "consertar" código correto. | **aberto — frente irmã** |
| A7 | `PanelLayers.cpp:183` | o botão `-` está **sempre habilitado**; sem seleção não faz nada e não diz nada (o `+` ao lado tem tooltip) | **aberto — frente irmã** |

### (b) Hostil — funciona, e é ruim de usar

| # | onde | o quê | estado |
|--:|---|---|---|
| B1 | inspetor inteiro | valor exato só por Ctrl+clique não anunciado | **consertado** §3.3 |
| B2 | `PanelInspector.cpp:653` | escopo editado ≠ composição na tela, sem nada dizendo | **consertado** §3.1 |
| B3 | inspetor inteiro | 7 combos sem rótulo; escopo em 2×100 px idênticos | **consertado** §3.7 |
| B4 | `InspectorSection.h` | `inherited`/`not set` não dizia que a próxima edição **cria** entrada | **consertado** §3.7 |
| B5 | `InspectorSection.h` | `Remove override` não dizia **qual** override | **consertado** §3.7 |
| B6 | `PanelInspector.cpp` (Fill) | espaço de cor da parada invisível; picker sRGB mentindo em silêncio | **consertado** §3.7 |
| B7 | `PanelInspectorAsset.cpp` | erro de import preso na tela ao trocar de camada | **consertado** §3.7 |
| B8 | `PanelInspector.cpp` (Geometry) | passo 1.0 e piso 1 % impedem pousar em valor redondo | **consertado** §3.4 |
| B9 | `MenuBar.cpp:86` | `Delete` de um grupo com camadas **sem confirmação** (undo existe, mas a ação é silenciosa) | aberto — barato, mas precisa de um modal e de onde guardar o estado dele |
| B10 | `PanelInspector.cpp` | o painel é **uma coluna só** com até 8 seções abertas; o alvo tem `InspectorScrollView` **e** `InspectorPane` (abas) | aberto |
| B11 | `PanelLayers.cpp:86` | renomear **só** por duplo-clique; não há item de menu nem atalho, e nada na tela sugere | aberto — frente irmã |
| B12 | `PanelCanvas.cpp:204+` (Diagnostics) | tabela sem cabeçalho, sem ordenação, sem copiar; num documento com muitos `skipped` vira parede de texto | aberto — frente irmã |
| B13 | `PanelInspectorAsset.cpp` | caminho de import **digitado**, sem `Browse` | aberto — ver C4 |
| B14 | `MenuBar.cpp:116-120` | `Preview Size` só 512 e 1024, sem valor livre | aberto |

### (c) Ausente — o alvo tem e nós não

| # | âncora `[BIN]` | o quê | nota |
|--:|---|---|---|
| **C1** | `PasteboardPropertySet` (27), `InspectorCopyablePropertyTypeKey` (3), `PasteboardCommandHandler`, `PasteboardMembers`, `PasteboardLayer` (14) | **Copiar/Colar propriedades**, e copiar/colar camadas. Hoje `MenuBar.cpp:87-88` desenha os dois cinza com "Round 5". A maquinaria do alvo é grande e nomeada; é o maior buraco funcional. | |
| **C2** | `LayerOutlineView` (35), `PasteboardMemberItemDragSource` (8), `ConstrainedDragGesture` (11), `SliceDrop`, `EmptyDrop` | **Arrastar camada para reordenar e para reparentar.** Hoje só `Move Up`/`Move Down` no menu de contexto (`PanelLayers.cpp:88-93`), que não move camada entre grupos **de jeito nenhum**. | frente irmã |
| **C3** | `AxisChoice` (29), `Axis2D` (4), `GradientPlacementView` (7) | **O eixo do gradiente como escolha e como desenho.** Nós temos quatro números. O alvo tem presets de eixo e uma vista do posicionamento. | |
| C4 | — (o Onyx já tem `SystemOpenFileDialog`) | **Botão `Browse` no import.** O Kit não pode abrir diálogo (`Ports.h`), mas `MenuActions` é exatamente o canal para pedir um, e o app já chama o diálogo em `Window.cpp:88`. Falta **um bool** — e uma linha em `Window.cpp`, que não é meu arquivo. | |
| C5 | `LayerSpecularInspector` (3) ao lado de `GroupSpecularInspector` (3) | o alvo tem **especular na camada** além do no grupo; nós só desenhamos no grupo (`PanelInspector.cpp:777`). **Conflito de evidência:** o cabeçalho de `PanelInspectorEffects.cpp` afirma `[ART]` que as 103 ocorrências de `specular` estão todas dentro de `groups[]`. Precisa de um censo que separe grupo de camada antes de virar controle — a regra da casa é não escrever chave não observada. | |
| C6 | `ExportSheet` (7), `ExportOptions` (13), `ExportSize`, `ExportableImage` (11) | **Exportar imagem.** Cinza com "Round 5" em `MenuBar.cpp:74`. | |
| C7 | `LocalizationMenu` (12), `AddLanguageSheet`, `RemapLanguageSheet`, `SupportedLanguagesList`, `DisplayableLanguage` (30) | **Localização.** Cinza em `MenuBar.cpp:89`. | |
| C8 | `RulerTicks` (17), `RulerTicksView`, `LayoutGuide` (22), `LayoutGuideView`, `Checkerboard` (14) | **Réguas, guias e xadrez de transparência** na canvas. Hoje a canvas é um retângulo chapado (`PanelCanvas.cpp`). O xadrez importa: sem ele o alpha do ícone é ilegível sobre cor chapada. | frente irmã |
| C9 | `ZoomableView` (30), `ZoomableViewReader` (8), `Zoomable` (9) | **Zoom/pan como componente**, com o que isso traz (fit, tamanho real, limites). Hoje: um `static ImVec2` e o botão do meio. | frente irmã, já confirmado |
| C10 | `ArrangeCommandHandler` (12) | **Arrange** (trazer para frente / mandar para trás) como comandos de menu. | |
| C11 | `BackgroundChooser` (5), `BackgroundKindPickerButton` (15), `BackgroundImageManager` (17), `BackgroundColorPopoverContent` | **Escolher o fundo da pré-visualização**, inclusive imagem. Hoje `IM_COL32(40,40,44,255)` fixo. | frente irmã |
| C12 | `EffectsRenderModePicker` (7) | um seletor de **modo de render dos efeitos**. Sem contrapartida nossa. | |
| C13 | `StateSavingView` (28), `LazyKeyValueStore` (30), `WindowLayoutConstants` | **O editor não lembra nada** entre execuções: nem escopo, nem zoom, nem qual documento estava aberto, nem o layout. | |
| C14 | `RefractivityGrid` (3), `RefractivityGridView` (4) | a refratividade do alvo tem uma **grade** além dos dois números. | |
| C15 | `GlowPopoverIndicator` (5), `PopupIndicator` (8), `SelectionIndicatorRectangle` (4) | afordâncias visuais de seleção e de "há mais aqui". | |

---

## 7. O que este laudo NÃO cobre

- `PanelCanvas.cpp` e `PanelLayers.cpp` além da leitura para a auditoria: frentes
  irmãs estavam neles.
- `Source/RenderBox/`, `Docs/03-o-motor-de-render.md`, `Docs/README.md`: quarta
  frente.
- Qualquer julgamento de aparência (§5).
