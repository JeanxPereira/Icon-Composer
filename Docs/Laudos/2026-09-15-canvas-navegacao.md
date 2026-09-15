# O canvas: recorte e navegação

*15/09/2026. Decisão de UI, não de engenharia reversa.*

## O que o usuário disse

> "app completamente bugado. mexer nos controles, nas layers, péssimo. **o viewport
> atravessa os botões se eu aumento a escala do icon**, **o zoom com pan suave pra
> mover o viewport não funciona igual o onyx disponibiliza**. tem muita coisa
> errada, muita mesmo, nem citei tudo aqui"

As duas frases em negrito são dois defeitos distintos e ambos estavam em
`Source/IconComposerKit/PanelCanvas.cpp`. Nenhum dos dois era invisível: os dois
estavam no arquivo desde que ele foi escrito, e o gate headless passou verde por
cima dos dois em toda execução. Isso é o ponto de partida deste laudo.

---

## 1. Não havia recorte

### O que estava errado

O corpo do canvas fazia, nesta ordem, dentro da mesma janela:

1. `contextBar(s)` — a fileira de combos (aparência / idiom / tamanho / zoom);
2. `dl->AddRectFilled(origin, corner, …)` — o fundo chapado;
3. `dl->AddImage(view.texture, tl, …)` — o ícone.

Sem `PushClipRect` em lugar nenhum.

O detalhe que fazia isso virar o bug relatado: uma `ImDrawList` de janela já vem
recortada, mas recortada no **inner rect da janela**, que começa **abaixo da menu
bar** e portanto **inclui a fileira de combos**. Quer dizer: o recorte que o ImGui
dá de graça protege a barra de menu e não protege os controles que o próprio
painel desenhou dois passos antes. Como o ícone é desenhado *depois* deles, na
mesma lista, ele pinta por cima.

E `tl` sobe: com

```cpp
const float side = view.width * s.view.zoom;
const ImVec2 tl(origin.x + (avail.x - side) * 0.5f + pan.x, …);
```

basta `side > avail.y` para `tl.y < origin.y`. Aumentar a escala do ícone é
exatamente isso. "O viewport atravessa os botões", literalmente.

### O que foi feito

Um `PushClipRect(origin, corner, true)` em volta de **tudo** que o canvas
desenha — imagem, overlay de seleção e o ponto de "render provisório" —, com
`true` para intersectar com o recorte corrente em vez de substituí-lo, de modo
que os limites da janela continuam valendo.

### Como isso está provado

`Tests/test_kit_canvas.cpp`, caso `canvas_never_paints_outside_the_canvas`. Ele
roda o `drawCanvas` de verdade num contexto ImGui headless, força o ícone para
8× com o canto em (−1800, −1500), e afirma **as duas metades**:

- que o ícone **realmente quer** sair dos quatro lados (`st.image` não está
  contido em `st.clip`) — sem isso o caso passaria em qualquer canvas;
- que o que chega à tela não sai.

A segunda metade não é afirmada só pela aritmética do painel, porque isso seria
o painel concordando consigo mesmo. Ela é lida da **draw data terminada do
ImGui**: o caso varre `ImGui::GetDrawData()`, acha o `ImDrawCmd` cuja textura é a
do ícone, e lê o `ClipRect` dele — que é o scissor que o backend efetivamente
programaria. A barra de ferramentas ocupa `[0, clip.y0)` e o scissor começa em
`clip.y0`; não há pixel do ícone que a alcance.

**Controle negativo, rodado:** comentando o par `PushClipRect`/`PopClipRect` e
reconstruindo, o caso vai a 5 falhas, todas nas asserções de scissor
(`test_kit_canvas.cpp:291-301`). O `PushClipRect` foi restaurado em seguida. O
teste não passa por vacuidade.

---

## 2. A navegação era um esboço

### O que estava errado

```cpp
static ImVec2 pan(0.0f, 0.0f);
…
if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    pan.x += ImGui::GetIO().MouseDelta.x;
    pan.y += ImGui::GetIO().MouseDelta.y;
}
```

Sem roda. Sem zoom ancorado. Sem suavização. Sem limite. Sem "fit". E o `static`:
o comentário que estava acima dele dizia que pan "não vale um campo na Session
que nada mais leria", mas um estático de arquivo não é *ausência* de estado — é
**um** estado compartilhado por todo documento que o processo abrir. Fechar um
documento arrastado para o canto e abrir outro punha o novo no mesmo canto, sem
nada na tela que explicasse por quê. Era bug por si só, e o comentário o
descrevia como economia.

### A referência, e a decisão

`D:/CodingProjects/OnyxSDK/Source/Viewers/ImageViewer.cpp` (268 linhas, projeto
de terceiros, somente leitura) tem a coisa inteira:

| o quê | onde |
|---|---|
| `ZoomToAnchored`, com `clamp(0.125f, 16.0f)` | `ImageViewer.cpp:100-110` |
| `fitZoomFor()` / `centerForZoom()` e o Fit do primeiro frame | `:154-171` |
| roda a 1.15 por entalhe, ancorada no cursor | `:190-195` |
| arrasto com **botão esquerdo OU do meio** | `:198-206` |
| clamp de pan com margem de 80px | `:209-226` |
| lerp exp-decay, coeficiente único, ~150ms | `:228-239` |
| botões zoom-in / zoom-out / `1:1` / `Fit` | `:130-136` |

**Decisão: (b), transcrever a matemática para o Kit.** Não é preferência; são
três motivos com endereço:

1. **Regra 2 da arquitetura.** `Source/IconComposerKit/CMakeLists.txt:22-23` liga
   `Foundation`, `CoreSVG`, `RenderBox` e `imgui_lib`, e nada mais. O Kit é o que
   `ic_tests` e o selftest headless linkam. Usar a classe do Onyx a partir daqui
   vaza Onyx para dentro do Kit e mata o `-DIC_BUILD_UI=OFF`.
2. **A classe não é uma transformação de vista; é um visualizador de arquivo.**
   Ela possui um `TexturePool` e exige um `VkContext` vivo
   (`ImageViewer.cpp:52-59`), faz upload de pixels (`UploadToGPU`), tem um toggle
   de alfa que reescreve a textura na CPU (`ApplyAlphaToggle`), e desenha a
   própria toolbar a partir de `Onyx::Theme` e `SFSymbols`. Nosso canvas já
   recebe a textura pronta pelo `RenderView` e já tem a própria barra. Das 268
   linhas, o que nos serve são umas quarenta — e essas quarenta são puras.
3. **Puras dá para testar.** Que é o item 3 deste laudo.

O que foi transcrito, e onde ficou:

- `canvasClampZoom`, `canvasFitZoom`, `canvasCentrePan`, `canvasZoomAnchored`,
  `canvasClampPan`, `canvasImageRect`, `canvasIntersect`, `canvasEase` —
  declaradas em `Panels.h`, definidas no topo de `PanelCanvas.cpp`. Nenhuma toca
  em ImGui, nenhuma desenha nada.
- Os limites são os do Onyx: `0.125`–`16`, margem de 80px, 1.15 por entalhe,
  `exp(-18·dt)` com `dt` preso em `[1/240, 1/30]`.

**Uma mudança de convenção deliberada:** o `pan` antigo era um *deslocamento a
partir do centro*; o novo é o **canto superior esquerdo do ícone em pixels do
canvas**, que é a convenção do Onyx. O zoom ancorado é uma conta de duas linhas
nessa convenção e uma bagunça na outra.

**Botão de arrasto: esquerdo ou do meio**, como o Onyx (`:199-200`). Esquerdo
*além* do meio porque o botão do meio é o único controle que um trackpad não
tem, e o canvas não tem outro uso para um arrasto com o esquerdo — a seleção
acontece no painel Layers.

---

## 3. O pan deixou de ser `static`

`ViewContext` (em `Session.h`, ao lado do `zoom`) passou a carregar:

```cpp
float zoom, zoomTarget;
float panX, panY, panTargetX, panTargetY;
bool  fitted;                 // false só numa Session recém-aberta
float zoomRequest;  bool fitRequest;
```

Floats soltos e não `ImVec2`: `Session.h` é o header de modelo do Kit e nada mais
nele precisa de Dear ImGui.

O par `x`/`xTarget` é o que torna o movimento suave: todo controle escreve o
**alvo**, e o canvas aproxima o valor corrente do alvo uma vez por frame. Os dois
precisam existir porque o zoom ancorado faz a conta *contra o alvo* — dois
entalhes de roda dentro de um mesmo easing têm que compor, não brigar.

`fitted` sendo falso só numa Session nova é o que dá "Fit ao abrir o documento"
sem evento nenhum para encanar: o primeiro frame do canvas o vê falso, ajusta e
o marca. E como abrir outro documento constrói outra `Session`, o pan do anterior
não pode ser herdado — o que é o conserto inteiro.

---

## 4. O combo de zoom e a roda são um número só

O combo lia e escrevia `s.view.zoom` direto. Com o easing isso passaria a ser
ativamente errado: o frame seguinte desfaria a escrita. E mesmo antes, um zoom
pela roda (que não existia) deixaria o combo mentindo.

Agora há **um caminho só** para dentro da transformação de vista. O combo, os
quatro botões novos da barra (`-`, `+`, `1:1`, `Fit`) e o menu `View > Zoom`
escrevem `view.zoomRequest` / `view.fitRequest`; o canvas consome, ancora no
**centro do viewport** e escreve `zoomTarget`. A roda escreve o mesmo
`zoomTarget`, ancorada no ponteiro. O rótulo e o tique do combo leem `zoomTarget`
de volta — então um zoom de roda a 137% mostra "137%", e não um "100%" parado ao
lado de um ícone que claramente não está a 100%.

Por que os controles não podem escrever `zoomTarget` eles mesmos: o zoom ancorado
precisa do alvo **anterior** para saber como mover o pan. Um controle que
sobrescrevesse o alvo estaria ampliando em torno do canto superior esquerdo do
canvas, que não é onde ninguém está olhando.

`MenuBar.cpp` foi tocado só nisso (a lista de zooms, o campo que ela escreve, e
um item `Fit` novo). As faixas do combo e do menu foram alinhadas em
`25 / 50 / 75 / 100 / 150 / 200 / 400 %`.

---

## O que foi provado, e como

`Tests/test_kit_canvas.cpp`, 7 casos, atrás de `if(TARGET IconComposerKit)` como
`test_kit_idiom.cpp` — um build `-DIC_BUILD_UI=OFF` não tem Kit e tem que seguir
verde.

| caso | o que afirma |
|---|---|
| `canvas_anchored_zoom_keeps_the_texel_under_the_cursor` | 180 combinações de zoom × fator × pan × âncora: o texel sob a âncora está sob a âncora depois. É a diferença inteira entre a roda do Onyx e não ter roda. |
| `canvas_fit_fits_exactly_and_centres` | Fit toca o eixo curto exatamente, não transborda em nenhum, e centra nos dois. Inclui canvas de tamanho zero e render que ainda não chegou. |
| `canvas_clamped_pan_always_leaves_the_icon_in_view` | 588 pans absurdos: depois do clamp a sobreposição é sempre ≥ `min(canvas, ícone) − 80px`. E um pan já legal não é mexido. |
| `canvas_never_paints_outside_the_canvas` | O recorte, no frame real, lido da draw data do ImGui. Com controle negativo. |
| `canvas_opens_fitted_and_does_not_inherit_the_previous_pan` | Abre em Fit e centrado; um segundo documento aberto depois de o primeiro ter sido arrastado ao canto abre centrado. |
| `canvas_zoom_request_and_wheel_share_one_number` | `zoomRequest` vira `zoomTarget`, é consumido, é ancorado no centro, é preso em 16×, e o easing chega ao alvo e para. |
| `canvas_ease_moves_on_every_frame_and_never_teleports` | `dt = 0` não congela o easing e `dt = 10s` não teleporta; nove frames a 60Hz passam de 90%. |

**Suíte: 634 casos, 0 falhas** (era 627; os 7 novos são estes). Com
`IC_CORPUS_DIR` apontando para o corpus de 145 documentos — anote, porque o
worktree tem `References/corpus/` **vazio** e sem a variável a suíte dá 26 falhas
que não são regressão.

**Selftest headless:** `5 frame(s) … textured yes; bytes round-tripped yes; imgui
errors 0`, saída 0.

## O que só um humano julga

Nada abaixo foi verificado e nada abaixo dá para verificar sem alguém na tela:

- **Se está bom de usar.** Não afirmo isso. Afirmo que o ícone não sai do canvas
  e que a matemática da navegação está correta.
- **1.15 por entalhe de roda** é a taxa do Onyx. Pode ser lenta demais num mouse
  com detentes finos ou rápida demais num trackpad.
- **~150ms de easing** pode ser suave ou pode ser preguiçoso. O número é
  `exp(-18·dt)`, do Onyx, e é uma constante de sensação.
- **Margem de 80px** como coleira: é o quanto de ícone sobra na tela no pior
  arrasto. Pode ser pouco.
- **Arrasto com o botão esquerdo** foi escolhido porque o canvas não tem outro
  uso para ele hoje. Se um dia houver clique-para-selecionar no canvas, essa
  escolha vira um conflito.
- **Fit ao abrir** pode surpreender quem esperava 100%.
- **`1:1` significa um texel do preview para um pixel de tela** — o que, com
  preview a 1024, é um ícone de 1024px. Se a expectativa for "tamanho real do
  ícone", a definição está errada e nenhum teste diz isso.
- A fileira da barra ficou com quatro combos e quatro botões. Se isso está
  apertado ou confuso, é uma olhada, não uma medição.

## O que não foi tocado

`Docs/03-o-motor-de-render.md`, `Docs/README.md`, `Source/RenderBox/`,
`PanelLayers.cpp`, `PanelInspector*.cpp`. Nada em `OnyxSDK/` foi escrito.
