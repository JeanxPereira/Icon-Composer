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
