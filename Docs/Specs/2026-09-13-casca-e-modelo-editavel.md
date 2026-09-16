# Spec — a casca do editor e o modelo editável

*2026-09-13. Escrito depois de o inventário da UI do alvo ter sido lido do
slice do `IconComposerKit`, e de duas regras de escrita terem sido medidas no
corpus — a grafia de número e a coexistência da propriedade com a sua lista de
especialização. A rodada junta duas frentes que foram desenhadas separadas: a
moldura da UI sobre o Onyx e o modelo que a UI edita.*

---

## 1. O que esta rodada é

O objetivo do projeto é o editor inteiro. Ele foi cortado em cinco rodadas, e
esta é a primeira e a segunda juntas:

| rodada | entrega | esta spec |
|---|---|---|
| 1 | casca: `app/` sobre o Onyx, os painéis, abrir e ver um `.icon` | **sim** |
| 2 | modelo editável, escrita byte-exata, seleção, undo | **sim** |
| 3 | os 16 inspetores sobre `SpecializablePropertyInspector` | os 8 que o modelo já tipa |
| 4 | canvas editável: clique, arrasto, setas, arranjo, importar asset | não |
| 5 | export, painel de cor, localização, fundos, menus completos | não |

Juntar 1 e 2 é decisão registrada (13/09): a casca sozinha seria um
visualizador, e um visualizador construído sem o modelo mutável por baixo é uma
moldura que se refaz quando a edição chega.

---

## 2. O que o binário e o corpus dizem

### 2.1. `[BIN]` O inventário da UI, pelo nome dos tipos

O app é SwiftUI e não tem nib (doc 00 §6). O que um nib daria de graça é a
geometria; o **inventário** está inteiro na metadata Swift do slice
`IconComposerKit.arm64` — 306 tipos de view com nome. Os que esta spec segue:

| peça | tipos |
|---|---|
| layout | `WindowLayoutConstants.{Sidebar, Canvas, Inspector}`, `InspectorPane` |
| sidebar | `LayerSidebar`, `LayerOutlineView` (`GroupRow`, `Row`, `MemberCell`), `ArrangeCommandHandler` (`OrderAmount`, `OrderDirection`, `LayoutSnapshot`) |
| canvas | `EditableIconCompositionCanvas` (`ClickGesture`, `DragGesture`, `DragState`), `CanvasLayer.PixelLayout`, `CanvasSelectionPresentation` |
| inspetores | `Fill`, `Opacity`, `BlendMode`, `Geometry`, `IsHidden`, `Shadow`, `Translucency`, `BlurMaterial`, `LayerSpecular`, `GroupSpecular`, `GroupEffects`, `Refractivity`, `ImageAsset`, `AssetMirroring`, `Background`, `DocumentSettings` — todos sobre `SpecializablePropertyInspector` |
| fora dos painéis | `ExportSheet`, `ExportOptions`, `ColorPicker`, `ColorPickerGrid`, `ColorSlider`, `LocalizationMenu`, `DocumentCommands`, `PreviewDimensionsSettings`, `EffectsRenderModePicker`, `BackgroundKindPickerButton` |

`[OBS]` As constantes de `WindowLayoutConstants` **não foram lidas**. As larguras
desta rodada são as do `sfsymview` (20% / 28%) e estão marcadas como tal.

O material de `_confrontar/editor-ui.md` e `ui-spec.md` já listava quase isto,
sem selo. O nome dos tipos dá a ele o selo que faltava — só o **inventário**;
o comportamento descrito lá continua sendo pergunta.

### 2.2. `[ART]` Como a Apple soletra número

Medido nos 145 `icon.json`, separando token JSON de número dentro de string:

| onde | regra | contagem |
|---|---|---|
| token JSON, não inteiro | o menor round-trip (`Double.description`) | 1.361 de 1.361 |
| token JSON, inteiro | sem `.0` | 1.152 de 1.152 |
| componente de cor, dentro da string | cinco casas fixas | 1.978 de 1.978 |

Um valor **editado** tem que voltar assim, ou o byte-exato morre na primeira
edição. `std::to_chars` sem precisão produz o menor round-trip, e é o que
`Value::number(double)` usa; `Color::toString()` usa `%.5f` por componente.

### 2.3. `[ART]` A propriedade e a lista nunca coexistem

Medido nas 890 listas `<prop>-specializations` do corpus: a chave simples
`<prop>` está **ausente** em 890 de 890. Nunca `null`, nunca valor. E, do doc 01
§5: a entrada sem predicado, quando existe, está sempre no índice 0 (616 de
616), e nenhum predicado se repete.

Então uma propriedade mora em **um** de dois lugares, e "escrever sob escopo" é
uma operação só (§4.3).

---

## 3. Arquitetura

Duas torres novas. O grafo de link da spec de 31/08 vale sem alteração, e as
três regras dela também.

```
Source/IconComposerKit/   ImGui puro. View model, seleção, undo, os painéis, a
                          barra de menu, o selftest. Linka Foundation, CoreSVG
                          e RenderBox. NÃO linka Onyx.
Source/app/               iconcomposer.exe. Onyx: janela, registrar, dock layout,
                          TexturePool, diálogos de arquivo, JobQueue. O ÚNICO
                          alvo que linka Onyx.
```

O Kit fala com a janela por **duas interfaces pequenas**, implementadas em
`app/` e substituídas por nulos nos testes:

```cpp
struct TextureSink {                     // sobe pixels, devolve um id do ImGui
    virtual ImTextureID create(uint32_t w, uint32_t h, const uint8_t* rgba8) = 0;
    virtual bool update(ImTextureID, const uint8_t* rgba8) = 0;
    virtual void remove(ImTextureID) = 0;
};
struct RenderScheduler {                 // "renderiza isto"; a resposta chega depois
    virtual void request(RenderRequest) = 0;        // o último pedido vence
    virtual std::optional<RenderResult> poll() = 0; // na thread principal
};
```

É a divisão `sf_viewer`/`sfsymview` (regra 2), e a razão é a mesma de lá: um
teste que linka Onyx não roda numa máquina sem GPU, e põe sessenta unidades de
tradução no caminho crítico do gate.

**Build.** Onyx entra por `FetchContent`, do jeito do `sfsymview`: a cache var
`IC_ONYX_SOURCE_DIR` aponta para o checkout local (`D:/CodingProjects/OnyxSDK`)
e, quando está vazia, `GIT_TAG` com um SHA fixo — nunca um branch — busca do
GitHub. Sempre `EXCLUDE_FROM_ALL`, `SYSTEM`, e `ONYX_BUILD_MEDIA`,
`ONYX_BUILD_EXAMPLES`, `ONYX_BUILD_TESTS` desligados. O compilador é o MinGW
g++ 13 do preset `mingw`, o mesmo que o `sfsymview` já usa para o Onyx. Uma
opção `IC_BUILD_APP` (ON) deixa o gate configurar sem o Onyx quando quiser.
`ic_tests` ganha `IconComposer::Kit` e continua sem Onyx.

---

## 4. O modelo editável

### 4.1. `json::Value` ganha mutação

Sem perder o que o gate provou. O lexema continua sendo a verdade: mutar um
irmão não re-soletra ninguém.

```cpp
std::vector<Member>& members();      std::vector<Value>& elements();
Value* find(std::string_view key);   // a versão mutável do find
void set(std::string key, Value v);  // substitui ou acrescenta
bool erase(std::string_view key);
static Value number(double);         // menor round-trip; inteiro sem ".0"  (§2.2)
```

`write` já ordena chaves, então **onde** um membro é inserido não importa para
os bytes.

### 4.2. `Layer`, `Group`, `IconDocument` mutáveis

As views ganham o par mutável (`json()` não-const, e os setters de §4.3). A
`IconBundle` ganha `save()` e `saveAs(dir)`: escrita **atômica** (temporário +
rename) de `json::write(root)` sobre `icon.json`; `saveAs` copia `Assets/`.

### 4.3. A operação central: escrever sob escopo

```cpp
// scope = (appearance, idiom). nullopt apaga.
void setProperty(json::Value& owner, std::string_view prop, Context scope,
                 std::optional<json::Value> value);
```

A regra, e cada linha vem de uma medição de §2.3 ou do doc 01 §5:

1. Se `<prop>-specializations` **não existe** e o escopo é `(Base, Base)`:
   escreve a chave simples. `nullopt` apaga a chave.
2. Senão, o alvo é a lista. Cria a lista se falta, **movendo** a chave simples
   para a entrada sem predicado no índice 0 — a chave simples é apagada no mesmo
   passo (nunca coexistem).
3. Na lista, a entrada cujo predicado é **igual** ao escopo (`appearance` e/ou
   `idiom` presentes só quando não são `Base`) recebe `value`; se não existe, é
   criada — sem predicado vai para o índice 0, com predicado vai para o fim.
4. `nullopt` apaga a entrada. Se a lista fica **só** com a entrada sem
   predicado, ela volta a ser chave simples. Se fica vazia, a lista é apagada.

`[INF]` O passo 3 põe entradas novas com predicado **no fim**. O corpus não
tem ordem observável entre predicados (nenhum se repete, doc 01 §5), então isto
não é distinguível por ele. Fica marcado, e um teste diz que ler de volta pelo
`resolve` dá o valor escrito em todos os 20 contextos.

Uma propriedade tipada (cor, `Fill`, `Shadow`, …) vira `json::Value` por um
`toJson()` em `Values.h`, que é o inverso dos `fromJson` já existentes e é
provado por round-trip sobre os 61.537 valores do corpus.

### 4.4. Estrutura também é escrita

Adicionar e remover camada e grupo, mover uma posição para cima ou para baixo,
renomear, e importar asset (copia o arquivo para `Assets/` e escreve
`image-name`). Um grupo ou camada novo nasce com o mínimo que o corpus mostra
num nó recém-criado: `name`, e na camada `image-name`. Remover asset órfão não
acontece nesta rodada; `unusedAssets()` já o nomeia no diagnóstico.

---

## 5. Undo

Por **comando**, com **snapshot de nó**. Um `Command` guarda o caminho do nó
tocado (`groups[1]/layers[2]`, ou a raiz) e o `Value` daquele nó antes e
depois. Desfazer é reatribuir. O maior `icon.json` do corpus tem menos de
200 KB; copiar um nó é barato, e não existe "operação inversa" para errar.

Comandos contíguos na mesma `(caminho, prop, escopo)` **coalescem** enquanto o
mesmo controle está sendo arrastado (o alvo faz isto: `DocumentCommands` com
undo coalescido). Um comando novo invalida o redo. Comandos estruturais (§4.4)
usam o **pai** como nó do snapshot.

`isDirty()` é "a pilha tem algo depois do último save", e a janela mostra isso
no título.

---

## 6. Render e canvas

`app/` cria **um** `rb::Device` no início — validação **desligada**, ao
contrário do default da torre, porque aqui ninguém está conferindo o driver — e
o entrega ao `RenderScheduler`. Dois `VkInstance` no mesmo processo (o do Onyx
e o do RenderBox) é a decisão desta rodada; adotar o `VkContext` do Onyx na
torre fica para quando latência pedir (§9).

**15/09 — a decisão foi desafiada e ficou em pé.** A branch
`alt/device-adotado-do-onyx` (`014c0ec`, hoje apagada e preservada na tag
`rejeitado/device-adotado-do-onyx`) propunha o contrário, e a premissa dela era
que *o volk guarda uma tabela de dispatch só, com escopo de processo*. A
premissa é **verdadeira da tabela global e falsa como impedimento**: o volk
expõe `volkLoadDeviceTable`, que preenche uma tabela **por device** e não toca
em símbolo global nenhum (`volk.c:189` → `volkGenLoadDeviceTable`, que só
escreve em `table->`). É essa a porta que `Source/RenderBox/VulkanApi.h` usa
desde `6ffa00a`, e por isso os dois `VkInstance` convivem **medidos, não
supostos**: com `VK_LAYER_KHRONOS_validation` ligada nos dois instances e sete
renders na janela real, as camadas não disseram uma palavra além do banner de
ativação, e o PNG de 412 px sai **byte a byte igual** pelo caminho com volk e
pelo caminho sem. O que a decisão custa e o que a torna segura estão escritos em
`Docs/Laudos/2026-09-15-device-do-onyx.md` e no comentário de
`Source/app/Window.cpp`.

Cada mudança no documento ou no contexto de visualização (appearance, idiom,
tamanho) gera um `RenderRequest`. O scheduler guarda **só o último** pendente e
submete numa lane única do `JobQueue` do Onyx, então nunca há dois renders em
voo. O job trabalha sobre um **clone** do `json::Value` (sem lock com a UI),
chama `rb::renderIcon`, converte float straight-alpha para `R8G8B8A8_UNORM`
(o formato que o `TexturePool` sobe), e o `Done` na thread principal chama
`update` (mesmo tamanho) ou `create` (tamanho mudou). Enquanto o próximo não
chega, o canvas mostra o anterior com um indicador discreto.

`[OBS]` O RGBA do renderizador é o que o `icrender` grava no PNG; o canvas o
sobe como UNORM e o ImGui o apresenta como está. Se o swapchain do Onyx for
sRGB, o canvas e o PNG discordam num gama — é para medir na primeira imagem, e
o resultado entra no doc 03.

`RenderedIcon::skipped`, `shapeGaps` e `notes` **vão para a tela** (§7), pela
mesma razão que o `icrender` os imprime: figura plausível sem medição não pode
ser silenciosa.

O canvas desenha a textura sobre cor chapada (os seis `sine-*` do alvo são da
rodada 5), com zoom (50–200%) e pan. Overlay de seleção: o retângulo da camada
selecionada, calculado do `viewBox` do SVG mais `position`, na régua
`kCanvasPoints` do `IconRenderer` — `[INF]` a mesma régua, com a mesma marca.

---

## 7. Painéis e menu

Quatro painéis no Kit, dockados por **nome** no layout padrão de `app/`. Os
nomes são os do alvo.

**Layers** (esquerda). A árvore grupo → camada do `LayerOutlineView`. Por
linha: disclosure no grupo, nome, toggle de visibilidade, toggle de vidro.
Renomear por duplo clique. Seleção **simples** nesta rodada; ela dirige o
inspetor. Rodapé com `+` (menu: grupo, camada de imagem) e `−`. Sem
drag-and-drop; "mover para cima/baixo" pelo menu de contexto, que é o mesmo
comando do `ArrangeCommandHandler`.

**Canvas** (centro). §6. Barra em cima com o que o alvo põe na toolbar e no
rodapé do canvas: appearance (os quatro do `[BIN]`, doc 01 §6), idiom, tamanho
de preview (512, 1024), zoom.

**Inspector** (direita). Nesta rodada, os inspetores cujo valor `Values.h` já
tipa e o render já consome: `IsHidden`, `Opacity`, `BlendMode`, `Geometry`
(x, y, escala), `Fill` da camada e do documento (solid com cor por campos
numéricos e `ColorEdit4`, e os kinds automáticos por seletor), `Shadow`,
`Translucency`, `Specular`. Cada seção nasce sobre um
`SpecializableProperty` nosso: seletor de escopo no cabeçalho, valor
resolvido para o escopo, marca de "herdado de Base" quando o escopo não tem
entrada própria, e "remover override". Os outros sete inspetores aparecem
**desabilitados com o motivo no tooltip**, o tratamento que o `sfsymview` dá ao
que não faz.

**Diagnóstico** (embaixo). `skipped`, `shapeGaps`, `notes` do último render;
`missingAssets` e `unknownKeys` do documento.

**Menu.** `File` (New, Open, Open Recent, Save, Save As, Close), `Edit` (Undo,
Redo, Duplicate, Delete), `View` (appearance, idiom, tamanho, painéis), `Layer`
(Add Group, Add Image Layer, Toggle Glass, Toggle Visibility, Move Up, Move
Down). O Onyx desenha `File > Open` e `Recent Files` por conta própria e sem
hook, então `app/` faz o que o `sfsymview` fez em `MenuBar.cpp`: desenha a
barra nossa, e o Onyx fica com o layout, o tema e a janela. Os itens que o alvo
tem e esta rodada não faz (Export, Copy/Paste Properties, Localization) são
desenhados desabilitados com o motivo.

---

## 8. Selftest, testes e gate

**`iconcomposer --selftest <bundle.icon> [--frames N]`** roda o Kit sem janela:
ImGui em backend nulo, `TextureSink` nulo, `RenderScheduler` **síncrono** sobre
o `rb::Device` real. Um script por frame: abre, seleciona, edita opacidade,
desfaz, escreve num temporário, compara bytes com o original. É o `sfsymview
--selftest`, e é o que faz "a UI abre" ser uma asserção em vez de uma foto.

**Testes novos em `ic_tests`**, sem Onyx:

| arquivo | afirma |
|---|---|
| `test_json_mutate` | mutar um membro não re-soletra irmãos; `number(double)` sobre os 1.361 floats e 1.152 inteiros do corpus devolve o lexema original; `Color::toString` sobre os 1.978 componentes idem |
| `test_document_edit` | os quatro casos de §4.3; a invariante "simples e lista nunca coexistem" varrida sobre o corpus após editar-e-desfazer cada propriedade de cada nó; `resolve` lê de volta o que foi escrito nos 20 contextos |
| `test_values_tojson` | round-trip `fromJson → toJson` sobre os 61.537 valores |
| `test_undo` | comando, coalescência, redo invalidado, `isDirty` |
| `test_kit_viewmodel` | seleção, escopo, o que o inspetor marca como herdado |
| `test_kit_selftest` | o script do selftest com sink nulo e scheduler nulo, sem GPU |
| `test_bundle_save` | escrita atômica; abrir, editar, desfazer, salvar dá os bytes originais nos 135 byte-exatos |

**Gate.** `gate-m1.ps1` ganha âncoras nos arquivos novos do Foundation
(mutadores, `setProperty`, `number(double)`, `toJson`, `save`) e nas partes do
Kit sem ImGui (view model, undo). Ao fim da rodada o sweep inteiro roda no
worktree, e o `README` recebe a linha de estado "a UI do app".

---

## 9. Fora desta rodada, por nome

Drag-and-drop na árvore; clique e arrasto no canvas; os sete inspetores
`BlurMaterial`, `GroupEffects`, `Refractivity`, `ImageAsset`, `AssetMirroring`,
`Background`, `DocumentSettings`; export; painel de cor; localização; os fundos
`sine-*`; grid de todas as appearances; multi-seleção; copiar/colar
propriedades; thumbnails na árvore; remover asset órfão; adotar o `VkContext`
do Onyx no `rb::Device`; ler `WindowLayoutConstants`.

---

## 10. As suposições, juntas

| marca | o quê | onde cai |
|---|---|---|
| `[INF]` | entrada com predicado nova vai para o fim da lista | §4.3 |
| `[INF]` | retângulo de seleção = `viewBox` + `position` na régua `kCanvasPoints` | §6 |
| `[OBS]` | larguras dos painéis são as do `sfsymview`, não do alvo | §2.1 |
| `[OBS]` | o gama entre o canvas e o PNG não foi medido | §6 |
| decisão | dois `VkInstance` no processo | §6 |
| decisão | seleção simples; sem drag-and-drop | §7 |

---

## 11. O que 15/09/2026 fez com esta spec

Cinco frentes daquele dia mexeram na casca, e três delas **derrubaram itens desta
própria spec**. Integrado em 16/09/2026.

### 11.1. O canvas abria numa plataforma que documento nenhum declara

`[BIN]` O canvas abria em `Idiom::Base`, e **`Base` não é uma plataforma**: é a
entrada de *fallback* do formato, que casa apenas com as entradas que **não**
nomeiam idiom nenhum. `[ART]` **145 de 145** documentos declaram
`supported-platforms` e **145 de 145** declaram `squares`; sob `Base`, **nenhuma**
das **84 entradas de especialização predicadas por idiom** do corpus resolve.

> **`0 de 84` é o número que condena o padrão antigo.** Abrir em `Base` mostra uma
> composição que **documento nenhum declara suportar** — e, no caso do usuário,
> não aplicava o `scale: 0.8` que ele tinha desenhado.

**O combo nunca esteve quebrado.** O caminho do idiom até o render foi seguido
arquivo por arquivo e está fixado por um caso que dirige o `RenderCoordinator` de
verdade — inclusive a parte que importa e que um teste ingênuo não pega: **uma
resposta que ecoa o contexto ERRADO não apaga o `pending`**.

A regra nova (`ick::declaredIdiom`) foi **lida dos autores**, não inventada:
`[ART]` dos sete documentos que declaram `squares: ["macOS"]` **e** especializam
por idiom, **os sete escrevem `idiom: macOS`**; nenhum escreve `square` sozinho. E
o alcance dela foi medido contra as três alternativas:

| padrão | alcança |
|---|---|
| `base` (o antigo) | **0 de 84**, em 0 documentos |
| sempre `square` | 57 de 84, em 15 documentos |
| **a declaração do documento** | **71 de 84**, em 21 documentos |

`[BIN]` **E o alvo separa `Idiom` de `Platform`** — dois enums distintos, com
mensagens de erro próprias (`"Unknown idiom name: "` em `0x1264A0`,
`"Unknown platform name: "` em `0x126590`), e o seletor da UI do alvo é um
`SwiftUI.State<Platform?>` (`IconComposerKit 0x18DAF2`), **não sobre `Idiom`**.
`[INF]` No alvo, `Idiom` é o vocabulário **do arquivo** e `Platform` o da
**pré-visualização**, validado contra o `supportedPlatforms` do documento (daí o
tipo `ConstrainedPlatform`). **Marcado como inferência, e o modelo não foi
mudado por causa disso** — vira `[OBS]`, não refatoração especulativa.

Rendimento imediato da medição, e vale por si: `idiomLabel(Base)` devolvia
**`"All"`**, que lê como "todas as plataformas" — **o oposto do que o caso faz**.
O alvo chama esse caso de `unspecified`. **A etiqueta agora é "Unspecified".**

**Só a vista se move:** `scope` continua em `Base`, o documento não é tocado,
`version()` não anda, a sessão **não nasce suja**.

### 11.2. O canvas: não havia recorte, e o pan era um `static`

**Isto derruba dois itens do §9** — "clique e arrasto no canvas" e "zoom/pan".

`[BIN]` **Não havia `PushClipRect` em lugar nenhum.** Uma `ImDrawList` de janela
já vem recortada, mas **no *inner rect***, que **inclui a fileira de combos** que
o próprio painel desenhou dois passos antes. Como o ícone é desenhado depois, na
mesma lista, **ele pinta por cima dos controles** assim que `side > avail.y`.

O caso que prende isso afirma **as duas metades**, e é a segunda que o torna
honesto: que o ícone **realmente quer** sair dos quatro lados — *sem isso o caso
passaria em qualquer canvas* — e que o que chega à tela não sai, lido da **draw
data terminada do ImGui**, no `ClipRect` do `ImDrawCmd` cuja textura é a do ícone.
**Controle negativo rodado:** comentando o `PushClipRect` o caso vai a **cinco
falhas**. *"O teste não passa por vacuidade."*

**E o pan era `static ImVec2`.** O comentário acima dele dizia que pan *"não vale
um campo na Session que nada mais leria"* — mas **um estático de arquivo não é
ausência de estado: é UM estado compartilhado por todo documento que o processo
abrir.** Fechar um documento arrastado para o canto e abrir outro punha o novo no
mesmo canto. *"Era bug por si só, e o comentário o descrevia como economia."*

A referência é o `ImageViewer.cpp` do Onyx, e **a decisão foi transcrever a
matemática, não usar a classe**, por três motivos com endereço: a regra 2 da
arquitetura (o Kit liga `Foundation`, `CoreSVG`, `RenderBox` e `imgui_lib` e nada
mais — usar a classe do Onyx aqui **mata o `-DIC_BUILD_UI=OFF`**); a classe **não
é uma transformação de vista, é um visualizador de arquivo** (possui um
`TexturePool`, exige `VkContext` vivo, faz `UploadToGPU`, desenha a própria
toolbar a partir do `Theme` do Onyx); e **das 268 linhas, o que serve são umas
quarenta — e essas quarenta são puras**, portanto testáveis.

**Uma mudança de convenção deliberada:** o `pan` antigo era *deslocamento a partir
do centro*; o novo é o **canto superior esquerdo do ícone em pixels do canvas**,
a convenção do Onyx — *"o zoom ancorado é uma conta de duas linhas nessa convenção
e uma bagunça na outra"*.

E `ViewContext` ganhou os pares `x`/`xTarget`, que são o que torna o movimento
suave. **Os dois precisam existir porque o zoom ancorado faz a conta contra o
alvo:** dois entalhes de roda dentro de um mesmo *easing* **têm de compor, não
brigar**. É a mesma razão pela qual os controles escrevem `zoomRequest` e **não**
`zoomTarget` — um controle que sobrescrevesse o alvo estaria ampliando em torno do
canto superior esquerdo.

### 11.3. O painel de camadas, reescrito contra o `LayerOutlineView`

`[BIN]` O painel do alvo é um `NSOutlineView` **por item, não por índice**, e a
metadata nomeia o que ele tem: um tipo inteiro só para desenhar o indicador de
drop, a área de clique da linha como assunto declarado
(`LargeHitAreaButtonStyle`), seleção múltipla com modificadores, *arrange* em duas
subseções, copiar/colar **lembrando o grupo de origem**, e **diagnóstico dentro da
lista**.

Três dos nove defeitos medidos eram bugs que ninguém tinha visto porque **o gate
headless passava verde por cima deles**:

- **O aberto/fechado do grupo era guardado pela POSIÇÃO.** `PushID((int)g)` põe o
  *disclosure* sob o índice; mover o grupo 0 para baixo **trocava o estado dele
  com o do vizinho**. O alvo endereça por item (`expandItem:`). `[BIN]` E o
  formato **não dá id a um nó** — nenhum documento do corpus carrega chave de
  identidade —, então o estado continua posicional; **o que mudou é que agora o
  dono dele é quem move as posições.**
- **A linha tinha dois itens sobrepostos e um buraco morto:** o espaço à direita
  do nome pertencia ao `TreeNodeEx`, que era `OpenOnArrow` — **clicar ali não
  fazia nada.**
- **Renomear e clicar fora jogava o que foi digitado no lixo.** Só o Enter
  comitava.

**A única mudança fora do painel foi `Session::moveNode(path, delta, coalesce)`.**
Um drop de três linhas são três `moveNode`, e três entradas na pilha para **um
gesto** são três `Ctrl+Z` para desfazer um arrasto — *"exatamente a raiva que este
round existe para tirar"*. O mecanismo já estava documentado em `Session.h`;
**faltava usá-lo para estrutura**. E a chave nomeia **o pai**, não o nó, porque o
índice do nó é justamente o que cada troca muda.

**O que ficou de fora é tão informativo quanto o que entrou**, e cada recusa tem
razão: **seleção múltipla** alargaria `Session::selection`, que é *um round do
Session* e não do painel; **duplicar/colar** poria um gesto na pilha como uma dúzia
de comandos, o oposto do parágrafo acima, e **precisa de `Session::duplicate`**; e
**reparent por arrasto** seria add+remove, dois comandos e um nó novo — então
**soltar fora do pai da linha arrastada é RECUSADO, com nenhum indicador aparecendo,
em vez de feito pela metade.**

### 11.4. O inspetor, e a auditoria do "nem citei tudo"

Esta frente abre com a frase que devia estar no topo de qualquer spec de UI deste
projeto:

> **O que o verde não vê.** O selftest headless imprime `imgui errors 0`,
> `textured yes` e `bytes round-tripped yes`. **Ele imprimiria exatamente isso com
> todos os sliders do painel quebrados**, porque ele não move nenhum, não aperta
> nenhuma tecla e não olha para nada. **Foi acreditar nesse verde que produziu a
> situação atual, e nada neste laudo é justificado por ele.**

**Quatro achados que valem além do conserto:**

**(a) Os atalhos de teclado eram desenho.** `ImGui::MenuItem(label, shortcut, …)`
**desenha** o terceiro argumento e **não liga nada**: a string é uma *foto* de uma
tecla. A barra anunciava **nove acordes**, e uma varredura por `ImGuiKey`,
`IsKeyPressed` ou `Shortcut` em todo o Kit e no `app/` voltava **vazia**. **Nenhum
deles fazia nada — inclusive o `Ctrl+Z`, que é o desfazer para o qual toda a
coalescência do inspetor existe.**

`[OBS]` E ligá-los **aumentou** um risco que já existia: `Ctrl+W` e `Ctrl+Q` agora
funcionam, e `Window.cpp:102-103` fecha e sai **sem checar `isDirty()` e sem
perguntar nada**. O item de menu era igualmente desprotegido; a tecla só torna o
acidente muito mais fácil. **É o A1 da lista abaixo, e o primeiro conserto da
próxima rodada.**

**(b) O escopo editado não era o que a canvas mostrava, e nada dizia.**
`Session::view.context` é o que a canvas renderiza; `Session::scope` é o que o
inspetor escreve; são independentes **de propósito**, e §11.1 acabou de mover a
*view* deixando o *scope* em `Base` **de propósito**. `[ART]` Como 145 de 145
documentos declaram plataforma, **em praticamente todo documento real os dois
discordam desde o primeiro quadro** — e o painel desenhava só o `scope`, em dois
combos de 100 px sem rótulo. *"É literalmente a mentira que o cabeçalho deste
arquivo diz que o painel existe para impedir, e o painel a estava contando."*

**(c) Um bug de BYTES no inspetor de geometria.** A seção lia o `position` inteiro
em `float` e **reescrevia o objeto todo a cada quadro de arrasto** — ou seja,
**arrastar a escala reimprimia as duas coordenadas da translação em precisão de
float**. `[ART]` O corpus escreve coordenadas como `0.5000000000000001`, e um
float não segura esse número. **Nada disso aparece na tela**; aparece como
documento que não bate mais com os bytes de onde foi aberto — a mesma coisa que o
`writeAxis` do fill, dez linhas abaixo, tem um parágrafo inteiro explicando que
não se faz.

**(d) Quatro seções do painel eram inalcançáveis no programa construído.**
`drawInspector` sempre teve ramo `NodeKind::Root`, e `nodeTitle` sempre respondeu
`"Document"` — mas **nada no editor rodando conseguia produzir esse path**.
Estavam plenamente escritas, testadas e desenhadas **no selftest**, que seleciona
por `firstSelectable`. Entre elas, **o único controle sobre `supported-platforms`**
— a chave que 145 de 145 documentos carregam e que decide se o ícone é squircle ou
círculo.

**E uma suspeita do briefing foi DERRUBADA por medida:** a coalescência de arrasto
já funcionava — os oito controles contínuos já passavam `coalesce=true` e fechavam
no `IsItemDeactivatedAfterEdit`. Virou teste porque é regressão fácil de causar. E
o teste achou um detalhe de contagem que vale registrar: **`Session::apply` não
registra comando quando a edição não muda byte nenhum**, então a contagem é **213
e não 214** se o arrasto começar no valor de abertura.

### 11.5. A auditoria priorizada — a coisa mais próxima de um roadmap de UI

Três listas. **(a) quebrado**, **(b) hostil**, **(c) ausente com âncora `[BIN]` no
slice**. A lista (c) é o que o alvo tem e nós não, cada linha com o **nome do tipo
e a contagem de ocorrências** no `IconComposerKit.arm64` — e é por isso que ela
vale como plano: **ela não é uma lista de desejos, é um inventário medido.**

**(a) Quebrado — 7 itens, 3 consertados naquele dia:**

| # | o quê | estado |
|---|---|---|
| **A1** | `close()` e `quit` **sem checar `isDirty()` e sem perguntar**. Perde trabalho sem aviso; o `*` no título é a única pista e não impede nada | **aberto — #1 da próxima rodada** |
| A2 | nove atalhos anunciados, nenhum ligado | consertado |
| A3 | `position` inteiro por `float`; arrastar a escala reescrevia as coordenadas | consertado |
| A4 | a raiz do documento não era selecionável; 4 seções inalcançáveis | consertado (porta provisória; a linha definitiva é na árvore) |
| A5 | sob `Base`, uma propriedade opcional escrita **não tem como ser removida** — `Section::begin` só desenha `Remove override` fora de `Base` | **aberto** — muda o contrato compartilhado de `Section`; é decisão de design |
| A6 | um comentário diz **o contrário** do código na frase seguinte. *"É o tipo de comentário que faz o próximo 'consertar' código correto."* | aberto |
| A7 | o botão `-` está **sempre habilitado**; sem seleção não faz nada e não diz nada | aberto |

**(b) Hostil — 14 itens, 8 consertados.** Os seis abertos: `Delete` de um grupo
com camadas **sem confirmação** (B9); o painel é **uma coluna só** com até 8 seções
abertas, enquanto o alvo tem `InspectorScrollView` **e** `InspectorPane`, isto é
abas (B10); renomear **só** por duplo-clique, sem item de menu nem atalho (B11); a
tabela de Diagnósticos sem cabeçalho, ordenação ou copiar (B12); caminho de import
**digitado**, sem `Browse` (B13); `Preview Size` só 512 e 1024, sem valor livre
(B14).

**(c) Ausente — 15 itens, cada um com âncora `[BIN]`:**

| # | âncora no slice | o quê |
|---|---|---|
| **C1** | `PasteboardPropertySet` (27), `InspectorCopyablePropertyTypeKey` (3), `PasteboardLayer` (14) | **Copiar/colar propriedades e camadas.** *"A maquinaria do alvo é grande e nomeada; é o maior buraco funcional."* |
| **C2** | `LayerOutlineView` (35), `PasteboardMemberItemDragSource` (8), `ConstrainedDragGesture` (11) | **Arrastar camada para reordenar e para REPARENTAR.** O reordenar entrou em §11.3; **mover camada entre grupos continua impossível** |
| **C3** | `AxisChoice` (29), `Axis2D` (4), `GradientPlacementView` (7) | **O eixo do gradiente como escolha e como desenho.** Nós temos quatro números |
| C4 | — (o Onyx já tem `SystemOpenFileDialog`) | **Botão `Browse` no import.** Falta **um bool** no `MenuActions` e uma linha no `app/` |
| C5 | `LayerSpecularInspector` (3) ao lado de `GroupSpecularInspector` (3) | **Especular na CAMADA** além do no grupo. **Conflito de evidência declarado:** `[ART]` as 103 ocorrências de `specular` do corpus estão todas dentro de `groups[]`. **Precisa de um censo que separe grupo de camada antes de virar controle** — a regra da casa é não escrever chave não observada |
| C6 | `ExportSheet` (7), `ExportOptions` (13), `ExportableImage` (11) | **Exportar imagem** |
| C7 | `LocalizationMenu` (12), `DisplayableLanguage` (30), `AddLanguageSheet` | **Localização** |
| C8 | `RulerTicks` (17), `LayoutGuide` (22), `Checkerboard` (14) | **Réguas, guias e xadrez de transparência.** O xadrez importa: **sem ele o alpha do ícone é ilegível sobre cor chapada** |
| C9 | `ZoomableView` (30), `Zoomable` (9) | zoom/pan como componente — **entrou em §11.2** |
| C10 | `ArrangeCommandHandler` (12) | **Arrange** como comandos de menu |
| C11 | `BackgroundChooser` (5), `BackgroundImageManager` (17) | **Escolher o fundo da pré-visualização**, inclusive imagem. Hoje é uma cor fixa |
| C12 | `EffectsRenderModePicker` (7) | um seletor de **modo de render dos efeitos**. Sem contrapartida nossa |
| C13 | `StateSavingView` (28), `LazyKeyValueStore` (30), `WindowLayoutConstants` | **O editor não lembra nada entre execuções** — nem escopo, nem zoom, nem qual documento estava aberto, nem o layout. É o `[OBS]` das constantes de janela do `Docs/README.md`, agora **com o custo dito** |
| C14 | `RefractivityGrid` (3), `RefractivityGridView` (4) | a refratividade do alvo tem uma **grade** além dos dois números |
| C15 | `GlowPopoverIndicator` (5), `PopupIndicator` (8), `SelectionIndicatorRectangle` (4) | afordâncias de seleção e de "há mais aqui" |

### 11.6. O que muda no §9 e no §10 desta spec

**Saem do §9** (foram feitos): drag-and-drop na árvore **para reordenar** (não
para reparentar — C2 continua aberto) e zoom/pan.

**O item "adotar o `VkContext` do Onyx no `rb::Device`" saiu do §9 por
DECISÃO, e não por implementação** — ver §6, que foi reescrita pela frente do
device em 15/09/2026. Em resumo: a branch que o propunha está **apagada**,
preservada na tag `rejeitado/device-adotado-do-onyx`, e o `[BIN]` que a derruba é
que o volk expõe **duas** portas — `volkLoadDevice`, que escreve num estado
**global de processo**, e `volkLoadDeviceTable` (`volk.c:189`), que preenche uma
tabela **por device** e **não toca em símbolo global nenhum**. O `main` já passava
pela segunda. A prova mais forte é de pixel: **render byte-idêntico nos dois
despachos** (`9776C1F6…`), com `IC_BUILD_UI` ligado e desligado.

**"Ler `WindowLayoutConstants`" continua no §9**, e agora tem custo escrito: é o
C13, e o que ele custa é **o editor não lembrar nada entre execuções**.

**Entram no §10, como suposições novas:**

| marca | o quê | onde |
|---|---|---|
| `[INF]` | `Idiom` é vocabulário do arquivo e `Platform` o da pré-visualização; `fullySpecialize` **não foi desmontado** | §11.1 |
| `[INF]` | `1,15` por entalhe de roda, `exp(−18·dt)` e margem de 80 px são **constantes de sensação** do Onyx, não medidas do alvo | §11.2 |
| `[INF]` | renomear comitar ao sair do campo é convenção do AppKit — **âncora fraca, e está dita** | §11.3 |
| `[INF]` | **F2 para renomear é SEM ÂNCORA**: é a grafia desta plataforma do Return que o alvo usa | §11.3 |
| `[INF]` | o texto e o formato do aviso âmbar de escopo **são nossos, sem âncora no alvo** | §11.4 |
| `[OBS]` | o aberto/fechado de um grupo **não sobrevive a um undo** — corrigir exige identidade de nó, **que o formato não tem** | §11.3 |
| `[OBS]` | `1:1` significa "um texel do preview para um pixel de tela". Se a expectativa for "tamanho real do ícone", a definição está errada **e nenhum teste diz isso** | §11.2 |
