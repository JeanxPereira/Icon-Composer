# Laudo — o painel de camadas (15/09/2026)

Laudo de **decisão de UI**. O usuário reprovou o app em uso real e citou "mexer
nos controles, **nas layers**, péssimo" sem detalhar. O método aqui não foi
adivinhar: foi comparar o painel com duas referências concretas — o alvo
(`References/2.0-125/out/slices/IconComposerKit.arm64`) e o Onyx, cujos painéis
são o padrão visual desta janela.

Arquivos: `Source/IconComposerKit/PanelLayers.cpp` (reescrito),
`Source/IconComposerKit/Panels.h` (duas funções puras + dois campos em
`LayersStats`), `Source/IconComposerKit/Session.h`/`.cpp` (um parâmetro com
default), `Tests/test_kit_layers.cpp` (novo).

---

## 1. O que o alvo tem, lido do slice

O painel do alvo é `LayerOutlineView`, um `NSViewRepresentable` em volta de um
`ICOutlineView : NSOutlineView` privado, com um `Coordinator` que conforma
`NSOutlineViewDataSource` e `NSOutlineViewDelegate`. Os *selector stubs* que essa
classe envia são o vocabulário do que o painel sabe fazer:

```
$ python scripts/macho.py syms .../IconComposerKit.arm64 objc_msgSend | grep -iE 'outline|drag|drop|row|select'
_objc_msgSend$registerForDraggedTypes:      _objc_msgSend$setDropItem:dropChildIndex:
_objc_msgSend$draggingPasteboard            _objc_msgSend$draggingSourceOperationMask
_objc_msgSend$setDraggingFormation:         _objc_msgSend$setDraggingSourceOperationMask:forLocal:
_objc_msgSend$expandItem:                   _objc_msgSend$expandItem:expandChildren:
_objc_msgSend$rowForItem:                   _objc_msgSend$itemAtRow:
_objc_msgSend$clickedRow                    _objc_msgSend$setOutlineTableColumn:
_objc_msgSend$setAllowsMultipleSelection:   _objc_msgSend$selectedRowIndexes
_objc_msgSend$selectRowIndexes:byExtendingSelection:
```

e os tipos com nome na metadata Swift:

| Tipo | O que diz |
| --- | --- |
| `LayerOutlineView`, `.Coordinator`, `.Coordinator.Row`, `.Coordinator.MemberCellItem` | a lista é um outline view por item, não por índice |
| `WithDragIndicatorDrawBugWorkaroundPixelModifier` | existe um **indicador de drop desenhado** — um tipo inteiro só sobre desenhá-lo |
| `LayerList.LargeHitAreaButtonStyle` | a **área de clique da linha** é assunto declarado |
| `IconMemberSelection`, `.ModifierFlags`, `.HomogeneousContent` | seleção **múltipla**, com modificadores |
| `ArrangeCommandHandler`, `.LayoutSnapshot`, `MenuContent.ArrangeSection.Subsections` | *arrange* em duas subseções (front/forward/backward/back), aplicado de um snapshot |
| `PasteboardLayer.ContainingGroupRecord`, `PasteboardMembers`, `PasteboardCommandHandler` | copiar/colar camada, lembrando o grupo de origem |
| no corpo de `LayerList`: `onDeleteCommand(perform:)`, `onKeyPress(characters:phases:action:)`, `keyboardShortcut`, `ForEach` sobre `IconComposerFoundation.Diagnostic` com `Image` + `Text` + `Color` | teclado, e **diagnóstico dentro da lista** |

## 2. O que estava errado, medido no código

1. **Reordenar era um clique por posição, dentro de um menu de contexto.**
   `Act::MoveUp/MoveDown` → `moveNode(path, ±1)`, que troca vizinhos. Mover uma
   camada cinco posições eram cinco viagens ao menu. O alvo arrasta e solta
   **entre** linhas (`setDropItem:dropChildIndex:`).
2. **Nenhuma tecla fazia nada.** Sem setas, sem `Delete`, sem F2. O alvo liga
   `onDeleteCommand`, `onKeyPress` e `keyboardShortcut` na lista.
3. **BUG: o aberto/fechado do grupo era guardado pela POSIÇÃO.**
   `PushID((int)g)` + `TreeNodeEx("##group")` põe o estado de disclosure sob o
   índice do grupo. Mover o grupo 0 para baixo **trocava o estado dele com o do
   vizinho**; apagar o grupo 0 entregava o estado dele a quem subisse para o
   índice 0. O alvo endereça por item (`expandItem:`).
4. **BUG: a linha tinha dois itens sobrepostos e um buraco morto.** O
   `TreeNodeEx` pedia `SpanAvailWidth` e o `Selectable` era desenhado por cima
   com `largura - 60px`. O espaço à direita do nome pertencia ao tree node, que
   era `OpenOnArrow` — clicar ali não fazia **nada**. O alvo chama a solução
   disso de `LargeHitAreaButtonStyle`.
5. **BUG: renomear e clicar fora jogava o que foi digitado no lixo.**
   `IsItemDeactivated()` → `g_renaming.reset()` sem gravar. Só o Enter comitava.
6. **Uma camada sem arte, ou com arte que falta em `Assets/`, era invisível na
   lista.** O aviso existia — só no inspetor, e só para a camada já selecionada.
   Achar uma referência quebrada entre quarenta camadas custava quarenta cliques.
7. **Menu de contexto com três itens** (Move Up, Move Down, Delete), sem
   renomear, sem *front/back*, sem atalhos escritos.
8. **O botão `-` clicava no vazio** quando não havia seleção.
9. **A seleção vinda de fora** (undo, canvas) podia ficar fora da rolagem.

## 3. O que foi feito, e com que âncora

| Mudança | Âncora |
| --- | --- |
| **Arrastar e soltar entre linhas**, com indicador desenhado na metade de cima/baixo da linha sob o cursor | `[BIN]` `registerForDraggedTypes:`, `setDropItem:dropChildIndex:`, `WithDragIndicatorDrawBugWorkaroundPixelModifier` |
| Soltar uma camada **no cabeçalho do próprio grupo** = topo do grupo | `[BIN]` `setDropItem:dropChildIndex:` com `childIndex` 0 |
| **A linha inteira é a área de clique** (um `Selectable` de largura cheia, tudo o mais desenhado por cima) | `[BIN]` `LayerList.LargeHitAreaButtonStyle` |
| **Teclado**: ↑↓ andam pelas linhas visíveis, → abre/entra, ← fecha/sobe, `Del`/`Backspace` apaga, `Ctrl`+↑↓ reordena | `[BIN]` `onDeleteCommand(perform:)`, `onKeyPress(characters:phases:action:)`, `keyboardShortcut` |
| **F2** também renomeia | **sem âncora** — é a grafia desta plataforma do Return que o alvo usa. Está dito no código. |
| **Menu de contexto**: Rename / Bring to Front / Move Up / Move Down / Send to Back / Add Image Layer / Delete, com os atalhos escritos | `[BIN]` `MenuContent.ArrangeSection.Subsections` + `ArrangeCommandHandler` |
| **Disclosure é estado do painel**, num vetor que o painel **permuta junto** com os grupos ao mover/apagar/adicionar | `[BIN]` `expandItem:`; o formato não dá id a um nó (`addGroup` escreve `name` e `layers`, e nenhum documento do corpus carrega chave de identidade), então o estado segue posicional — mas agora o dono dele é quem move as posições |
| **Diagnóstico na própria linha**: "sem arte" em cinza, arte ausente em vermelho com `!` e tooltip, resolvido **sob a composição que o canvas mostra** | `[BIN]` `ForEach` sobre `IconComposerFoundation.Diagnostic` no corpo de `LayerList`; `[ONYX]` `DocumentBrowser::DrawEntry` pinta o nó `Failed` com `kFailedColor` e explica no hover |
| **Renomear comita ao sair do campo**; só `Esc` descarta | AppKit: o editor inline de um `NSOutlineView` comita no fim da edição. Âncora fraca, e está dita. |
| **`-` desabilitado** sem seleção, tooltip no botão, e um texto quando não há grupo nenhum | `[ONYX]` `Widgets::IconButtonOpts::disabled` — os painéis do Onyx acinzentam o botão que não pode agir |
| **Rolar até a seleção** quando ela mudou de fora do painel | `[BIN]` `rowForItem:` |
| **Um arrasto = um `Ctrl+Z`** | ver §4 |

### 3.1 A única mudança fora do painel: `Session::moveNode(path, delta, coalesce)`

Um drop de três linhas são três `moveNode`. Três entradas na pilha de undo para
um gesto são três `Ctrl+Z` para desfazer um arrasto — exatamente a raiva que
este round existe para tirar. O mecanismo já estava documentado em `Session.h`
("while a control is being dragged, consecutive edits ... fold into one command;
`endCoalescing` is the drag ending"); faltava usá-lo para estrutura.

A chave nomeia o **pai**, não o nó: o índice do nó é justamente o que cada troca
muda, então uma chave sobre ele não dobraria nada. O snapshot do comando já é o
do pai, então dobrar uma corrida de trocas dá um único *before/after* do pai —
que é precisamente "o arrasto, desfeito". O parâmetro tem default `false`, então
os dois chamadores do `MenuBar` seguem idênticos.

## 4. O que ficou de fora, de propósito

* **Seleção múltipla** (`setAllowsMultipleSelection:`, `IconMemberSelection.ModifierFlags`).
  `Session::selection` é um `optional<NodePath>` que o inspetor e o canvas leem.
  Alargar isso é um round do Session, não do painel, e mexeria num tipo contra o
  qual duas frentes irmãs estão editando agora.
* **Duplicar / copiar e colar** (`PasteboardLayer`, `PasteboardMembers`).
  Não há comando único no modelo para "copie este nó"; compor um de `addLayer` +
  um `setProperty` por chave poria um gesto na pilha como uma dúzia de comandos —
  o oposto do §3.1. Precisa de `Session::duplicate`.
* **Reparent por arrasto.** `setDropItem:dropChildIndex:` do alvo aceita soltar
  uma camada em **outro** grupo; `moveNode` só troca irmãos. Um drop entre pais
  diferentes seria add+remove — dois comandos e um nó novo. Soltar fora do pai da
  linha arrastada é **recusado** (nenhum indicador aparece), não feito pela metade.

## 5. O que foi provado com teste

`Tests/test_kit_layers.cpp`, 8 casos, atrás de `if(TARGET IconComposerKit)`.
A lógica do gesto saiu do frame para funções puras em `Panels.h`:

* `planDrop(from, gap, count)` — o slot onde a linha caiu vira uma corrida de
  `moveNode`. Provado: o custo é **exatamente a distância** (três linhas = três
  trocas, nunca quatro); a assimetria de um slot abaixo da própria linha
  (`to = gap - 1`), que um `to = gap` ingênuo erra por um; soltar nos dois slots
  que encostam na própria linha **não é movimento** (não empurra comando vazio);
  fora de faixa é recusado, não *clampeado*.
* A corrida rodada num `Session` de verdade: a ordem sai `ABCD → BCAD` como o
  slot mandou, **a seleção segue a camada arrastada** (o reindex do Session a
  carrega troca a troca), e o arrasto inteiro é **um** `undo` — com o caso
  gêmeo provando que **dois arrastos seguidos são dois undos**, isto é, que o
  coalescing não engole o segundo gesto.
* `visibleRows(root, expanded)` — a ordem que as setas andam. Provado que um
  grupo fechado esconde suas camadas da caminhada (senão ↓ de um grupo fechado
  pula para dentro de uma camada que ninguém vê), e que um grupo além do fim do
  vetor conta como aberto (é como um grupo recém-adicionado chega).

Suíte: **635 casos, 0 falhas** (era 627; os 8 novos).
Selftest headless: `5 frame(s), 3 group(s), 4 layer(s), 7 inspector section(s),
9 diagnostic row(s); textured yes; bytes round-tripped yes; imgui errors 0`.

## 6. O que só um humano julga

O verde acima **não é evidência de qualidade** — foi confiar nele que produziu o
painel de ontem. O que nenhum teste daqui viu:

1. **Se o indicador de drop aparece onde a mão espera.** A metade de cima da
   linha = acima, a de baixo = abaixo. O limiar é o meio da linha; se na prática
   ele parecer alto ou baixo demais, é um número a mexer.
2. **Se o arrasto "pega".** ImGui exige um limiar de movimento antes de virar
   drag; entre isso e o duplo-clique-para-renomear pode haver briga que só se
   sente com o mouse.
3. **O layout da linha.** A linha é desenhada à mão: seta, nome, e os toggles
   ancorados na borda direita. Alinhamento vertical, largura do nome quando o
   painel está estreito, e o corte do nome (é *clip*, não elipse) precisam de
   olho.
4. **Se o `!` vermelho é legível** na cor de tema do usuário, e se o tooltip da
   linha inteira incomoda ao passar o mouse.
5. **Se ↑↓ brigam com o resto da janela.** O painel só escuta teclado quando tem
   foco, mas quem dá foco a ele é o Onyx/docking, não este arquivo.
6. **Se a falta de seleção múltipla e de duplicar é aceitável neste round** —
   ambas estão no alvo e ambas dependem de um round do `Session`.
7. **Se o aberto/fechado sobrevivendo a um `undo` importa.** Não sobrevive: undo
   e redo acontecem no menu, fora deste arquivo, e um grupo apagado e desfeito
   volta aberto. Corrigir isso exige identidade de nó, que o formato não tem.
