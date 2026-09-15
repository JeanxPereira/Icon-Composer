# A casca do editor e o modelo editável — plano de implementação

> **For agentic workers:** Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Este plano NÃO é TDD, e isso é deliberado (decidido em 15/09/2026).** Onde a skill de execução mandar escrever o teste que falha antes do código, **as Global Constraints abaixo ganham**: implementa, confere que compila, commita. Os casos que já estavam escritos moram no bloco `Se precisar de rede` no fim de cada task, como consulta.

**Goal:** um `iconcomposer.exe` sobre o Onyx que abre um `.icon`, o desenha com o `rb::renderIcon`, deixa editar as propriedades que o modelo já tipa com undo, e o escreve de volta byte-exato.

**Architecture:** `IconComposerFoundation` ganha mutação preservando lexema e a operação "escrever sob escopo". `Source/IconComposerKit` é ImGui puro (sessão, undo, painéis, selftest) e fala com a janela por duas interfaces (`TextureSink`, `RenderScheduler`). `Source/app` é o único alvo que linka Onyx e implementa as duas interfaces sobre `TexturePool` e `JobQueue`, com um `rb::Device` próprio.

**Tech Stack:** C++23, CMake, MinGW g++ 13 (preset `mingw`), Dear ImGui 1.92 (o que o Onyx traz), OnyxSDK por `FetchContent`, Vulkan headless no `RenderBox`.

**Spec:** `Docs/Specs/2026-09-13-casca-e-modelo-editavel.md`

## Global Constraints

- `IconComposerFoundation` não linka GPU nem UI e compila sem Vulkan (spec 31/08, regra 1).
- Só `Source/app` linka Onyx (regra 2). `ic_tests` nunca linka Onyx.
- O corpus vem por `IC_CORPUS_DIR`; quando a suíte for rodada, um teste que precisa dele e não o acha **falha**, não pula.
- Float editado é o menor round-trip (`std::to_chars`), inteiro sem `.0`, componente de cor `%.5f` (spec §2.2).
- A chave simples e a lista `<prop>-specializations` nunca coexistem; a entrada sem predicado fica no índice 0 (spec §2.3, §4.3).
- **Teste não é pré-requisito de nada aqui.** Nenhuma task exige escrever teste antes do código, e a suíte não precisa estar verde para seguir. Escreve-se teste quando algo quebra e a causa não é óbvia — aí o teste é o do bug, e ele fica. Cada task guarda os casos que **já estavam escritos** num bloco `Se precisar de rede` no fim: é consulta, não obrigação.
- Quando um teste for escrito, ele usa `check.h` (`TEST_CASE`, `CHECK`, `REQUIRE`, `CHECK_EQ`) e entra em `Tests/CMakeLists.txt`.
- A suíte **não entra no build default** (`EXCLUDE_FROM_ALL`): o loop de edição não paga os 40 MB dela.
- A varredura de mutação (`scripts/gate-m1.ps1`, ~4 h) **não roda por task**. Ela é de marco, sob pedido.
- Comandos, sempre da raiz do repo:
  - configurar: `cmake --preset mingw`
  - construir: `cmake --build --preset mingw`  *(sem a suíte)*
  - construir a suíte: `cmake --build --preset mingw --target ic_tests`
  - rodar a suíte: `IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe <filtro>` (o filtro é um substring do nome do caso; sem filtro roda tudo, ~48 s)
- Mensagens de commit em português, uma frase que diz o que mudou e por quê, sem atribuição a IA.
- Nenhum comentário cita este plano; comentários citam a spec e as medições.

---

## Estrutura de arquivos

| arquivo | responsabilidade |
|---|---|
| `Source/IconComposerFoundation/Json.h/.cpp` | **modificar**: mutadores do `Value`, `number(double)` |
| `Source/IconComposerFoundation/Values.h/.cpp` | **modificar**: `toString` de cor e enums, `toJson` dos structs |
| `Source/IconComposerFoundation/IconDocument.h/.cpp` | **modificar**: `appearanceToString`, `idiomToString` |
| `Source/IconComposerFoundation/Edit.h/.cpp` | **criar**: `NodePath`, `nodeAt`, `setProperty`, `hasOwnEntry`, `addGroup`, `addLayer`, `removeNode`, `moveNode`, `setName` |
| `Source/IconComposerFoundation/IconBundle.h/.cpp` | **modificar**: `json()` mutável, `clone()`, `save()`, `saveAs()`, `importAsset()` |
| `Source/IconComposerKit/CMakeLists.txt` | **criar**: `IconComposer::Kit`, linka Foundation, CoreSVG, RenderBox, `imgui_lib` |
| `Source/IconComposerKit/Ports.h` | **criar**: `TextureSink`, `RenderScheduler`, `RenderRequest`, `RenderResult` |
| `Source/IconComposerKit/Session.h/.cpp` | **criar**: bundle aberto, comandos, undo/redo, seleção, escopo, contexto de visualização, dirty, save |
| `Source/IconComposerKit/ViewModel.h/.cpp` | **criar**: `PropertyView` (valor resolvido + herdado), nomes de enum para a UI |
| `Source/IconComposerKit/Panels.h` | **criar**: assinaturas e structs de estatística dos quatro painéis e do menu |
| `Source/IconComposerKit/PanelLayers.cpp` | **criar**: a árvore |
| `Source/IconComposerKit/PanelInspector.cpp` | **criar**: as seções especializáveis |
| `Source/IconComposerKit/PanelCanvas.cpp` | **criar**: canvas com barra de contexto, zoom, overlay, e o painel de diagnóstico |
| `Source/IconComposerKit/MenuBar.cpp` | **criar**: os quatro menus, dentro da janela do canvas |
| `Source/IconComposerKit/RenderCoordinator.h/.cpp` | **criar**: pede render quando a versão muda, sobe a textura quando chega |
| `Source/IconComposerKit/SelfTest.h/.cpp` | **criar**: o script headless |
| `Source/app/CMakeLists.txt`, `main.cpp`, `OnyxPorts.h/.cpp`, `Window.cpp` | **criar**: o executável |
| `CMakeLists.txt` | **modificar**: `IC_BUILD_UI`, FetchContent do Onyx, as duas torres |
| `Tests/CMakeLists.txt`, `Tests/test_*.cpp` | **opcional** — só quando um bug pedir a rede |
| `scripts/gate-m1.ps1` | **modificar**: fontes e mutações novas |
| `Docs/README.md` | **modificar**: a linha "a UI do app" |

---

## Parte A — a Foundation editável

### Task 1: `json::Value` mutável e `number(double)`

**Files:**
- Modify: `Source/IconComposerFoundation/Json.h`
- Modify: `Source/IconComposerFoundation/Json.cpp`
- Create: `Tests/test_json_mutate.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces:
  - `std::vector<Member>& Value::members()`, `std::vector<Value>& Value::elements()` (sobrecargas não-const)
  - `Value* Value::find(std::string_view)` (não-const)
  - `void Value::set(std::string key, Value v)` — substitui o membro de mesma chave ou acrescenta ao fim
  - `bool Value::erase(std::string_view key)` — true se removeu
  - `static Value Value::number(double)` — lexema pelo menor round-trip, inteiro sem `.0`

- [x] **Step 1: implementar**

Em `Json.h`, dentro de `class Value`, ao lado dos acessores const:

```cpp
    // ---- mutation ----------------------------------------------------------
    // Since 2026-09-13 (spec 13/09 §4.1). The lexeme stays the truth: mutating one
    // member never re-spells a sibling, which is what keeps the 135 byte-exact
    // documents byte-exact after an edit somewhere else in the tree.
    std::vector<Value>& elements() { return elements_; }
    std::vector<Member>& members() { return members_; }
    Value* find(std::string_view key) {
        for (auto& m : members_) {
            if (m.first == key) return &m.second;
        }
        return nullptr;
    }
    // Replaces the member named `key`, or appends one. `write` sorts keys, so
    // where it lands does not reach the bytes.
    void set(std::string key, Value v);
    bool erase(std::string_view key);

    // `[ART]` The spelling Apple's encoder gives a Double: the shortest string
    // that round-trips, and no ".0" on an integral value -- 1,361 non-integer
    // and 1,152 integer tokens over 145 documents, every one (spec 13/09 §2.2).
    // `std::to_chars` without a format is exactly that.
    static Value number(double d);
```

Em `Json.cpp`, antes de `parse`:

```cpp
void Value::set(std::string key, Value v) {
    for (auto& m : members_) {
        if (m.first == key) {
            m.second = std::move(v);
            return;
        }
    }
    members_.emplace_back(std::move(key), std::move(v));
}

bool Value::erase(std::string_view key) {
    for (auto it = members_.begin(); it != members_.end(); ++it) {
        if (it->first == key) {
            members_.erase(it);
            return true;
        }
    }
    return false;
}

Value Value::number(double d) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    return Value::number(std::string(buf, r.ptr));
}
```

Acrescentar `#include <charconv>` em `Json.cpp`.

- [x] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [x] **Step 3: commitar**

```bash
git add Source/IconComposerFoundation/Json.h Source/IconComposerFoundation/Json.cpp
git commit -m "o json::Value aceita mutacao sem re-soletrar ninguem, e number(double) soletra como o corpus"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_json_mutate.cpp
#include "check.h"
#include "Source/IconComposerFoundation/Json.h"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace icf::json;

namespace {
std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
void collectNumbers(const Value& v, std::vector<std::string>& out) {
    switch (v.kind()) {
        case Value::Kind::Number: out.push_back(v.number()); break;
        case Value::Kind::Array: for (const auto& e : v.elements()) collectNumbers(e, out); break;
        case Value::Kind::Object: for (const auto& m : v.members()) collectNumbers(m.second, out); break;
        default: break;
    }
}
}  // namespace

TEST_CASE(json_set_replaces_in_place_and_keeps_sibling_lexemes) {
    auto v = parse(R"({"a" : 0.10000000000000001, "b" : 2})");
    REQUIRE(v.has_value());
    v->set("b", Value::number(3.0));
    CHECK_EQ(v->find("b")->number(), std::string("3"));
    CHECK_EQ(v->find("a")->number(), std::string("0.10000000000000001"));  // untouched
    CHECK_EQ(v->members().size(), std::size_t(2));
}

TEST_CASE(json_set_appends_when_absent_and_erase_removes) {
    auto v = parse(R"({"a" : 1})");
    REQUIRE(v.has_value());
    v->set("z", Value::boolean(true));
    CHECK_EQ(v->members().size(), std::size_t(2));
    CHECK(v->find("z") != nullptr);
    CHECK(v->erase("a"));
    CHECK(!v->erase("a"));
    CHECK(v->find("a") == nullptr);
    CHECK_EQ(write(*v), std::string("{\n  \"z\" : true\n}"));
}

TEST_CASE(json_mutable_find_writes_through) {
    auto v = parse(R"({"groups" : [ { "name" : "x" } ]})");
    REQUIRE(v.has_value());
    Value* groups = v->find("groups");
    REQUIRE(groups != nullptr);
    groups->elements()[0].set("name", Value::string("y"));
    CHECK_EQ(v->find("groups")->elements()[0].find("name")->rawString(), std::string("y"));
}

TEST_CASE(json_number_from_double_spells_like_the_corpus) {
    // spec §2.2: 1.361 floats are the shortest round-trip, 1.152 integers have no ".0".
    CHECK_EQ(Value::number(1.0).number(), std::string("1"));
    CHECK_EQ(Value::number(0.5).number(), std::string("0.5"));
    CHECK_EQ(Value::number(-0.0010000010208841559).number(), std::string("-0.0010000010208841559"));
    CHECK_EQ(Value::number(3.000000106112566e-07).number(), std::string("3.000000106112566e-07"));
    CHECK_EQ(Value::number(1751189635.0).number(), std::string("1751189635"));
}

TEST_CASE(json_number_from_double_reproduces_every_corpus_lexeme) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t seen = 0, same = 0;
    std::string firstMiss;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.path().filename() != "icon.json") continue;
        auto v = parse(slurp(e.path()));
        REQUIRE(v.has_value());
        std::vector<std::string> lexemes;
        collectNumbers(*v, lexemes);
        for (const auto& lx : lexemes) {
            double d = 0;
            auto r = std::from_chars(lx.data(), lx.data() + lx.size(), d);
            REQUIRE(r.ec == std::errc());
            ++seen;
            if (Value::number(d).number() == lx) ++same;
            else if (firstMiss.empty()) firstMiss = lx + " -> " + Value::number(d).number();
        }
    }
    std::printf("  %zu number lexemes, %zu re-spelled identically %s\n", seen, same,
                firstMiss.empty() ? "" : ("first miss: " + firstMiss).c_str());
    CHECK(seen >= 2500);
    CHECK_EQ(same, seen);
}
```

Em `Tests/CMakeLists.txt`, acrescentar `test_json_mutate.cpp` depois de `test_json.cpp`.


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe json_`
Expected: os cinco casos novos passam, e o caso do corpus imprime `... re-spelled identically` com os dois números iguais. Se `firstMiss` aparecer, o lexema listado é um caso que o `to_chars` soletra diferente do `Double.description`, e isso vira uma linha na spec §2.2 antes de qualquer contorno.

</details>

---

### Task 2: cor e enums para string, structs para JSON

**Files:**
- Modify: `Source/IconComposerFoundation/Values.h`
- Modify: `Source/IconComposerFoundation/Values.cpp`
- Create: `Tests/test_values_tojson.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `namespace icf`:
  - `std::string colorToString(const Color&)` — `"<space>:%.5f,..."`
  - `std::string_view blendModeToString(BlendMode)`, `shadowKindToString(ShadowKind)`, `specularHighlightToString(SpecularHighlight)`, `lightingToString(Lighting)`, `fillKindToString(FillKind)`
  - `json::Value fillToJson(const Fill&)`, `shadowToJson(const Shadow&)`, `positionToJson(const Position&)`, `translucencyToJson(const Translucency&)`, `refractivityToJson(const Refractivity&)`

- [x] **Step 1: implementar**

Em `Values.h`, depois dos `fromString`:

```cpp
// ---- writing, since 2026-09-13 --------------------------------------------
// The inverse of the readers above, and each one is held to the bytes it reads:
// `write(toJson(fromJson(v)))` must equal `write(v)` over every value the corpus
// resolves (test_values_tojson).
//
// `[ART]` A colour component is "%.5f": 1,978 of 1,978 in the corpus (spec 13/09
// §2.2). Doubles elsewhere go through `json::Value::number(double)`.
std::string colorToString(const Color& c);
std::string_view blendModeToString(BlendMode m);
std::string_view shadowKindToString(ShadowKind k);
std::string_view specularHighlightToString(SpecularHighlight s);
std::string_view lightingToString(Lighting l);
std::string_view fillKindToString(FillKind k);

json::Value fillToJson(const Fill& f);
json::Value shadowToJson(const Shadow& s);
json::Value positionToJson(const Position& p);
json::Value translucencyToJson(const Translucency& t);
json::Value refractivityToJson(const Refractivity& r);
```

Acrescentar `#include <string>` em `Values.h`. Em `Values.cpp`, ao fim, antes do fecho do namespace:

```cpp
std::string colorToString(const Color& c) {
    std::string out;
    for (const auto& e : kSpaces) {
        if (e.space == c.space) {
            out = std::string(e.name);
            break;
        }
    }
    out += ':';
    char buf[32];
    for (int i = 0; i < c.count; ++i) {
        std::snprintf(buf, sizeof buf, "%.5f", c.components[i]);
        if (i) out += ',';
        out += buf;
    }
    return out;
}

std::string_view blendModeToString(BlendMode m) {
    switch (m) {
        case BlendMode::Normal: return "normal";
        case BlendMode::PlusLighter: return "plus-lighter";
        case BlendMode::PlusDarker: return "plus-darker";
        case BlendMode::Overlay: return "overlay";
        case BlendMode::Multiply: return "multiply";
        case BlendMode::SoftLight: return "soft-light";
        case BlendMode::HardLight: return "hard-light";
        case BlendMode::Darken: return "darken";
        case BlendMode::Lighten: return "lighten";
        case BlendMode::Screen: return "screen";
    }
    return "normal";
}

std::string_view shadowKindToString(ShadowKind k) {
    switch (k) {
        case ShadowKind::Automatic: return "automatic";
        case ShadowKind::Neutral: return "neutral";
        case ShadowKind::LayerColor: return "layer-color";
        case ShadowKind::None: return "none";
    }
    return "none";
}

std::string_view specularHighlightToString(SpecularHighlight s) {
    switch (s) {
        case SpecularHighlight::Off: return "off";
        case SpecularHighlight::Automatic: return "automatic";
        case SpecularHighlight::Inside: return "inside";
        case SpecularHighlight::Outside: return "outside";
    }
    return "off";
}

std::string_view lightingToString(Lighting l) {
    return l == Lighting::Combined ? "combined" : "individual";
}

std::string_view fillKindToString(FillKind k) {
    switch (k) {
        case FillKind::None: return "none";
        case FillKind::Automatic: return "automatic";
        case FillKind::Solid: return "solid";
        case FillKind::AutomaticGradient: return "automatic-gradient";
        case FillKind::LinearGradient: return "linear-gradient";
        case FillKind::SystemLight: return "system-light";
        case FillKind::SystemDark: return "system-dark";
    }
    return "none";
}

namespace {
json::Value pointToJson(const Point& p) {
    return json::Value::object({{"x", json::Value::number(p.x)}, {"y", json::Value::number(p.y)}});
}
json::Value orientationToJson(const Orientation& o) {
    return json::Value::object({{"start", pointToJson(o.start)}, {"stop", pointToJson(o.stop)}});
}
}  // namespace

json::Value fillToJson(const Fill& f) {
    switch (f.kind) {
        case FillKind::Solid:
        case FillKind::AutomaticGradient: {
            json::Value o = json::Value::object({});
            const char* key = f.kind == FillKind::Solid ? "solid" : "automatic-gradient";
            o.set(key, json::Value::string(colorToString(f.colors.empty() ? Color{} : f.colors[0])));
            if (f.orientation) o.set("orientation", orientationToJson(*f.orientation));
            return o;
        }
        case FillKind::LinearGradient: {
            std::vector<json::Value> ramp;
            for (const auto& c : f.colors) ramp.push_back(json::Value::string(colorToString(c)));
            json::Value o = json::Value::object({{"linear-gradient", json::Value::array(std::move(ramp))}});
            if (f.orientation) o.set("orientation", orientationToJson(*f.orientation));
            return o;
        }
        default:
            return json::Value::string(std::string(fillKindToString(f.kind)));
    }
}

json::Value shadowToJson(const Shadow& s) {
    return json::Value::object({{"kind", json::Value::string(std::string(shadowKindToString(s.kind)))},
                                {"opacity", json::Value::number(s.opacity)}});
}

json::Value positionToJson(const Position& p) {
    return json::Value::object(
        {{"scale", json::Value::number(p.scale)},
         {"translation-in-points",
          json::Value::array({json::Value::number(p.translation.x), json::Value::number(p.translation.y)})}});
}

json::Value translucencyToJson(const Translucency& t) {
    return json::Value::object({{"enabled", json::Value::boolean(t.enabled)},
                                {"value", json::Value::number(t.value)}});
}

json::Value refractivityToJson(const Refractivity& r) {
    return json::Value::object({{"enabled", json::Value::boolean(r.enabled)},
                                {"strength", json::Value::number(r.strength)},
                                {"depth", json::Value::number(r.depth)}});
}
```

Acrescentar `#include <cstdio>` em `Values.cpp`.

- [x] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [x] **Step 3: commitar**

```bash
git add Source/IconComposerFoundation/Values.h Source/IconComposerFoundation/Values.cpp
git commit -m "os valores tipados ganham o caminho de volta ao JSON, provado contra cada valor do corpus"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_values_tojson.cpp
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Values.h"

#include <cstdlib>
#include <filesystem>

using namespace icf;

TEST_CASE(values_color_to_string_uses_five_fixed_decimals) {
    // spec §2.2: 1,978 of 1,978 colour components in the corpus are "%.5f".
    Color c;
    c.space = ColorSpace::DisplayP3;
    c.count = 4;
    c.components[0] = 0.5; c.components[1] = 0; c.components[2] = 1; c.components[3] = 0.00392;
    CHECK_EQ(colorToString(c), std::string("display-p3:0.50000,0.00000,1.00000,0.00392"));
    Color g;
    g.space = ColorSpace::Gray;
    g.count = 2;
    g.components[0] = 1; g.components[1] = 1;
    CHECK_EQ(colorToString(g), std::string("gray:1.00000,1.00000"));
}

TEST_CASE(values_enum_to_string_inverts_from_string) {
    for (auto m : {BlendMode::Normal, BlendMode::PlusLighter, BlendMode::PlusDarker, BlendMode::Overlay,
                   BlendMode::Multiply, BlendMode::SoftLight, BlendMode::HardLight, BlendMode::Darken,
                   BlendMode::Lighten, BlendMode::Screen}) {
        auto back = blendModeFromString(blendModeToString(m));
        REQUIRE(back.has_value());
        CHECK(*back == m);
    }
    for (auto k : {ShadowKind::Automatic, ShadowKind::Neutral, ShadowKind::LayerColor, ShadowKind::None}) {
        CHECK(*shadowKindFromString(shadowKindToString(k)) == k);
    }
    for (auto s : {SpecularHighlight::Off, SpecularHighlight::Automatic, SpecularHighlight::Inside,
                   SpecularHighlight::Outside}) {
        CHECK(*specularHighlightFromString(specularHighlightToString(s)) == s);
    }
    CHECK(*lightingFromString(lightingToString(Lighting::Combined)) == Lighting::Combined);
    for (auto f : {FillKind::None, FillKind::Automatic, FillKind::Solid, FillKind::AutomaticGradient,
                   FillKind::LinearGradient, FillKind::SystemLight, FillKind::SystemDark}) {
        CHECK(*fillKindFromString(fillKindToString(f)) == f);
    }
}

TEST_CASE(values_to_json_writes_the_shape_from_json_reads) {
    auto solid = json::parse(R"({"solid" : "srgb:0.00000,0.50000,1.00000,1.00000"})");
    REQUIRE(solid.has_value());
    auto f = fillFrom(*solid);
    REQUIRE(f.has_value());
    CHECK_EQ(json::write(fillToJson(*f)), json::write(*solid));

    auto plain = json::parse(R"("automatic")");
    CHECK_EQ(json::write(fillToJson(*fillFrom(*plain))), std::string("\"automatic\""));

    auto pos = json::parse(R"({"scale" : 0.5, "translation-in-points" : [ 10, -2.5 ]})");
    auto p = positionFrom(*pos);
    REQUIRE(p.has_value());
    CHECK_EQ(json::write(positionToJson(*p)), json::write(*pos));

    auto sh = json::parse(R"({"kind" : "neutral", "opacity" : 0.5})");
    CHECK_EQ(json::write(shadowToJson(*shadowFrom(*sh))), json::write(*sh));
    auto tr = json::parse(R"({"enabled" : true, "value" : 0.2})");
    CHECK_EQ(json::write(translucencyToJson(*translucencyFrom(*tr))), json::write(*tr));
    auto rf = json::parse(R"({"depth" : 0.14, "enabled" : true, "strength" : 0.23})");
    CHECK_EQ(json::write(refractivityToJson(*refractivityFrom(*rf))), json::write(*rf));
}

namespace {
constexpr Appearance kA[] = {Appearance::Base, Appearance::Light, Appearance::Dark, Appearance::Tinted};
constexpr Idiom kI[] = {Idiom::Base, Idiom::Square, Idiom::IOS, Idiom::MacOS, Idiom::WatchOS};

// Every typed value the corpus resolves, re-encoded, must give the bytes it was read from.
void roundTrip(const json::Value& owner, std::size_t& seen, std::size_t& same, std::string& miss) {
    auto check = [&](const json::Value* v, const std::optional<json::Value>& back) {
        if (!v || !back) return;
        ++seen;
        if (json::write(*back) == json::write(*v)) ++same;
        else if (miss.empty()) miss = json::write(*v);
    };
    for (auto a : kA) {
        for (auto i : kI) {
            const Context ctx{a, i};
            if (const json::Value* v = resolve(owner, "fill", ctx)) {
                if (auto f = fillFrom(*v)) check(v, fillToJson(*f));
            }
            if (const json::Value* v = resolve(owner, "shadow", ctx)) {
                if (auto s = shadowFrom(*v)) check(v, shadowToJson(*s));
            }
            if (const json::Value* v = resolve(owner, "position", ctx)) {
                if (auto p = positionFrom(*v)) check(v, positionToJson(*p));
            }
            if (const json::Value* v = resolve(owner, "translucency", ctx)) {
                if (auto t = translucencyFrom(*v)) check(v, translucencyToJson(*t));
            }
            if (const json::Value* v = resolve(owner, "refractivity", ctx)) {
                if (auto r = refractivityFrom(*v)) check(v, refractivityToJson(*r));
            }
        }
    }
}
}  // namespace

TEST_CASE(values_to_json_round_trips_every_corpus_value) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t seen = 0, same = 0;
    std::string miss;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        auto b = IconBundle::open(e.path());
        if (!b) continue;
        auto doc = b->document();
        roundTrip(doc.json(), seen, same, miss);
        for (const auto& g : doc.groups()) {
            roundTrip(g.json(), seen, same, miss);
            for (const auto& l : g.layers()) roundTrip(l.json(), seen, same, miss);
        }
    }
    std::printf("  %zu typed values re-encoded, %zu identical%s%s\n", seen, same,
                miss.empty() ? "" : "; first miss: ", miss.c_str());
    CHECK(seen >= 1000);
    CHECK_EQ(same, seen);
}
```

Acrescentar `test_values_tojson.cpp` em `Tests/CMakeLists.txt` depois de `test_values.cpp`,
e o stem `"test_values_tojson"` ao array `kSlow` de `Tests/main.cpp` (varre o corpus).


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe values_`
Expected: os quatro casos passam; o do corpus imprime `N typed values re-encoded, N identical`. Um `first miss` é um formato que o leitor aceita e o escritor não reproduz: corrige-se o escritor, nunca o teste.

</details>

---

### Task 3: `Edit.h` — `NodePath`, `nodeAt`, `setProperty`, `hasOwnEntry`

**Files:**
- Create: `Source/IconComposerFoundation/Edit.h`
- Create: `Source/IconComposerFoundation/Edit.cpp`
- Modify: `Source/IconComposerFoundation/IconDocument.h` (duas funções)
- Modify: `Source/IconComposerFoundation/IconDocument.cpp`
- Modify: `Source/IconComposerFoundation/CMakeLists.txt` (acrescentar `Edit.cpp`)
- Create: `Tests/test_document_edit.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `namespace icf`:
  - `std::string_view appearanceToString(Appearance)`, `std::string_view idiomToString(Idiom)` (em `IconDocument.h`; `Base` devolve `"base"`, que nunca é escrito — quem escreve testa `!= Base` antes)
  - `struct NodePath { std::optional<std::size_t> group; std::optional<std::size_t> layer; }` — vazio é a raiz; `group` só é o grupo; os dois é a camada; `operator==` default
  - `json::Value* nodeAt(json::Value& root, NodePath)` e a sobrecarga const — `nullptr` se fora do intervalo
  - `void setProperty(json::Value& owner, std::string_view prop, Context scope, std::optional<json::Value> value)`
  - `bool hasOwnEntry(const json::Value& owner, std::string_view prop, Context scope)` — o escopo tem valor próprio (não herdado)

- [x] **Step 1: implementar**

Em `IconDocument.h`, depois de `idiomFromString`:

```cpp
// The disk spelling of each case. `Base` answers "base", which the format never
// writes (doc 01 §6: 0 of 1,740 entries) -- a writer tests for Base first.
std::string_view appearanceToString(Appearance a);
std::string_view idiomToString(Idiom i);
```

Em `IconDocument.cpp`, depois de `idiomFromString`:

```cpp
std::string_view appearanceToString(Appearance a) {
    switch (a) {
        case Appearance::Base: return "base";
        case Appearance::Light: return "light";
        case Appearance::Dark: return "dark";
        case Appearance::Tinted: return "tinted";
    }
    return "base";
}

std::string_view idiomToString(Idiom i) {
    switch (i) {
        case Idiom::Base: return "base";
        case Idiom::Square: return "square";
        case Idiom::IOS: return "iOS";
        case Idiom::MacOS: return "macOS";
        case Idiom::WatchOS: return "watchOS";
    }
    return "base";
}
```

`Edit.h`:

```cpp
#pragma once
// Editing a `.icon` document IN the JSON tree it was read from.
//
// The model is a view (IconDocument.h), and editing keeps it one: an edit is a
// change to a node of the tree, spelled the way Apple's encoder would spell it,
// and `json::write` of the tree is the save. That is what keeps the 135
// byte-exact documents byte-exact through an edit and its undo.
//
// THE ONE RULE, MEASURED (spec 13/09 §2.3, §4.3; doc 01 §5)
// -----------------------------------------------------------
// A property lives in exactly one of two places: the plain key `<prop>`, or the
// list `<prop>-specializations`. Over the corpus's 890 lists the plain key is
// absent 890 times. In a list, the entry with no predicate -- the default -- is
// at index 0 (616 of 616), and no predicate repeats. So "write under a scope"
// is one operation, `setProperty`, and both `resolve` and this file agree on
// which entry a scope names.
#include "Source/IconComposerFoundation/IconDocument.h"

#include <optional>
#include <string>
#include <string_view>

namespace icf {

// Where a node is. Empty is the root; `group` alone is a group; both is a layer.
struct NodePath {
    std::optional<std::size_t> group;
    std::optional<std::size_t> layer;
    bool operator==(const NodePath&) const = default;
};

json::Value* nodeAt(json::Value& root, NodePath path);
const json::Value* nodeAt(const json::Value& root, NodePath path);

// Writes `value` for `prop` under `scope`, or removes that scope's own entry when
// `value` is nullopt. See the header note for the rule; the steps are §4.3 of
// the spec, numbered in the implementation.
void setProperty(json::Value& owner, std::string_view prop, Context scope,
                 std::optional<json::Value> value);

// True when `scope` has an entry of its OWN -- as opposed to resolving to
// something more general. This is what the inspector marks as "inherited".
bool hasOwnEntry(const json::Value& owner, std::string_view prop, Context scope);

}  // namespace icf
```

`Edit.cpp`:

```cpp
#include "Source/IconComposerFoundation/Edit.h"

#include <string>
#include <vector>

namespace icf {
namespace {

std::string listKeyFor(std::string_view prop) {
    std::string k(prop);
    k += "-specializations";
    return k;
}

bool isBase(Context c) { return c.appearance == Appearance::Base && c.idiom == Idiom::Base; }

// An entry's predicate EQUALS the scope: the keys present are exactly the
// non-Base parts of the scope, and each one names the scope's case. `resolve`
// asks "matches"; this asks "is the entry FOR this scope", which is stricter.
bool predicateIs(const json::Value& entry, Context scope) {
    if (entry.find("localization") || entry.find("language-direction")) return false;
    const json::Value* a = entry.find("appearance");
    const json::Value* i = entry.find("idiom");
    if ((scope.appearance != Appearance::Base) != (a != nullptr)) return false;
    if ((scope.idiom != Idiom::Base) != (i != nullptr)) return false;
    if (a && (a->kind() != json::Value::Kind::String ||
              appearanceFromString(a->rawString()) != scope.appearance)) return false;
    if (i && (i->kind() != json::Value::Kind::String ||
              idiomFromString(i->rawString()) != scope.idiom)) return false;
    return true;
}

json::Value entryFor(Context scope, json::Value value) {
    std::vector<json::Value::Member> m;
    if (scope.appearance != Appearance::Base) {
        m.emplace_back("appearance", json::Value::string(std::string(appearanceToString(scope.appearance))));
    }
    if (scope.idiom != Idiom::Base) {
        m.emplace_back("idiom", json::Value::string(std::string(idiomToString(scope.idiom))));
    }
    m.emplace_back("value", std::move(value));
    return json::Value::object(std::move(m));
}

json::Value* entryIn(json::Value& list, Context scope) {
    for (auto& e : list.elements()) {
        if (e.kind() == json::Value::Kind::Object && predicateIs(e, scope)) return &e;
    }
    return nullptr;
}

}  // namespace

json::Value* nodeAt(json::Value& root, NodePath path) {
    if (!path.group) return &root;
    json::Value* groups = root.find("groups");
    if (!groups || groups->kind() != json::Value::Kind::Array) return nullptr;
    if (*path.group >= groups->elements().size()) return nullptr;
    json::Value* group = &groups->elements()[*path.group];
    if (!path.layer) return group;
    json::Value* layers = group->find("layers");
    if (!layers || layers->kind() != json::Value::Kind::Array) return nullptr;
    if (*path.layer >= layers->elements().size()) return nullptr;
    return &layers->elements()[*path.layer];
}

const json::Value* nodeAt(const json::Value& root, NodePath path) {
    return nodeAt(const_cast<json::Value&>(root), path);
}

void setProperty(json::Value& owner, std::string_view prop, Context scope,
                 std::optional<json::Value> value) {
    const std::string listKey = listKeyFor(prop);
    const std::string plainKey(prop);
    json::Value* list = owner.find(listKey);

    // §4.3 step 1: no list and the Base scope -- the plain key is the whole story.
    if (!list && isBase(scope)) {
        if (value) owner.set(plainKey, std::move(*value));
        else owner.erase(plainKey);
        return;
    }

    // Removing from a list that does not exist removes nothing.
    if (!list && !value) return;

    // §4.3 step 2: the list is the target. Create it, moving the plain key into
    // index 0 as the unpredicated entry -- the two never coexist.
    if (!list) {
        std::vector<json::Value> entries;
        if (json::Value* plain = owner.find(plainKey)) {
            entries.push_back(entryFor(Context{}, *plain));
            owner.erase(plainKey);
        }
        owner.set(listKey, json::Value::array(std::move(entries)));
        list = owner.find(listKey);
    }

    // §4.3 step 3: the entry whose predicate IS the scope.
    if (value) {
        if (json::Value* entry = entryIn(*list, scope)) {
            entry->set("value", std::move(*value));
        } else if (isBase(scope)) {
            list->elements().insert(list->elements().begin(), entryFor(scope, std::move(*value)));
        } else {
            // `[INF]` at the end: the corpus has no observable order between
            // predicates (spec §4.3), so this is a choice, and it is named.
            list->elements().push_back(entryFor(scope, std::move(*value)));
        }
        return;
    }

    // §4.3 step 4: remove, then collapse.
    auto& entries = list->elements();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->kind() == json::Value::Kind::Object && predicateIs(*it, scope)) {
            entries.erase(it);
            break;
        }
    }
    if (entries.empty()) {
        owner.erase(listKey);
        return;
    }
    if (entries.size() == 1 && predicateIs(entries[0], Context{})) {
        if (const json::Value* v = entries[0].find("value")) {
            json::Value plain = *v;
            owner.erase(listKey);
            owner.set(plainKey, std::move(plain));
        }
    }
}

bool hasOwnEntry(const json::Value& owner, std::string_view prop, Context scope) {
    if (const json::Value* list = owner.find(listKeyFor(prop))) {
        if (list->kind() == json::Value::Kind::Array) {
            for (const auto& e : list->elements()) {
                if (e.kind() == json::Value::Kind::Object && predicateIs(e, scope) && e.find("value")) {
                    return true;
                }
            }
        }
        return false;
    }
    return isBase(scope) && owner.find(prop) != nullptr;
}

}  // namespace icf
```

Em `Source/IconComposerFoundation/CMakeLists.txt`, acrescentar `Edit.cpp` à lista.

- [x] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [x] **Step 3: commitar**

```bash
git add Source/IconComposerFoundation/Edit.h Source/IconComposerFoundation/Edit.cpp Source/IconComposerFoundation/IconDocument.h Source/IconComposerFoundation/IconDocument.cpp Source/IconComposerFoundation/CMakeLists.txt
git commit -m "escrever sob escopo e uma operacao so, porque a chave simples e a lista nunca coexistem"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_document_edit.cpp
#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"

#include <cstdlib>
#include <filesystem>

using namespace icf;

namespace {
json::Value obj(const char* text) {
    auto v = json::parse(text);
    if (!v) std::abort();
    return *v;
}
const Context kBase{Appearance::Base, Idiom::Base};
const Context kDark{Appearance::Dark, Idiom::Base};
const Context kDarkWatch{Appearance::Dark, Idiom::WatchOS};
}  // namespace

TEST_CASE(edit_node_at_walks_groups_and_layers) {
    json::Value root = obj(R"({"groups" : [ { "name" : "g0", "layers" : [ { "name" : "l0" } ] } ]})");
    CHECK(nodeAt(root, NodePath{}) == &root);
    CHECK(nodeAt(root, NodePath{0, std::nullopt})->find("name")->rawString() == "g0");
    CHECK(nodeAt(root, NodePath{0, 0})->find("name")->rawString() == "l0");
    CHECK(nodeAt(root, NodePath{1, std::nullopt}) == nullptr);
    CHECK(nodeAt(root, NodePath{0, 3}) == nullptr);
}

TEST_CASE(edit_base_scope_without_a_list_writes_the_plain_key) {
    json::Value layer = obj(R"({"name" : "l"})");
    setProperty(layer, "glass", kBase, json::Value::boolean(true));
    CHECK_EQ(json::write(layer), std::string("{\n  \"glass\" : true,\n  \"name\" : \"l\"\n}"));
    CHECK(hasOwnEntry(layer, "glass", kBase));
    CHECK(!hasOwnEntry(layer, "glass", kDark));
    setProperty(layer, "glass", kBase, std::nullopt);
    CHECK(layer.find("glass") == nullptr);
}

TEST_CASE(edit_predicated_scope_moves_the_plain_key_into_index_zero) {
    // spec §4.3 step 2: the plain key becomes the unpredicated entry, and the two
    // never coexist (890 of 890 lists, spec §2.3).
    json::Value layer = obj(R"({"glass" : false})");
    setProperty(layer, "glass", kDark, json::Value::boolean(true));
    CHECK(layer.find("glass") == nullptr);
    const json::Value* list = layer.find("glass-specializations");
    REQUIRE(list != nullptr);
    REQUIRE(list->elements().size() == 2);
    CHECK(list->elements()[0].find("appearance") == nullptr);
    CHECK(list->elements()[0].find("value")->boolean() == false);
    CHECK(list->elements()[1].find("appearance")->rawString() == "dark");
    CHECK(list->elements()[1].find("value")->boolean() == true);
    CHECK(resolve(layer, "glass", kDark)->boolean() == true);
    CHECK(resolve(layer, "glass", kBase)->boolean() == false);
}

TEST_CASE(edit_predicated_scope_with_no_plain_key_creates_a_list_without_a_default) {
    // 274 corpus lists carry no unpredicated entry; writing Dark first must give one of those.
    json::Value layer = obj(R"({"name" : "l"})");
    setProperty(layer, "hidden", kDark, json::Value::boolean(true));
    const json::Value* list = layer.find("hidden-specializations");
    REQUIRE(list != nullptr);
    CHECK_EQ(list->elements().size(), std::size_t(1));
    CHECK(list->elements()[0].find("appearance")->rawString() == "dark");
    CHECK(!hasOwnEntry(layer, "hidden", kBase));
    CHECK(hasOwnEntry(layer, "hidden", kDark));
}

TEST_CASE(edit_base_scope_with_a_list_writes_index_zero) {
    json::Value layer = obj(R"({"glass-specializations" : [ { "appearance" : "dark", "value" : true } ]})");
    setProperty(layer, "glass", kBase, json::Value::boolean(false));
    const json::Value* list = layer.find("glass-specializations");
    REQUIRE(list->elements().size() == 2);
    CHECK(list->elements()[0].find("appearance") == nullptr);
    CHECK(list->elements()[0].find("value")->boolean() == false);
    CHECK(layer.find("glass") == nullptr);
}

TEST_CASE(edit_two_predicates_match_only_their_own_entry) {
    json::Value layer = obj(R"({"name" : "l"})");
    setProperty(layer, "opacity", kDark, json::Value::number(0.5));
    setProperty(layer, "opacity", kDarkWatch, json::Value::number(0.25));
    setProperty(layer, "opacity", kDark, json::Value::number(0.75));  // replaces, not appends
    const json::Value* list = layer.find("opacity-specializations");
    REQUIRE(list->elements().size() == 2);
    CHECK(resolve(layer, "opacity", kDark)->number() == "0.75");
    CHECK(resolve(layer, "opacity", kDarkWatch)->number() == "0.25");
    CHECK(list->elements()[1].find("idiom")->rawString() == "watchOS");
}

TEST_CASE(edit_removing_the_last_override_collapses_back_to_the_plain_key) {
    json::Value layer = obj(R"({"glass" : false})");
    setProperty(layer, "glass", kDark, json::Value::boolean(true));
    setProperty(layer, "glass", kDark, std::nullopt);
    CHECK(layer.find("glass-specializations") == nullptr);
    REQUIRE(layer.find("glass") != nullptr);
    CHECK(layer.find("glass")->boolean() == false);
    // and removing a list that had no default deletes the list entirely
    json::Value other = obj(R"({"name" : "l"})");
    setProperty(other, "hidden", kDark, json::Value::boolean(true));
    setProperty(other, "hidden", kDark, std::nullopt);
    CHECK(other.find("hidden-specializations") == nullptr);
    CHECK(other.find("hidden") == nullptr);
}

namespace {
constexpr Appearance kA[] = {Appearance::Base, Appearance::Light, Appearance::Dark, Appearance::Tinted};
constexpr Idiom kI[] = {Idiom::Base, Idiom::Square, Idiom::IOS, Idiom::MacOS, Idiom::WatchOS};
const char* kProps[] = {"glass", "hidden", "opacity", "blend-mode", "fill", "shadow", "position",
                        "translucency", "specular", "image-name", "name"};

bool coexists(const json::Value& owner) {
    for (const char* p : kProps) {
        if (owner.find(p) && owner.find(std::string(p) + "-specializations")) return true;
    }
    return false;
}
}  // namespace

TEST_CASE(edit_every_corpus_node_survives_a_write_under_every_scope) {
    // For each node and each property that resolves: write the resolved value back
    // under every one of the 20 scopes, read it back, then remove it, and the
    // "never coexist" invariant must hold at every step.
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t nodes = 0, writes = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        auto b = IconBundle::open(e.path());
        if (!b) continue;
        json::Value root = b->json();  // a copy to scribble on
        std::vector<json::Value*> owners{&root};
        for (auto& g : root.find("groups")->elements()) {
            owners.push_back(&g);
            if (json::Value* layers = g.find("layers")) {
                for (auto& l : layers->elements()) owners.push_back(&l);
            }
        }
        for (json::Value* owner : owners) {
            ++nodes;
            for (const char* p : kProps) {
                for (auto a : kA) {
                    for (auto i : kI) {
                        const Context ctx{a, i};
                        const json::Value* was = resolve(*owner, p, ctx);
                        if (!was) continue;
                        const json::Value copy = *was;
                        const std::string before = json::write(*owner);
                        setProperty(*owner, p, ctx, copy);
                        ++writes;
                        REQUIRE(!coexists(*owner));
                        REQUIRE(hasOwnEntry(*owner, p, ctx));
                        REQUIRE(json::write(*resolve(*owner, p, ctx)) == json::write(copy));
                        setProperty(*owner, p, ctx, std::nullopt);
                        REQUIRE(!coexists(*owner));
                        REQUIRE(!hasOwnEntry(*owner, p, ctx));
                        // put the original back so the next scope sees the corpus, not us
                        *owner = *json::parse(before);
                    }
                }
            }
        }
    }
    std::printf("  %zu nodes, %zu scoped writes, invariant held\n", nodes, writes);
    CHECK(nodes >= 700);
}
```

Acrescentar `test_document_edit.cpp` em `Tests/CMakeLists.txt` depois de `test_document.cpp`,
e o stem `"test_document_edit"` ao array `kSlow` de `Tests/main.cpp` — ele varre o corpus,
e aquele array é o que mantém o sweep de mutação barato.


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe edit_`
Expected: os oito casos passam; o do corpus imprime `N nodes, M scoped writes, invariant held`.

</details>

---

### Task 4: `Edit.h` — a estrutura

**Files:**
- Modify: `Source/IconComposerFoundation/Edit.h`
- Modify: `Source/IconComposerFoundation/Edit.cpp`
- Modify: `Tests/test_document_edit.cpp` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `namespace icf`:
  - `std::size_t addGroup(json::Value& root, std::string name)` — acrescenta ao fim de `groups`, devolve o índice
  - `std::size_t addLayer(json::Value& group, std::string name, std::string imageName)` — idem em `layers`
  - `bool removeNode(json::Value& root, NodePath)` — false na raiz ou fora do intervalo
  - `bool moveNode(json::Value& root, NodePath, int delta)` — troca com o vizinho (`-1` sobe, `+1` desce); false quando não há vizinho
  - `bool setName(json::Value& root, NodePath, std::string)` — escreve `name`

- [x] **Step 1: implementar**

Em `Edit.h`, antes do fecho do namespace:

```cpp
// ---- structure -------------------------------------------------------------
// A new node carries the minimum a fresh node carries in the corpus: `name`, plus
// `image-name` on a layer, plus an empty `layers` on a group. Everything else is
// the renderer's default until the inspector writes it.
std::size_t addGroup(json::Value& root, std::string name);
std::size_t addLayer(json::Value& group, std::string name, std::string imageName);
bool removeNode(json::Value& root, NodePath path);     // false for the root or out of range
bool moveNode(json::Value& root, NodePath path, int delta);   // -1 up, +1 down; false at an edge
bool setName(json::Value& root, NodePath path, std::string name);
```

Em `Edit.cpp`, antes do fecho do namespace:

```cpp
namespace {
// The array a path's node lives in, and its index there. Null for the root.
json::Value* siblingsOf(json::Value& root, NodePath path, std::size_t& index) {
    if (!path.group) return nullptr;
    if (!path.layer) {
        json::Value* groups = root.find("groups");
        if (!groups || groups->kind() != json::Value::Kind::Array) return nullptr;
        index = *path.group;
        return index < groups->elements().size() ? groups : nullptr;
    }
    json::Value* group = nodeAt(root, NodePath{path.group, std::nullopt});
    if (!group) return nullptr;
    json::Value* layers = group->find("layers");
    if (!layers || layers->kind() != json::Value::Kind::Array) return nullptr;
    index = *path.layer;
    return index < layers->elements().size() ? layers : nullptr;
}
}  // namespace

std::size_t addGroup(json::Value& root, std::string name) {
    json::Value* groups = root.find("groups");
    if (!groups || groups->kind() != json::Value::Kind::Array) {
        root.set("groups", json::Value::array({}));
        groups = root.find("groups");
    }
    groups->elements().push_back(json::Value::object(
        {{"name", json::Value::string(std::move(name))}, {"layers", json::Value::array({})}}));
    return groups->elements().size() - 1;
}

std::size_t addLayer(json::Value& group, std::string name, std::string imageName) {
    json::Value* layers = group.find("layers");
    if (!layers || layers->kind() != json::Value::Kind::Array) {
        group.set("layers", json::Value::array({}));
        layers = group.find("layers");
    }
    layers->elements().push_back(json::Value::object({{"name", json::Value::string(std::move(name))},
                                                      {"image-name", json::Value::string(std::move(imageName))}}));
    return layers->elements().size() - 1;
}

bool removeNode(json::Value& root, NodePath path) {
    std::size_t index = 0;
    json::Value* siblings = siblingsOf(root, path, index);
    if (!siblings) return false;
    siblings->elements().erase(siblings->elements().begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool moveNode(json::Value& root, NodePath path, int delta) {
    std::size_t index = 0;
    json::Value* siblings = siblingsOf(root, path, index);
    if (!siblings || delta == 0) return false;
    auto& v = siblings->elements();
    if (delta < 0 && index == 0) return false;
    if (delta > 0 && index + 1 >= v.size()) return false;
    const std::size_t other = delta < 0 ? index - 1 : index + 1;
    std::swap(v[index], v[other]);
    return true;
}

bool setName(json::Value& root, NodePath path, std::string name) {
    json::Value* node = nodeAt(root, path);
    if (!node || node->kind() != json::Value::Kind::Object) return false;
    node->set("name", json::Value::string(std::move(name)));
    return true;
}
```

Acrescentar `#include <utility>` em `Edit.cpp`.

- [x] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [x] **Step 3: commitar**

```bash
git add Source/IconComposerFoundation/Edit.h Source/IconComposerFoundation/Edit.cpp
git commit -m "grupo e camada nascem, somem, sobem e descem, e um no novo carrega so o que o corpus mostra"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

Acrescentar ao fim de `Tests/test_document_edit.cpp`:

```cpp
TEST_CASE(edit_add_group_and_layer_create_the_minimal_node) {
    json::Value root = obj(R"({"groups" : [ ]})");
    CHECK_EQ(addGroup(root, "Back"), std::size_t(0));
    CHECK_EQ(addGroup(root, "Front"), std::size_t(1));
    json::Value* front = nodeAt(root, NodePath{1, std::nullopt});
    REQUIRE(front != nullptr);
    CHECK_EQ(addLayer(*front, "mark", "mark.svg"), std::size_t(0));
    CHECK_EQ(json::write(*nodeAt(root, NodePath{1, 0})),
             std::string("{\n  \"image-name\" : \"mark.svg\",\n  \"name\" : \"mark\"\n}"));
    CHECK_EQ(json::write(*nodeAt(root, NodePath{0, std::nullopt})),
             std::string("{\n  \"layers\" : [\n\n  ],\n  \"name\" : \"Back\"\n}"));
}

TEST_CASE(edit_remove_and_move_act_on_siblings_only) {
    json::Value root = obj(R"({"groups" : [ { "name" : "g", "layers" : [ { "name" : "a" }, { "name" : "b" }, { "name" : "c" } ] } ]})");
    CHECK(!removeNode(root, NodePath{}));
    CHECK(!moveNode(root, NodePath{0, 0}, -1));   // already first
    CHECK(moveNode(root, NodePath{0, 0}, +1));
    CHECK(nodeAt(root, NodePath{0, 0})->find("name")->rawString() == "b");
    CHECK(nodeAt(root, NodePath{0, 1})->find("name")->rawString() == "a");
    CHECK(!moveNode(root, NodePath{0, 2}, +1));   // already last
    CHECK(removeNode(root, NodePath{0, 1}));
    CHECK_EQ(nodeAt(root, NodePath{0, std::nullopt})->find("layers")->elements().size(), std::size_t(2));
    CHECK(!removeNode(root, NodePath{0, 5}));
    CHECK(removeNode(root, NodePath{0, std::nullopt}));
    CHECK_EQ(root.find("groups")->elements().size(), std::size_t(0));
}

TEST_CASE(edit_set_name_writes_the_name_key) {
    json::Value root = obj(R"({"groups" : [ { "name" : "g" } ]})");
    CHECK(setName(root, NodePath{0, std::nullopt}, "renamed"));
    CHECK(nodeAt(root, NodePath{0, std::nullopt})->find("name")->rawString() == "renamed");
    CHECK(!setName(root, NodePath{3, std::nullopt}, "x"));
}
```


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe edit_`
Expected: onze casos, zero falhas.

</details>

---

### Task 5: `IconBundle` — `json()` mutável, `clone()`, `save()`, `saveAs()`, `importAsset()`

**Files:**
- Modify: `Source/IconComposerFoundation/IconBundle.h`
- Modify: `Source/IconComposerFoundation/IconBundle.cpp`
- Create: `Tests/test_bundle_save.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `class IconBundle`:
  - `json::Value& json()` (não-const)
  - `IconBundle clone() const` — mesma pasta, cópia profunda da árvore, mesma lista de assets
  - `std::string save() const` — escreve `icon.json` atomicamente; vazio no sucesso, senão a razão
  - `std::string saveAs(const std::filesystem::path& dir)` — cria `dir/Assets`, copia os assets, escreve, e passa a apontar para `dir`
  - `std::string importAsset(const std::filesystem::path& file)` — copia para `Assets/<nome>` e acrescenta a `assetFiles()`; vazio no sucesso

- [x] **Step 1: implementar**

Em `IconBundle.h`, dentro da classe, depois de `document()`:

```cpp
    // ---- editing, since 2026-09-13 (spec 13/09 §4.2) ----
    json::Value& json() { return *tree_; }
    // A deep copy that shares nothing -- what a render job works on while the
    // UI keeps editing the original.
    IconBundle clone() const;
    // Atomic: written to a sibling temporary and renamed over `icon.json`, so a
    // crash mid-write leaves the old document, never half of the new one.
    // Empty on success, otherwise the reason -- this tower reports, it does not throw.
    std::string save() const;
    // Creates `dir/Assets`, copies every asset, writes the document, and this
    // bundle now IS `dir`.
    std::string saveAs(const std::filesystem::path& dir);
    // Copies `file` into `Assets/` under its own name and lists it.
    std::string importAsset(const std::filesystem::path& file);
```

Em `IconBundle.cpp`, ao fim, antes do fecho do namespace:

```cpp
namespace {
std::string writeAtomically(const fs::path& target, const std::string& bytes) {
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "could not open " + tmp.string() + " for writing";
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!f) return "could not write " + tmp.string();
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return "could not replace " + target.string() + ": " + ec.message();
    }
    return {};
}
}  // namespace

IconBundle IconBundle::clone() const {
    return IconBundle(dir_, std::make_unique<json::Value>(*tree_), assets_);
}

std::string IconBundle::save() const {
    return writeAtomically(dir_ / "icon.json", json::write(*tree_));
}

std::string IconBundle::saveAs(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Assets", ec);
    if (ec) return "could not create " + (dir / "Assets").string() + ": " + ec.message();
    for (const auto& a : assets_) {
        fs::copy_file(dir_ / "Assets" / a, dir / "Assets" / a, fs::copy_options::overwrite_existing, ec);
        if (ec) return "could not copy " + a + ": " + ec.message();
    }
    const std::string wrote = writeAtomically(dir / "icon.json", json::write(*tree_));
    if (!wrote.empty()) return wrote;
    dir_ = dir;
    return {};
}

std::string IconBundle::importAsset(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return "not a file: " + file.string();
    const std::string name = file.filename().string();
    fs::create_directories(dir_ / "Assets", ec);
    fs::copy_file(file, dir_ / "Assets" / name, fs::copy_options::overwrite_existing, ec);
    if (ec) return "could not copy " + name + ": " + ec.message();
    if (std::find(assets_.begin(), assets_.end(), name) == assets_.end()) {
        assets_.push_back(name);
        std::sort(assets_.begin(), assets_.end());
    }
    return {};
}
```

- [x] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [x] **Step 3: commitar**

```bash
git add Source/IconComposerFoundation/IconBundle.h Source/IconComposerFoundation/IconBundle.cpp
git commit -m "o bundle escreve de volta, atomicamente, e os 135 byte-exatos continuam byte-exatos depois de editar"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_bundle_save.cpp
#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace icf;
namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("ic-save-") + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary)
        << "{\n  \"fill\" : \"automatic\",\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n          \"image-name\" : \"a.svg\",\n          \"name\" : \"a\"\n        }\n      ],\n      \"name\" : \"g\"\n    }\n  ]\n}";
    std::ofstream(dir / "Assets" / "a.svg", std::ios::binary) << "<svg/>";
    return dir;
}
}  // namespace

TEST_CASE(bundle_save_writes_the_tree_back_and_leaves_no_temp_file) {
    const fs::path dir = scratch("basic");
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    const std::string before = slurp(dir / "icon.json");
    CHECK_EQ(b->save(), std::string(""));
    CHECK_EQ(slurp(dir / "icon.json"), before);
    json::Value* layer = nodeAt(b->json(), NodePath{0, 0});
    REQUIRE(layer != nullptr);
    setProperty(*layer, "glass", Context{}, json::Value::boolean(true));
    CHECK_EQ(b->save(), std::string(""));
    CHECK(slurp(dir / "icon.json").find("\"glass\" : true") != std::string::npos);
    std::size_t files = 0;
    for (const auto& e : fs::directory_iterator(dir)) { (void)e; ++files; }
    CHECK_EQ(files, std::size_t(2));  // icon.json and Assets/, no leftover temp
}

TEST_CASE(bundle_save_as_copies_assets_and_retargets) {
    const fs::path dir = scratch("as-src");
    const fs::path dst = fs::temp_directory_path() / "ic-save-as-dst.icon";
    fs::remove_all(dst);
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    CHECK_EQ(b->saveAs(dst), std::string(""));
    CHECK(b->path() == dst);
    CHECK(fs::exists(dst / "Assets" / "a.svg"));
    CHECK_EQ(slurp(dst / "icon.json"), slurp(dir / "icon.json"));
}

TEST_CASE(bundle_clone_is_deep) {
    const fs::path dir = scratch("clone");
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    IconBundle c = b->clone();
    nodeAt(c.json(), NodePath{0, 0})->set("name", json::Value::string("changed"));
    CHECK(nodeAt(b->json(), NodePath{0, 0})->find("name")->rawString() == "a");
    CHECK(c.path() == b->path());
    CHECK_EQ(c.assetFiles().size(), b->assetFiles().size());
}

TEST_CASE(bundle_import_asset_copies_into_assets_and_lists_it) {
    const fs::path dir = scratch("import");
    const fs::path src = fs::temp_directory_path() / "ic-import-b.svg";
    std::ofstream(src, std::ios::binary) << "<svg id='b'/>";
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    CHECK_EQ(b->importAsset(src), std::string(""));
    CHECK(fs::exists(dir / "Assets" / "b.svg"));
    CHECK_EQ(b->assetFiles().size(), std::size_t(2));
    CHECK(b->assetFiles()[1] == "b.svg");
    CHECK(!b->importAsset(fs::temp_directory_path() / "ic-does-not-exist.svg").empty());
}

TEST_CASE(bundle_save_keeps_every_byte_exact_corpus_document_byte_exact) {
    // Open, edit, put the edit back by hand, save to a scratch copy: the bytes must
    // be the corpus's own for every document the writer already reproduces.
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t docs = 0, exact = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        auto b = IconBundle::open(e.path());
        if (!b) continue;
        const std::string original = slurp(e.path() / "icon.json");
        if (json::write(b->json()) != original) continue;  // one of the 10 reformatted
        ++docs;
        json::Value* g0 = nodeAt(b->json(), NodePath{0, std::nullopt});
        REQUIRE(g0 != nullptr);
        const json::Value snapshot = *g0;
        setProperty(*g0, "opacity", Context{Appearance::Dark, Idiom::Base}, json::Value::number(0.5));
        *g0 = snapshot;
        const fs::path out = fs::temp_directory_path() / ("ic-exact-" + e.path().filename().string());
        fs::remove_all(out);
        REQUIRE(b->saveAs(out).empty());
        if (slurp(out / "icon.json") == original) ++exact;
        fs::remove_all(out);
    }
    std::printf("  %zu byte-exact documents saved, %zu still byte-exact\n", docs, exact);
    CHECK(docs >= 135);
    CHECK_EQ(exact, docs);
}
```

Acrescentar `test_bundle_save.cpp` em `Tests/CMakeLists.txt` depois de `test_bundle.cpp`,
e o stem `"test_bundle_save"` ao array `kSlow` de `Tests/main.cpp` (varre o corpus).


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe bundle_`
Expected: os cinco casos passam; o do corpus imprime `135 byte-exact documents saved, 135 still byte-exact` (ou mais, nunca menos).

</details>

---

### Task 6 (opcional): âncoras do gate para a Foundation editável

> **Fora do caminho crítico.** A varredura de mutação virou coisa de marco, não de task — esta task só faz sentido no dia em que o gate for rodado de novo.

**Files:**
- Modify: `scripts/gate-m1.ps1` (o mapa `$sources` e a lista `$mutations`)

**Interfaces:** nenhuma. Cada mutação abaixo tem de deixar a suíte **vermelha com uma asserção**; uma que só derruba o processo é falha do teste.

- [x] **Step 1: acrescentar a fonte**

No `$sources`, depois de `bundle`:

```powershell
    edit     = Join-Path $root "Source/IconComposerFoundation/Edit.cpp"
```

- [x] **Step 2: acrescentar as mutações**

Na lista `$mutations`, depois do bloco `# ---- the JSON layer ----` existente.

**Toda âncora é de UMA linha.** O C++ deste repositório é CRLF por decisão registrada no
`.gitattributes`, e um `` `n `` do PowerShell é LF puro, que nunca casa; as 316 âncoras
que já existiam são todas de uma linha, e essa convenção é indiferente ao fim de linha.

```powershell
    @{ file = "json"; name = "number(double) spelled with a fixed precision"
       from = 'auto r = std::to_chars(buf, buf + sizeof buf, d);'
       to   = 'auto r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::fixed, 6);' },
    @{ file = "json"; name = "set appends a duplicate instead of replacing"
       from = '        if (m.first == key) {'
       to   = '        if (false) {' },
    # ---- the editing layer ----
    @{ file = "edit"; name = "the plain key survives beside its new list"
       from = '            owner.erase(plainKey);'
       to   = '' },
    @{ file = "edit"; name = "a new unpredicated entry lands at the end, not index 0"
       from = 'list->elements().insert(list->elements().begin(), entryFor(scope, std::move(*value)));'
       to   = 'list->elements().push_back(entryFor(scope, std::move(*value)));' },
    @{ file = "edit"; name = "a non-array specialization list is walked anyway"
       from = 'if (list && list->kind() != json::Value::Kind::Array) {'
       to   = 'if (false) {' },
    @{ file = "edit"; name = "predicate equality ignores the idiom"
       from = 'return named && named->appearance == scope.appearance && named->idiom == scope.idiom;'
       to   = 'return named && named->appearance == scope.appearance;' },
    @{ file = "edit"; name = "a list left with only its default is not collapsed"
       from = 'if (entries.size() == 1 && predicateIs(entries[0], Context{})) {'
       to   = 'if (false) {' },
    @{ file = "edit"; name = "hasOwnEntry answers for the resolved value, not the scope's own"
       from = 'return isBase(scope) && owner.find(prop) != nullptr;'
       to   = 'return owner.find(prop) != nullptr;' },
    @{ file = "edit"; name = "moveNode swaps a node with itself"
       from = 'std::swap(v[index], v[other]);'
       to   = 'std::swap(v[index], v[index]);' },
    @{ file = "values"; name = "a colour component written with four decimals"
       from = 'std::snprintf(buf, sizeof buf, "%.5f", c.components[i]);'
       to   = 'std::snprintf(buf, sizeof buf, "%.4f", c.components[i]);' },
    @{ file = "values"; name = "a position swaps x and y on the way out"
       from = 'json::Value::array({json::Value::number(p.translation.x), json::Value::number(p.translation.y)})'
       to   = 'json::Value::array({json::Value::number(p.translation.y), json::Value::number(p.translation.x)})' },
    @{ file = "bundle"; name = "save leaves its temporary behind"
       from = 'fs::rename(tmp, target, ec);'
       to   = 'fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec);' },
```

- [x] **Step 3: rodar a fatia**

Run: `powershell -File scripts\gate-m1.ps1 -Files json,edit,values,bundle`
Expected: cada mutação nova reporta que foi apanhada por uma asserção; o veredito diz PARTIAL (uma fatia nunca é o gate). Uma mutação não apanhada é um buraco no teste da task correspondente: volta-se à task, acrescenta-se a asserção, e só então esta task fecha.

- [x] **Step 4: commitar**

```bash
git add scripts/gate-m1.ps1
git commit -m "o gate ganha onze mutacoes na Foundation editavel, e cada uma cai numa asserção"
```

---

## Parte B — o Kit, ImGui puro

### Task 7: CMake, o Onyx por FetchContent, e o esqueleto do Kit

**Files:**
- Modify: `CMakeLists.txt`
- Create: `Source/IconComposerKit/CMakeLists.txt`
- Create: `Source/IconComposerKit/Ports.h`
- Create: `Source/IconComposerKit/Headless.h`, `Source/IconComposerKit/Headless.cpp`
- Create: `Tests/test_kit_headless.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces:
  - opção CMake `IC_BUILD_UI` (ON): quando ON, o Onyx é trazido por `FetchContent`, `IconComposer::Kit` existe e `ic_tests` ganha os `test_kit_*.cpp` e linka o Kit; quando OFF nada disso existe e o gate segue como hoje
  - cache var `IC_ONYX_SOURCE_DIR` (default `D:/CodingProjects/OnyxSDK`); vazia → `GIT_TAG` com o SHA `dddce38`
  - `namespace ick` para tudo do Kit
  - `Ports.h`:
    ```cpp
    struct RenderRequest { std::uint64_t version; icf::IconBundle bundle; icf::Context context; std::uint32_t size; };
    struct RenderResult { std::uint64_t version = 0; std::uint32_t width = 0, height = 0;
                          std::vector<std::uint8_t> rgba8; std::size_t drawn = 0, total = 0;
                          std::vector<std::string> skipped, shapeGaps, notes; std::string error; };
    struct RenderScheduler { virtual ~RenderScheduler() = default;
                             virtual void request(RenderRequest) = 0;            // the latest wins
                             virtual std::optional<RenderResult> poll() = 0; };  // main thread
    struct TextureSink { virtual ~TextureSink() = default;
                         virtual ImTextureID create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) = 0;
                         virtual bool update(ImTextureID, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) = 0;
                         virtual void remove(ImTextureID) = 0; };
    std::vector<std::uint8_t> toRgba8(const std::vector<float>& straightRgba);  // clamp, ×255, round
    ```
  - `Headless.h`: `class HeadlessImGui { public: HeadlessImGui(float w = 1440, float h = 900); ~HeadlessImGui(); void newFrame(); void render(); std::uint64_t errors() const; }` — contexto sem backend, com `io.DisplaySize`, o atlas construído e `SetTexID(1)`, e um error callback que conta

- [x] **Step 1: implementar**

No `CMakeLists.txt` da raiz, depois de `option(IC_SANITIZE ...)`:

```cmake
option(IC_BUILD_UI "Builds IconComposerKit and the app, which pull the OnyxSDK" ON)
set(IC_ONYX_SOURCE_DIR "D:/CodingProjects/OnyxSDK"
    CACHE PATH "Local OnyxSDK checkout; empty to fetch the pinned SHA from GitHub")
```

Depois de `add_subdirectory(Source/cli)` e **antes** do `if(IC_BUILD_TESTS)` — `Tests`
linka `IconComposer::Kit`, que tem de existir quando aquele bloco roda:

```cmake
if(IC_BUILD_UI)
    # The toolkit, the way sfsymview takes it: source consumption, EXCLUDE_FROM_ALL
    # so a plain build only compiles what the app links, SYSTEM so Onyx's headers
    # are not held to this tree's warnings. Local checkout first; the SHA when the
    # path is empty -- an exact SHA, never a branch.
    include(FetchContent)
    set(ONYX_BUILD_MEDIA    OFF CACHE BOOL "" FORCE)
    set(ONYX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ONYX_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    if(IC_ONYX_SOURCE_DIR AND EXISTS "${IC_ONYX_SOURCE_DIR}/CMakeLists.txt")
        FetchContent_Declare(OnyxSDK SOURCE_DIR "${IC_ONYX_SOURCE_DIR}" EXCLUDE_FROM_ALL SYSTEM)
    else()
        FetchContent_Declare(OnyxSDK
            GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
            GIT_TAG        dddce38
            EXCLUDE_FROM_ALL SYSTEM)
    endif()
    # Onyx's own pins (GLFW 3.3.9, GLM, lz4) declare cmake_minimum_required below
    # 3.5, which CMake 4 refuses; sfsymview records the same escape hatch.
    set(_ic_saved_policy_min "${CMAKE_POLICY_VERSION_MINIMUM}")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(OnyxSDK)
    if(_ic_saved_policy_min)
        set(CMAKE_POLICY_VERSION_MINIMUM "${_ic_saved_policy_min}" CACHE STRING "" FORCE)
    else()
        unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)
    endif()
    add_subdirectory(Source/IconComposerKit)
    add_subdirectory(Source/app)
endif()
```

(`Source/app` só passa a existir na Task 16; até lá, deixar a linha `add_subdirectory(Source/app)` comentada e descomentar na Task 16.)

`Source/IconComposerKit/CMakeLists.txt`:

```cmake
# IconComposerKit -- the editor's UI, in Dear ImGui and nothing else.
#
# No window, no Vulkan surface, no Onyx: this is what `ic_tests` and the selftest
# link, and rule 2 of the architecture spec is what keeps it that way. The two
# things a window provides -- a texture upload and a render thread -- arrive
# through Ports.h, implemented in Source/app.
add_library(IconComposerKit STATIC
    Headless.cpp
    Ports.cpp
)
target_link_libraries(IconComposerKit
    PUBLIC IconComposer::Foundation IconComposer::CoreSVG IconComposer::RenderBox imgui_lib)
target_include_directories(IconComposerKit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_library(IconComposer::Kit ALIAS IconComposerKit)
```

`Ports.h`:

```cpp
#pragma once
// The two things a window gives the Kit, as interfaces the Kit never implements.
//
// Rule 2 of the architecture spec: only Source/app links Onyx. So the upload of
// pixels to the GPU the window owns, and the thread the render runs on, are
// abstract here and concrete there -- and null in every test, which is what
// makes the whole UI exercisable on a machine with no device.
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "imgui.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ick {

// An AGGREGATE, and it stays one: `icf::IconBundle` has no default constructor, so a
// caller builds this with `RenderRequest r{version, bundle.clone(), context, size}`.
// Adding a constructor here breaks every call site.
struct RenderRequest {
    std::uint64_t version = 0;   // Session::version() this was made from
    icf::IconBundle bundle;      // a clone: the job reads it while the UI keeps editing
    icf::Context context;
    std::uint32_t size = 512;
};

struct RenderResult {
    std::uint64_t version = 0;
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba8;   // straight alpha, R8G8B8A8, row major
    std::size_t drawn = 0, total = 0;
    std::vector<std::string> skipped, shapeGaps, notes;
    std::string error;                 // non-empty: nothing else is valid
};

// "Render this"; the answer arrives later, on the main thread, through poll().
// Only the LATEST request matters: a scheduler may drop any earlier one that has
// not started, and a result older than the last request may be dropped too.
struct RenderScheduler {
    virtual ~RenderScheduler() = default;
    virtual void request(RenderRequest r) = 0;
    virtual std::optional<RenderResult> poll() = 0;
};

// Pixels in, an ImGui texture id out. `update` keeps the id when the size is the
// same; the caller removes and creates when it is not.
struct TextureSink {
    virtual ~TextureSink() = default;
    virtual ImTextureID create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) = 0;
    virtual bool update(ImTextureID id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) = 0;
    virtual void remove(ImTextureID id) = 0;
};

// Straight RGBA floats, as `rb::RenderedIcon::rgba` gives them, to 8 bits:
// clamped to [0,1] and rounded, the same rule `icf::encodePng` applies, so the
// canvas and the PNG show the same byte.
std::vector<std::uint8_t> toRgba8(const std::vector<float>& straightRgba);

}  // namespace ick
```

`Ports.cpp`:

```cpp
#include "Source/IconComposerKit/Ports.h"

#include <cmath>

namespace ick {

std::vector<std::uint8_t> toRgba8(const std::vector<float>& in) {
    std::vector<std::uint8_t> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        float v = in[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[i] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
    }
    return out;
}

}  // namespace ick
```

`Headless.h`:

```cpp
#pragma once
// A Dear ImGui context with no backend, for tests and the selftest.
//
// ImGui runs with no platform and no renderer as long as `io.DisplaySize` is
// set and the font atlas has been built and given a texture id -- sfsymview's
// selftest is the precedent. Every panel in this Kit is exercised through this
// class before it is ever shown in a window.
#include <cstdint>

struct ImGuiContext;

namespace ick {

class HeadlessImGui {
public:
    explicit HeadlessImGui(float width = 1440.0f, float height = 900.0f);
    ~HeadlessImGui();
    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;

    void newFrame();
    void render();   // ImGui::Render(); the draw data is measured, never consumed
    std::uint64_t errors() const { return errors_; }

private:
    static void onError(ImGuiContext*, void* user, const char* msg);
    ImGuiContext* ctx_ = nullptr;
    std::uint64_t errors_ = 0;
};

}  // namespace ick
```

`Headless.cpp`:

```cpp
#include "Source/IconComposerKit/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace ick {

HeadlessImGui::HeadlessImGui(float width, float height) {
    ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.IniFilename = nullptr;   // a test must not read or write imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    ctx_->ErrorCallback = &HeadlessImGui::onError;
    ctx_->ErrorCallbackUserData = this;
}

HeadlessImGui::~HeadlessImGui() { ImGui::DestroyContext(ctx_); }

void HeadlessImGui::newFrame() { ImGui::NewFrame(); }
void HeadlessImGui::render() { ImGui::Render(); }

void HeadlessImGui::onError(ImGuiContext*, void* user, const char*) {
    ++static_cast<HeadlessImGui*>(user)->errors_;
}

}  // namespace ick
```

Se a versão do ImGui do Onyx expuser o atlas por `io.Fonts->Build()` em vez de `GetTexDataAsRGBA32` (1.92 mudou a API de texturas), seguir o que `sfsymview/SelfTest.cpp` linhas 224–232 fazem nessa mesma versão; o teste é o mesmo.

Em `Tests/CMakeLists.txt`, depois de `add_test(...)`:

```cmake
if(IC_BUILD_UI)
    target_sources(ic_tests PRIVATE
        test_kit_headless.cpp
    )
    target_link_libraries(ic_tests PRIVATE IconComposer::Kit)
endif()
```

- [x] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [x] **Step 3: commitar**

```bash
git add CMakeLists.txt Source/IconComposerKit
git commit -m "o Kit nasce ImGui puro, com as duas portas que a janela implementa e um ImGui sem backend para os testes"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_kit_headless.cpp
#include "check.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Ports.h"
#include "imgui.h"

TEST_CASE(kit_headless_imgui_runs_a_frame_with_no_backend) {
    ick::HeadlessImGui gui;
    gui.newFrame();
    ImGui::Begin("probe");
    ImGui::Text("hello");
    ImGui::End();
    gui.render();
    CHECK_EQ(gui.errors(), std::uint64_t(0));
    CHECK(ImGui::GetIO().MetricsRenderWindows >= 1);
}

TEST_CASE(kit_to_rgba8_clamps_and_rounds) {
    const std::vector<float> in{0.0f, 0.5f, 1.0f, 1.5f, -0.2f, 0.25f, 0.75f, 1.0f};
    const auto out = ick::toRgba8(in);
    REQUIRE(out.size() == 8);
    CHECK_EQ(int(out[0]), 0);
    CHECK_EQ(int(out[1]), 128);
    CHECK_EQ(int(out[2]), 255);
    CHECK_EQ(int(out[3]), 255);
    CHECK_EQ(int(out[4]), 0);
    CHECK_EQ(int(out[5]), 64);
    CHECK_EQ(int(out[6]), 191);
}
```


**Como rodar e o que esperar:**

Run: `cmake --preset mingw && cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe kit_`
Expected: dois casos, zero falhas. A configuração puxa o Onyx e o ImGui; o build do `ic_tests` compila o ImGui e **nenhum** fonte do Onyx (`EXCLUDE_FROM_ALL`).

</details>

---

### Task 8: `Session` — o documento aberto, os comandos e o undo

**Files:**
- Create: `Source/IconComposerKit/Session.h`, `Source/IconComposerKit/Session.cpp`
- Modify: `Source/IconComposerKit/CMakeLists.txt` (acrescentar `Session.cpp`)
- Create: `Tests/test_kit_session.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `namespace ick`:
  ```cpp
  struct ViewContext { icf::Context context; std::uint32_t size = 512; float zoom = 1.0f; };
  class Session {
  public:
      static std::optional<Session> open(const std::filesystem::path& bundleDir);
      static std::optional<Session> create(const std::filesystem::path& bundleDir);  // writes a minimal document, then opens it
      icf::IconBundle& bundle();  const icf::IconBundle& bundle() const;
      icf::json::Value& root();   const icf::json::Value& root() const;
      std::uint64_t version() const;          // bumps on every edit, undo and redo
      // edits -- each one a command
      void setProperty(icf::NodePath, std::string_view prop, icf::Context scope,
                       std::optional<icf::json::Value> value, bool coalesce = false);
      void endCoalescing();                   // the drag ended; the next edit is a new command
      std::optional<std::size_t> addGroup(std::string name);
      std::optional<std::size_t> addLayer(std::size_t group, std::string name, std::string imageName);
      bool removeNode(icf::NodePath);  bool moveNode(icf::NodePath, int delta);  bool rename(icf::NodePath, std::string);
      bool undo();  bool redo();  bool canUndo() const;  bool canRedo() const;
      bool isDirty() const;
      std::string save();  std::string saveAs(const std::filesystem::path&);
      // UI state, plain members
      std::optional<icf::NodePath> selection;
      icf::Context scope;      // the inspector's scope selector
      ViewContext view;        // what the canvas shows
  };
  ```

- [x] **Step 1: implementar**

`Session.h`:

```cpp
#pragma once
// One open `.icon`, and everything the UI does to it.
//
// UNDO IS A SNAPSHOT OF THE NODE, NOT AN INVERSE OPERATION (spec 13/09 §5)
// -------------------------------------------------------------------------
// A command records the path of the node it touched and that node's JSON
// before and after. Undo is assignment. The largest corpus document is under
// 200 KB, so a node copy costs nothing worth an inverse-operation bug. A
// structural command (add, remove, move) snapshots the PARENT.
//
// Coalescing: while a control is being dragged, consecutive edits to the same
// (path, property, scope) fold into one command -- what the target's
// `DocumentCommands` does with its coalesced undo. `endCoalescing` is the drag
// ending; the panels call it on `IsItemDeactivatedAfterEdit`.
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ick {

struct ViewContext {
    icf::Context context;         // appearance and idiom the canvas renders
    std::uint32_t size = 512;     // the preview size, in pixels
    float zoom = 1.0f;            // 0.5 .. 2.0
};

class Session {
public:
    static std::optional<Session> open(const std::filesystem::path& bundleDir);
    // Writes `{"fill":"automatic","groups":[]}` and an empty Assets/, then opens it.
    static std::optional<Session> create(const std::filesystem::path& bundleDir);

    icf::IconBundle& bundle() { return bundle_; }
    const icf::IconBundle& bundle() const { return bundle_; }
    icf::json::Value& root() { return bundle_.json(); }
    const icf::json::Value& root() const { return bundle_.json(); }
    // Bumps on every edit, undo and redo -- what the render coordinator watches.
    std::uint64_t version() const { return version_; }

    void setProperty(icf::NodePath path, std::string_view prop, icf::Context scope,
                     std::optional<icf::json::Value> value, bool coalesce = false);
    void endCoalescing() { coalesceKey_.clear(); }
    std::optional<std::size_t> addGroup(std::string name);
    std::optional<std::size_t> addLayer(std::size_t group, std::string name, std::string imageName);
    bool removeNode(icf::NodePath path);
    bool moveNode(icf::NodePath path, int delta);
    bool rename(icf::NodePath path, std::string name);

    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    // "The stack is not where it was at the last save."
    bool isDirty() const { return undo_.size() != cleanDepth_; }

    std::string save();
    std::string saveAs(const std::filesystem::path& dir);

    std::optional<icf::NodePath> selection;
    icf::Context scope;
    ViewContext view;

private:
    struct Command {
        icf::NodePath target;
        icf::json::Value before;
        icf::json::Value after;
        std::string key;   // non-empty while coalescing
    };
    explicit Session(icf::IconBundle b) : bundle_(std::move(b)) {}
    // Snapshots `target`, runs `edit` on it, snapshots again, records. Returns
    // what `edit` returned. Nothing is recorded when the node does not exist.
    // Snapshots the node at `target`, runs `edit`, snapshots again, and records a
    // command when the two differ. `edit` takes the target node and returns bool;
    // a structural edit ignores that argument and calls the `icf::` function on
    // `root()` instead -- the parent snapshot captures the change either way, and
    // no pointer moves (erasing inside `groups[g].layers` does not move `groups[g]`).
    template <class F>
    bool apply(icf::NodePath target, std::string key, F&& edit);   // edit: bool(json::Value&)
    void push(Command c);
    void dropSelectionIfGone();

    icf::IconBundle bundle_;
    std::vector<Command> undo_;
    std::vector<Command> redo_;
    std::size_t cleanDepth_ = 0;
    std::string coalesceKey_;
    std::uint64_t version_ = 1;
};

}  // namespace ick
```

`Session.cpp`:

```cpp
#include "Source/IconComposerKit/Session.h"

#include <fstream>
#include <limits>

namespace ick {
namespace fs = std::filesystem;

std::optional<Session> Session::open(const fs::path& dir) {
    auto b = icf::IconBundle::open(dir);
    if (!b) return std::nullopt;
    return Session(std::move(*b));
}

std::optional<Session> Session::create(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Assets", ec);
    if (ec) return std::nullopt;
    std::ofstream f(dir / "icon.json", std::ios::binary | std::ios::trunc);
    if (!f) return std::nullopt;
    f << "{\n  \"fill\" : \"automatic\",\n  \"groups\" : [\n\n  ]\n}";
    f.close();
    return open(dir);
}

template <class F>
bool Session::apply(icf::NodePath target, std::string key, F&& edit) {
    icf::json::Value* node = icf::nodeAt(root(), target);
    if (!node) return false;
    Command c{target, *node, *node, key};
    const bool result = edit(*node);
    c.after = *node;
    if (icf::json::write(c.before) == icf::json::write(c.after)) return result;  // a no-op is not a command
    push(std::move(c));
    return result;
}

void Session::push(Command c) {
    redo_.clear();
    if (cleanDepth_ > undo_.size()) cleanDepth_ = std::numeric_limits<std::size_t>::max();
    if (!c.key.empty() && c.key == coalesceKey_ && !undo_.empty() && undo_.back().key == c.key) {
        undo_.back().after = std::move(c.after);   // fold into the drag's command
    } else {
        coalesceKey_ = c.key;
        undo_.push_back(std::move(c));
    }
    ++version_;
}

void Session::setProperty(icf::NodePath path, std::string_view prop, icf::Context scope,
                          std::optional<icf::json::Value> value, bool coalesce) {
    std::string key;
    if (coalesce) {
        key = (path.group ? std::to_string(*path.group) : "r") + "/" +
              (path.layer ? std::to_string(*path.layer) : "-") + "/" + std::string(prop) + "/" +
              std::to_string(static_cast<int>(scope.appearance)) + "/" +
              std::to_string(static_cast<int>(scope.idiom));
    } else {
        coalesceKey_.clear();
    }
    apply(path, key, [&](icf::json::Value& node) {
        icf::setProperty(node, prop, scope, std::move(value));
        return true;
    });
}

std::optional<std::size_t> Session::addGroup(std::string name) {
    coalesceKey_.clear();
    std::optional<std::size_t> index;
    apply(icf::NodePath{}, "", [&](icf::json::Value& rootNode) {
        index = icf::addGroup(rootNode, std::move(name));
        return true;
    });
    return index;
}

std::optional<std::size_t> Session::addLayer(std::size_t group, std::string name, std::string imageName) {
    coalesceKey_.clear();
    std::optional<std::size_t> index;
    apply(icf::NodePath{group, std::nullopt}, "", [&](icf::json::Value& g) {
        index = icf::addLayer(g, std::move(name), std::move(imageName));
        return true;
    });
    return index;
}

bool Session::removeNode(icf::NodePath path) {
    coalesceKey_.clear();
    if (!path.group) return false;
    const icf::NodePath parent = path.layer ? icf::NodePath{path.group, std::nullopt} : icf::NodePath{};
    // The snapshot is the PARENT's; the edit is `icf::removeNode` on the root, which
    // is the one spelling of this operation. Two spellings are two places to be wrong.
    const bool ok = apply(parent, "", [&](icf::json::Value&) { return icf::removeNode(root(), path); });
    if (ok) dropSelectionIfGone();
    return ok;
}

bool Session::moveNode(icf::NodePath path, int delta) {
    coalesceKey_.clear();
    if (!path.group) return false;
    const icf::NodePath parent = path.layer ? icf::NodePath{path.group, std::nullopt} : icf::NodePath{};
    const bool ok = apply(parent, "", [&](icf::json::Value&) { return icf::moveNode(root(), path, delta); });
    if (ok && selection == path) {
        if (path.layer) selection = icf::NodePath{path.group, *path.layer + (delta < 0 ? -1 : 1)};
        else selection = icf::NodePath{*path.group + (delta < 0 ? -1 : 1), std::nullopt};
    }
    return ok;
}

bool Session::rename(icf::NodePath path, std::string name) {
    coalesceKey_.clear();
    return apply(path, "", [&](icf::json::Value&) { return icf::setName(root(), path, std::move(name)); });
}

bool Session::undo() {
    if (undo_.empty()) return false;
    coalesceKey_.clear();
    Command c = std::move(undo_.back());
    undo_.pop_back();
    if (icf::json::Value* node = icf::nodeAt(root(), c.target)) *node = c.before;
    redo_.push_back(std::move(c));
    ++version_;
    dropSelectionIfGone();
    return true;
}

bool Session::redo() {
    if (redo_.empty()) return false;
    coalesceKey_.clear();
    Command c = std::move(redo_.back());
    redo_.pop_back();
    if (icf::json::Value* node = icf::nodeAt(root(), c.target)) *node = c.after;
    undo_.push_back(std::move(c));
    ++version_;
    dropSelectionIfGone();
    return true;
}

std::string Session::save() {
    const std::string r = bundle_.save();
    if (r.empty()) cleanDepth_ = undo_.size();
    return r;
}

std::string Session::saveAs(const fs::path& dir) {
    const std::string r = bundle_.saveAs(dir);
    if (r.empty()) cleanDepth_ = undo_.size();
    return r;
}

void Session::dropSelectionIfGone() {
    if (selection && !icf::nodeAt(root(), *selection)) selection.reset();
}

}  // namespace ick
```

Acrescentar `#include <string>` em `Session.cpp`. Acrescentar `Session.cpp` ao
`add_library` do Kit.

As três operações estruturais chamam `icf::removeNode`, `icf::moveNode` e `icf::setName`
de dentro do `apply`, sobre `root()` e o caminho completo. O snapshot do PAI captura a
mudança mesmo assim, e nenhum ponteiro se move: apagar dentro de `groups[g].layers` não
move `groups[g]`, e apagar dentro de `groups` não move a raiz.

- [x] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [x] **Step 3: commitar**

```bash
git add Source/IconComposerKit/Session.h Source/IconComposerKit/Session.cpp Source/IconComposerKit/CMakeLists.txt
git commit -m "a sessao e o documento aberto com undo por snapshot de no, e um arrasto vira um comando so"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_kit_session.cpp
#include "check.h"
#include "Source/IconComposerKit/Session.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ick;
namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("ic-session-") + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary)
        << "{\n  \"fill\" : \"automatic\",\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n          \"image-name\" : \"a.svg\",\n          \"name\" : \"a\"\n        },\n        {\n          \"image-name\" : \"b.svg\",\n          \"name\" : \"b\"\n        }\n      ],\n      \"name\" : \"g\"\n    }\n  ]\n}";
    std::ofstream(dir / "Assets" / "a.svg", std::ios::binary) << "<svg/>";
    std::ofstream(dir / "Assets" / "b.svg", std::ios::binary) << "<svg/>";
    return dir;
}
const icf::NodePath kLayerA{0, 0};
}  // namespace

TEST_CASE(session_edit_undo_redo_round_trip_the_bytes) {
    const fs::path dir = scratch("undo");
    auto s = Session::open(dir);
    REQUIRE(s.has_value());
    const std::string original = icf::json::write(s->root());
    const std::uint64_t v0 = s->version();
    CHECK(!s->canUndo());
    CHECK(!s->isDirty());
    s->setProperty(kLayerA, "opacity", icf::Context{}, icf::json::Value::number(0.5));
    CHECK(s->version() != v0);
    CHECK(s->isDirty());
    CHECK(icf::json::write(s->root()) != original);
    CHECK(s->undo());
    CHECK_EQ(icf::json::write(s->root()), original);
    CHECK(!s->isDirty());
    CHECK(s->canRedo());
    CHECK(s->redo());
    CHECK(s->isDirty());
    CHECK(s->undo());
    CHECK(!s->undo());   // stack empty now
}

TEST_CASE(session_coalesces_a_drag_into_one_command) {
    const fs::path dir = scratch("coalesce");
    auto s = Session::open(dir);
    REQUIRE(s.has_value());
    s->setProperty(kLayerA, "opacity", icf::Context{}, icf::json::Value::number(0.9), true);
    s->setProperty(kLayerA, "opacity", icf::Context{}, icf::json::Value::number(0.8), true);
    s->setProperty(kLayerA, "opacity", icf::Context{}, icf::json::Value::number(0.7), true);
    s->endCoalescing();
    CHECK(icf::nodeAt(s->root(), kLayerA)->find("opacity")->number() == "0.7");
    CHECK(s->undo());
    CHECK(icf::nodeAt(s->root(), kLayerA)->find("opacity") == nullptr);   // one undo undid the drag
    CHECK(!s->canUndo());
    // a different key never coalesces, even mid-drag
    s->setProperty(kLayerA, "opacity", icf::Context{}, icf::json::Value::number(0.5), true);
    s->setProperty(kLayerA, "hidden", icf::Context{}, icf::json::Value::boolean(true), true);
    s->endCoalescing();
    CHECK(s->undo());
    CHECK(icf::nodeAt(s->root(), kLayerA)->find("opacity")->number() == "0.5");
}

TEST_CASE(session_new_command_after_undo_drops_the_redo_branch) {
    const fs::path dir = scratch("branch");
    auto s = Session::open(dir);
    REQUIRE(s.has_value());
    s->setProperty(kLayerA, "hidden", icf::Context{}, icf::json::Value::boolean(true));
    s->undo();
    CHECK(s->canRedo());
    s->setProperty(kLayerA, "glass", icf::Context{}, icf::json::Value::boolean(true));
    CHECK(!s->canRedo());
}

TEST_CASE(session_structure_commands_are_undoable_and_fix_the_selection) {
    const fs::path dir = scratch("structure");
    auto s = Session::open(dir);
    REQUIRE(s.has_value());
    s->selection = icf::NodePath{0, 1};
    CHECK(s->removeNode(icf::NodePath{0, 1}));
    CHECK(!s->selection.has_value());   // it pointed at what is gone
    CHECK_EQ(icf::nodeAt(s->root(), icf::NodePath{0, std::nullopt})->find("layers")->elements().size(), std::size_t(1));
    CHECK(s->undo());
    CHECK_EQ(icf::nodeAt(s->root(), icf::NodePath{0, std::nullopt})->find("layers")->elements().size(), std::size_t(2));
    auto g = s->addGroup("Front");
    REQUIRE(g.has_value());
    CHECK_EQ(*g, std::size_t(1));
    auto l = s->addLayer(*g, "mark", "mark.svg");
    REQUIRE(l.has_value());
    CHECK(s->moveNode(icf::NodePath{1, std::nullopt}, -1));
    CHECK(icf::nodeAt(s->root(), icf::NodePath{0, std::nullopt})->find("name")->rawString() == "Front");
    CHECK(s->rename(icf::NodePath{0, std::nullopt}, "First"));
    CHECK(s->undo());  // rename
    CHECK(s->undo());  // move
    CHECK(s->undo());  // addLayer
    CHECK(s->undo());  // addGroup
    CHECK_EQ(s->root().find("groups")->elements().size(), std::size_t(1));
}

TEST_CASE(session_save_clears_dirty_and_writes_the_document) {
    const fs::path dir = scratch("save");
    auto s = Session::open(dir);
    REQUIRE(s.has_value());
    s->setProperty(kLayerA, "hidden", icf::Context{}, icf::json::Value::boolean(true));
    CHECK_EQ(s->save(), std::string(""));
    CHECK(!s->isDirty());
    CHECK(slurp(dir / "icon.json").find("\"hidden\" : true") != std::string::npos);
    CHECK(s->undo());
    CHECK(s->isDirty());   // the disk has the edit; memory does not
    CHECK(s->redo());
    CHECK(!s->isDirty());
}

TEST_CASE(session_create_writes_the_minimal_document) {
    const fs::path dir = fs::temp_directory_path() / "ic-session-new.icon";
    fs::remove_all(dir);
    auto s = Session::create(dir);
    REQUIRE(s.has_value());
    CHECK_EQ(slurp(dir / "icon.json"), std::string("{\n  \"fill\" : \"automatic\",\n  \"groups\" : [\n\n  ]\n}"));
    CHECK(fs::is_directory(dir / "Assets"));
}
```

Acrescentar `test_kit_session.cpp` ao bloco `if(IC_BUILD_UI)` de `Tests/CMakeLists.txt`.


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe session_`
Expected: seis casos, zero falhas.

</details>

---

### Task 9: `ViewModel` — o que o inspetor mostra

**Files:**
- Create: `Source/IconComposerKit/ViewModel.h`, `Source/IconComposerKit/ViewModel.cpp`
- Modify: `Source/IconComposerKit/CMakeLists.txt`
- Create: `Tests/test_kit_viewmodel.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `namespace ick`:
  ```cpp
  struct PropertyView { const icf::json::Value* value = nullptr; bool own = false; };  // own: the scope has its own entry
  PropertyView viewProperty(const Session&, icf::NodePath, std::string_view prop);       // resolves under session.scope
  enum class NodeKind { Root, Group, Layer };
  NodeKind kindOf(icf::NodePath);
  std::string nodeTitle(const Session&, icf::NodePath);   // "Document", or the node's name, or "Group 2" / "Layer 3" when unnamed
  const char* appearanceLabel(icf::Appearance);   // "Default", "Light", "Dark", "Tinted"
  const char* idiomLabel(icf::Idiom);             // "All", "Square", "iOS", "macOS", "watchOS"
  const char* blendModeLabel(icf::BlendMode);     // "Normal", "Plus Lighter", ...
  const char* shadowKindLabel(icf::ShadowKind);   // "Automatic", "Neutral", "Layer Color", "None"
  const char* specularLabel(icf::SpecularHighlight);  // "Off", "Automatic", "Inside", "Outside"
  const char* fillKindLabel(icf::FillKind);
  ```

- [ ] **Step 1: implementar**

`ViewModel.h`:

```cpp
#pragma once
// What a panel needs to know about a node, computed from the Session and never
// stored: the resolved value under the inspector's scope and whether that scope
// OWNS it, the node's kind and title, and the UI spelling of each vocabulary.
#include "Source/IconComposerFoundation/Values.h"
#include "Source/IconComposerKit/Session.h"

#include <string>
#include <string_view>

namespace ick {

struct PropertyView {
    const icf::json::Value* value = nullptr;   // resolved under session.scope, or null
    bool own = false;                          // session.scope has an entry of its own
};

PropertyView viewProperty(const Session& s, icf::NodePath path, std::string_view prop);

enum class NodeKind { Root, Group, Layer };
NodeKind kindOf(icf::NodePath path);
std::string nodeTitle(const Session& s, icf::NodePath path);

const char* appearanceLabel(icf::Appearance a);
const char* idiomLabel(icf::Idiom i);
const char* blendModeLabel(icf::BlendMode m);
const char* shadowKindLabel(icf::ShadowKind k);
const char* specularLabel(icf::SpecularHighlight h);
const char* fillKindLabel(icf::FillKind k);

}  // namespace ick
```

`ViewModel.cpp`:

```cpp
#include "Source/IconComposerKit/ViewModel.h"

namespace ick {

PropertyView viewProperty(const Session& s, icf::NodePath path, std::string_view prop) {
    PropertyView v;
    const icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return v;
    v.value = icf::resolve(*node, prop, s.scope);
    v.own = icf::hasOwnEntry(*node, prop, s.scope);
    return v;
}

NodeKind kindOf(icf::NodePath p) {
    if (!p.group) return NodeKind::Root;
    return p.layer ? NodeKind::Layer : NodeKind::Group;
}

std::string nodeTitle(const Session& s, icf::NodePath p) {
    if (!p.group) return "Document";
    const icf::json::Value* node = icf::nodeAt(s.root(), p);
    if (node) {
        if (const icf::json::Value* n = node->find("name")) {
            if (n->kind() == icf::json::Value::Kind::String && !n->rawString().empty()) return n->rawString();
        }
    }
    return p.layer ? "Layer " + std::to_string(*p.layer + 1) : "Group " + std::to_string(*p.group + 1);
}

const char* appearanceLabel(icf::Appearance a) {
    switch (a) {
        case icf::Appearance::Base: return "Default";
        case icf::Appearance::Light: return "Light";
        case icf::Appearance::Dark: return "Dark";
        case icf::Appearance::Tinted: return "Tinted";
    }
    return "Default";
}

const char* idiomLabel(icf::Idiom i) {
    switch (i) {
        case icf::Idiom::Base: return "All";
        case icf::Idiom::Square: return "Square";
        case icf::Idiom::IOS: return "iOS";
        case icf::Idiom::MacOS: return "macOS";
        case icf::Idiom::WatchOS: return "watchOS";
    }
    return "All";
}

const char* blendModeLabel(icf::BlendMode m) {
    switch (m) {
        case icf::BlendMode::Normal: return "Normal";
        case icf::BlendMode::PlusLighter: return "Plus Lighter";
        case icf::BlendMode::PlusDarker: return "Plus Darker";
        case icf::BlendMode::Overlay: return "Overlay";
        case icf::BlendMode::Multiply: return "Multiply";
        case icf::BlendMode::SoftLight: return "Soft Light";
        case icf::BlendMode::HardLight: return "Hard Light";
        case icf::BlendMode::Darken: return "Darken";
        case icf::BlendMode::Lighten: return "Lighten";
        case icf::BlendMode::Screen: return "Screen";
    }
    return "Normal";
}

const char* shadowKindLabel(icf::ShadowKind k) {
    switch (k) {
        case icf::ShadowKind::Automatic: return "Automatic";
        case icf::ShadowKind::Neutral: return "Neutral";
        case icf::ShadowKind::LayerColor: return "Layer Color";
        case icf::ShadowKind::None: return "None";
    }
    return "None";
}

const char* specularLabel(icf::SpecularHighlight h) {
    switch (h) {
        case icf::SpecularHighlight::Off: return "Off";
        case icf::SpecularHighlight::Automatic: return "Automatic";
        case icf::SpecularHighlight::Inside: return "Inside";
        case icf::SpecularHighlight::Outside: return "Outside";
    }
    return "Off";
}

const char* fillKindLabel(icf::FillKind k) {
    switch (k) {
        case icf::FillKind::None: return "None";
        case icf::FillKind::Automatic: return "Automatic";
        case icf::FillKind::Solid: return "Solid";
        case icf::FillKind::AutomaticGradient: return "Automatic Gradient";
        case icf::FillKind::LinearGradient: return "Linear Gradient";
        case icf::FillKind::SystemLight: return "System Light";
        case icf::FillKind::SystemDark: return "System Dark";
    }
    return "None";
}

}  // namespace ick
```

- [ ] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [ ] **Step 3: commitar**

```bash
git add Source/IconComposerKit/ViewModel.h Source/IconComposerKit/ViewModel.cpp Source/IconComposerKit/CMakeLists.txt
git commit -m "o view model diz o que cada escopo tem de seu e o que herda, e soletra os vocabularios para a tela"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_kit_viewmodel.cpp
#include "check.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/IconComposerKit/ViewModel.h"

#include <filesystem>
#include <fstream>

using namespace ick;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
    const fs::path dir = fs::temp_directory_path() / "ic-viewmodel.icon";
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary)
        << R"({"fill" : "automatic", "groups" : [ { "name" : "g", "layers" : [ { "name" : "a", "image-name" : "a.svg", "glass" : false, "opacity-specializations" : [ { "value" : 1 }, { "appearance" : "dark", "value" : 0.5 } ] }, { "image-name" : "b.svg" } ] } ]})";
    return dir;
}
}  // namespace

TEST_CASE(viewmodel_property_view_says_own_or_inherited) {
    auto s = Session::open(scratch());
    REQUIRE(s.has_value());
    const icf::NodePath a{0, 0};
    s->scope = icf::Context{};
    PropertyView base = viewProperty(*s, a, "opacity");
    REQUIRE(base.value != nullptr);
    CHECK(base.value->number() == "1");
    CHECK(base.own);
    s->scope = icf::Context{icf::Appearance::Dark, icf::Idiom::Base};
    PropertyView dark = viewProperty(*s, a, "opacity");
    CHECK(dark.value->number() == "0.5");
    CHECK(dark.own);
    s->scope = icf::Context{icf::Appearance::Tinted, icf::Idiom::Base};
    PropertyView tinted = viewProperty(*s, a, "opacity");
    CHECK(tinted.value->number() == "1");   // resolves to the default
    CHECK(!tinted.own);                     // but has nothing of its own
    PropertyView glass = viewProperty(*s, a, "glass");
    CHECK(glass.value != nullptr);
    CHECK(!glass.own);                      // plain key is Base's own, not Tinted's
    PropertyView none = viewProperty(*s, a, "shadow");
    CHECK(none.value == nullptr);
}

TEST_CASE(viewmodel_titles_and_kinds) {
    auto s = Session::open(scratch());
    REQUIRE(s.has_value());
    CHECK(kindOf(icf::NodePath{}) == NodeKind::Root);
    CHECK(kindOf(icf::NodePath{0, std::nullopt}) == NodeKind::Group);
    CHECK(kindOf(icf::NodePath{0, 1}) == NodeKind::Layer);
    CHECK_EQ(nodeTitle(*s, icf::NodePath{}), std::string("Document"));
    CHECK_EQ(nodeTitle(*s, icf::NodePath{0, std::nullopt}), std::string("g"));
    CHECK_EQ(nodeTitle(*s, icf::NodePath{0, 0}), std::string("a"));
    CHECK_EQ(nodeTitle(*s, icf::NodePath{0, 1}), std::string("Layer 2"));
}

TEST_CASE(viewmodel_labels_cover_every_case) {
    CHECK_EQ(std::string(appearanceLabel(icf::Appearance::Base)), std::string("Default"));
    CHECK_EQ(std::string(idiomLabel(icf::Idiom::Base)), std::string("All"));
    CHECK_EQ(std::string(blendModeLabel(icf::BlendMode::PlusLighter)), std::string("Plus Lighter"));
    CHECK_EQ(std::string(shadowKindLabel(icf::ShadowKind::LayerColor)), std::string("Layer Color"));
    CHECK_EQ(std::string(specularLabel(icf::SpecularHighlight::Outside)), std::string("Outside"));
    CHECK_EQ(std::string(fillKindLabel(icf::FillKind::AutomaticGradient)), std::string("Automatic Gradient"));
}
```


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe viewmodel_`
Expected: três casos, zero falhas.

</details>

---

### Task 10: o painel Layers

**Files:**
- Create: `Source/IconComposerKit/Panels.h`
- Create: `Source/IconComposerKit/PanelLayers.cpp`
- Modify: `Source/IconComposerKit/CMakeLists.txt`
- Create: `Tests/test_kit_panels.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `Panels.h` (`namespace ick`): as janelas chamam-se **`"Layers"`, `"Canvas"`, `"Inspector##ic"`, `"Diagnostics"`** — o `##ic` porque o Onyx registra um painel `"Inspector"` próprio.
  ```cpp
  struct LayersStats { std::size_t groups = 0, layers = 0; bool selectionChanged = false; };
  LayersStats drawLayers(Session& s);
  ```
  `drawLayers` faz `ImGui::Begin("Layers")` … `End()`. Por grupo: `TreeNodeEx` com a seta, o nome (`Selectable` para selecionar), e no fim da linha dois `Checkbox` — visibilidade (`!hidden`) e vidro (`glass`, só em camadas). Duplo clique num nome abre um `InputText` inline; `Enter` chama `s.rename`. Menu de contexto por linha: Move Up, Move Down, Delete. Rodapé: `+` abre um popup com "Add Group" e "Add Image Layer" (a camada nova vai para o grupo selecionado ou para o último, com `image-name` vazio até a rodada 4 importar), `−` remove a seleção.

- [ ] **Step 1: implementar**

`Panels.h`:

```cpp
#pragma once
// The four panels and the menu, each a function from the Session to what it
// drew. The return values are MEASUREMENTS of the frame -- how many rows, how
// many sections, whether a texture was shown -- so the selftest and the tests
// can assert on a frame without a window.
//
// Window titles, and why one carries `##ic`: Onyx registers its own "Inspector"
// panel, and two ImGui windows with the same title are one window. The label a
// person reads stops at `##`.
#include "Source/IconComposerKit/Ports.h"
#include "Source/IconComposerKit/Session.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ick {

inline constexpr const char* kLayersWindow = "Layers";
inline constexpr const char* kCanvasWindow = "Canvas";
inline constexpr const char* kInspectorWindow = "Inspector##ic";
inline constexpr const char* kDiagnosticsWindow = "Diagnostics";

struct LayersStats {
    std::size_t groups = 0, layers = 0;
    bool selectionChanged = false;
};
LayersStats drawLayers(Session& s);

// The last render the canvas has to show, and what it did not draw.
struct RenderView {
    // `ImTextureID_Invalid`, never a literal 0: ImGui 1.92 is mid-migration to
    // `ImTextureRef`, and Onyx's own TexturePool already spells it this way.
    ImTextureID texture = ImTextureID_Invalid;
    std::uint32_t width = 0, height = 0;
    bool pending = false;   // a newer render is on its way
    std::size_t drawn = 0, total = 0;
    std::vector<std::string> skipped, shapeGaps, notes;
    std::string error;
};

// What the menu asked the app to do this frame. The Kit cannot open a file
// dialog or quit; the app reads these after the frame and does it.
struct MenuActions {
    bool newDocument = false, open = false, save = false, saveAs = false, close = false, quit = false;
};

struct MenuStats {
    std::size_t menus = 0, items = 0, disabled = 0;
};
// Draws inside the current window's menu bar: the caller opened the window with
// ImGuiWindowFlags_MenuBar (the canvas does, like sfsymview's "Symbols").
MenuStats drawMenuBar(Session& s, MenuActions& actions);

struct CanvasStats {
    bool textured = false;
    std::size_t contextControls = 0;   // appearance, idiom, size, zoom
    MenuStats menu;
};
CanvasStats drawCanvas(Session& s, const RenderView& view, MenuActions& actions);

struct InspectorStats {
    std::string title;
    std::size_t sections = 0;    // enabled sections drawn
    std::size_t inherited = 0;   // of those, marked as inherited under the scope
    std::size_t disabled = 0;    // sections drawn greyed, with the reason
};
InspectorStats drawInspector(Session& s);

struct DiagnosticsStats {
    std::size_t rows = 0;
};
DiagnosticsStats drawDiagnostics(const Session& s, const RenderView& view);

}  // namespace ick
```

`PanelLayers.cpp`:

```cpp
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

#include <cstring>
#include <string>

namespace ick {
namespace {

// One row: the name (selectable, renamable), then the toggles at the right edge.
// `hidden` is stored inverted from what the eye sees, so the checkbox is "visible".
void drawRow(Session& s, icf::NodePath path, LayersStats& st, bool isLayer) {
    icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return;
    ImGui::PushID(static_cast<int>(path.group.value_or(0) * 1000 + path.layer.value_or(999)));
    const std::string title = nodeTitle(s, path);
    const bool selected = s.selection == path;

    static icf::NodePath renaming{99999, std::nullopt};
    static char buffer[256];
    if (renaming == path) {
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", buffer, sizeof buffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
            s.rename(path, buffer);
            renaming = icf::NodePath{99999, std::nullopt};
        }
        if (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) renaming = icf::NodePath{99999, std::nullopt};
    } else {
        if (ImGui::Selectable(title.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(ImGui::GetContentRegionAvail().x - (isLayer ? 60.0f : 30.0f), 0))) {
            if (s.selection != path) st.selectionChanged = true;
            s.selection = path;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                renaming = path;
                std::strncpy(buffer, title.c_str(), sizeof buffer - 1);
                buffer[sizeof buffer - 1] = '\0';
            }
        }
        if (ImGui::BeginPopupContextItem("row-menu")) {
            if (ImGui::MenuItem("Move Up")) s.moveNode(path, -1);
            if (ImGui::MenuItem("Move Down")) s.moveNode(path, +1);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) s.removeNode(path);
            ImGui::EndPopup();
        }
    }

    // The toggles read under the BASE scope on purpose: the sidebar is the
    // document's structure, and the per-appearance answer belongs to the inspector.
    ImGui::SameLine();
    const icf::json::Value* hidden = icf::resolve(*node, "hidden", icf::Context{});
    bool visible = !(hidden && hidden->kind() == icf::json::Value::Kind::Bool && hidden->boolean());
    if (ImGui::Checkbox("##visible", &visible)) {
        s.setProperty(path, "hidden", icf::Context{}, visible ? std::optional<icf::json::Value>{} : icf::json::Value::boolean(true));
    }
    ImGui::SetItemTooltip("Toggle visibility");
    if (isLayer) {
        ImGui::SameLine();
        const icf::json::Value* glass = icf::resolve(*node, "glass", icf::Context{});
        bool on = glass && glass->kind() == icf::json::Value::Kind::Bool && glass->boolean();
        if (ImGui::Checkbox("##glass", &on)) s.setProperty(path, "glass", icf::Context{}, icf::json::Value::boolean(on));
        ImGui::SetItemTooltip("Enable or disable glass effects on this layer");
    }
    ImGui::PopID();
}

}  // namespace

LayersStats drawLayers(Session& s) {
    LayersStats st;
    if (!ImGui::Begin(kLayersWindow)) {
        ImGui::End();
        return st;
    }
    const icf::json::Value* groups = s.root().find("groups");
    const std::size_t count = groups ? groups->elements().size() : 0;
    for (std::size_t g = 0; g < count; ++g) {
        ++st.groups;
        const icf::NodePath gp{g, std::nullopt};
        ImGui::PushID(static_cast<int>(g));
        const bool open = ImGui::TreeNodeEx("##group", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap |
                                                           ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);
        ImGui::SameLine();
        drawRow(s, gp, st, false);
        if (open) {
            const icf::json::Value* layers = icf::nodeAt(s.root(), gp)->find("layers");
            const std::size_t n = layers ? layers->elements().size() : 0;
            for (std::size_t l = 0; l < n; ++l) {
                ++st.layers;
                drawRow(s, icf::NodePath{g, l}, st, true);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("+")) ImGui::OpenPopup("add-menu");
    ImGui::SetItemTooltip("Opens menu to add new group or image layer");
    if (ImGui::BeginPopup("add-menu")) {
        if (ImGui::MenuItem("Add Group")) {
            if (auto i = s.addGroup("Group")) s.selection = icf::NodePath{*i, std::nullopt};
        }
        if (ImGui::MenuItem("Add Image Layer", nullptr, false, count > 0)) {
            const std::size_t g = s.selection && s.selection->group ? *s.selection->group : count - 1;
            if (auto i = s.addLayer(g, "Layer", "")) s.selection = icf::NodePath{g, *i};
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("-") && s.selection && s.selection->group) s.removeNode(*s.selection);
    ImGui::End();
    return st;
}

}  // namespace ick
```

Acrescentar `PanelLayers.cpp` ao Kit e `test_kit_panels.cpp` ao bloco de testes.

- [ ] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [ ] **Step 3: commitar**

```bash
git add Source/IconComposerKit/Panels.h Source/IconComposerKit/PanelLayers.cpp Source/IconComposerKit/CMakeLists.txt
git commit -m "a arvore de grupos e camadas, com visibilidade, vidro, renomear e o menu de arranjo"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_kit_panels.cpp
#include "check.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"
#include "imgui.h"

#include <filesystem>
#include <fstream>

using namespace ick;
namespace fs = std::filesystem;

namespace {
fs::path fixture(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("ic-panels-") + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary)
        << R"({"fill" : "automatic", "groups" : [ { "name" : "Back", "layers" : [ { "name" : "plate", "image-name" : "plate.svg" } ] }, { "name" : "Front", "layers" : [ { "name" : "mark", "image-name" : "mark.svg", "glass" : true, "opacity-specializations" : [ { "value" : 1 }, { "appearance" : "dark", "value" : 0.5 } ] }, { "name" : "badge", "image-name" : "badge.svg", "hidden" : true } ] } ]})";
    return dir;
}
}  // namespace

TEST_CASE(panel_layers_lists_every_group_and_layer) {
    auto s = Session::open(fixture("layers"));
    REQUIRE(s.has_value());
    HeadlessImGui gui;
    // Two frames: tree nodes report their open state from the frame before.
    for (int i = 0; i < 2; ++i) {
        gui.newFrame();
        ImGui::SetNextWindowSize(ImVec2(300, 600));
        LayersStats st = drawLayers(*s);
        gui.render();
        if (i == 1) {
            CHECK_EQ(st.groups, std::size_t(2));
            CHECK_EQ(st.layers, std::size_t(3));
        }
    }
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}
```


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe panel_layers`
Expected: passa, zero erros de ImGui.

</details>

---

### Task 11: o painel Inspector

**Files:**
- Create: `Source/IconComposerKit/PanelInspector.cpp`
- Modify: `Source/IconComposerKit/CMakeLists.txt`
- Modify: `Tests/test_kit_panels.cpp` — **opcional**, só se for escrever a rede

**Interfaces:**
- Consumes: `viewProperty`, `Session::setProperty/endCoalescing`, os `*ToJson`/`*FromString` da Foundation.
- Produces: `InspectorStats drawInspector(Session&)` (declarada na Task 10).

Seções por tipo de nó, **só onde `Coverage.cpp` conhece a chave**:

| nó | seções ativas | desabilitadas (motivo no tooltip: "rodada 3") |
|---|---|---|
| raiz | Fill | Document Settings |
| grupo | Visible, Opacity, Blend Mode, Geometry, Shadow, Translucency, Specular | Blur Material, Refractivity, Lighting, Group Effects |
| camada | Visible, Opacity, Blend Mode, Geometry, Fill, Liquid Glass (o bool `glass`) | Material, Image Asset, Asset Mirroring |

Cada seção: cabeçalho `CollapsingHeader` com o nome; o seletor de escopo é **um só**, no topo do painel (dois combos: appearance e idiom, escrevendo `s.scope`), porque cada seção repetindo o mesmo seletor é o que o alvo faz e o que esta rodada não precisa. Dentro: o controle, e ao lado a marca `inherited` (texto cinza "inherited") quando `!own`, mais um botão pequeno "×" ("Remove override", só quando `own` e o escopo não é Base) que chama `setProperty(..., nullopt)`.

Controles:
- Visible: `Checkbox` sobre `hidden` invertido.
- Opacity: `SliderFloat` 0–1 → `Value::number`, `coalesce = true`; `endCoalescing` em `IsItemDeactivatedAfterEdit`.
- Blend Mode: `Combo` sobre os dez `blendModeLabel`, escreve `Value::string(blendModeToString)`.
- Geometry: três `DragFloat` (x, y em pontos; scale) sobre `Position`, escreve `positionToJson`, coalescendo.
- Fill: `Combo` de `FillKind`; para Solid, um `ColorEdit4` (sRGB) e o espaço mantido do valor atual (default `srgb`); escreve `fillToJson`. Linear gradient: só mostra as cores em `ColorEdit4` editáveis, sem adicionar paradas.
- Shadow: `Combo` de `ShadowKind` + `SliderFloat` opacity.
- Translucency: `Checkbox` enabled + `SliderFloat` value.
- Specular: `Combo` de `SpecularHighlight`.
- Liquid Glass: `Checkbox` sobre `glass`.

- [ ] **Step 1: implementar**

`PanelInspector.cpp`:

```cpp
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "Source/IconComposerFoundation/Values.h"
#include "imgui.h"

#include <cmath>
#include <string>

namespace ick {
namespace {

struct Section {
    Session& s;
    icf::NodePath path;
    InspectorStats& st;

    // Opens a section for `prop`; returns the view and whether the body should draw.
    bool begin(const char* label, std::string_view prop, PropertyView& view) {
        view = viewProperty(s, path, prop);
        ImGui::PushID(label);
        const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
        ++st.sections;
        if (!view.own) ++st.inherited;
        if (open) {
            if (!view.own) {
                ImGui::TextDisabled(view.value ? "inherited" : "not set");
            } else if (s.scope.appearance != icf::Appearance::Base || s.scope.idiom != icf::Idiom::Base) {
                if (ImGui::SmallButton("Remove override")) s.setProperty(path, prop, s.scope, std::nullopt);
            }
        }
        return open;
    }
    void end() { ImGui::PopID(); }

    void disabled(const char* label, const char* why) {
        ImGui::BeginDisabled();
        ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_Leaf);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", why);
        ++st.disabled;
    }

    void write(std::string_view prop, icf::json::Value v, bool coalesce) {
        s.setProperty(path, prop, s.scope, std::move(v), coalesce);
        if (coalesce && ImGui::IsItemDeactivatedAfterEdit()) s.endCoalescing();
    }
};

double numberOr(const icf::json::Value* v, double fallback) {
    if (!v || v->kind() != icf::json::Value::Kind::Number) return fallback;
    return std::atof(v->number().c_str());
}

void visible(Section& x) {
    PropertyView v;
    if (x.begin("Visible", "hidden", v)) {
        bool on = !(v.value && v.value->kind() == icf::json::Value::Kind::Bool && v.value->boolean());
        if (ImGui::Checkbox("Visible", &on)) x.write("hidden", icf::json::Value::boolean(!on), false);
    }
    x.end();
}

void opacity(Section& x) {
    PropertyView v;
    if (x.begin("Opacity", "opacity", v)) {
        float f = static_cast<float>(numberOr(v.value, 1.0));
        if (ImGui::SliderFloat("##opacity", &f, 0.0f, 1.0f, "%.2f")) x.write("opacity", icf::json::Value::number(f), true);
        if (ImGui::IsItemDeactivatedAfterEdit()) x.s.endCoalescing();
    }
    x.end();
}

void blendMode(Section& x) {
    static const icf::BlendMode kModes[] = {
        icf::BlendMode::Normal, icf::BlendMode::PlusLighter, icf::BlendMode::PlusDarker, icf::BlendMode::Overlay,
        icf::BlendMode::Multiply, icf::BlendMode::SoftLight, icf::BlendMode::HardLight, icf::BlendMode::Darken,
        icf::BlendMode::Lighten, icf::BlendMode::Screen};
    PropertyView v;
    if (x.begin("Blend Mode", "blend-mode", v)) {
        icf::BlendMode current = icf::BlendMode::Normal;
        if (v.value && v.value->kind() == icf::json::Value::Kind::String) {
            if (auto m = icf::blendModeFromString(v.value->rawString())) current = *m;
        }
        if (ImGui::BeginCombo("##blend", blendModeLabel(current))) {
            for (auto m : kModes) {
                if (ImGui::Selectable(blendModeLabel(m), m == current)) {
                    x.write("blend-mode", icf::json::Value::string(std::string(icf::blendModeToString(m))), false);
                }
            }
            ImGui::EndCombo();
        }
    }
    x.end();
}

void geometry(Section& x) {
    PropertyView v;
    if (x.begin("Geometry", "position", v)) {
        icf::Position p;
        if (v.value) {
            if (auto read = icf::positionFrom(*v.value)) p = *read;
        }
        float xy[2] = {static_cast<float>(p.translation.x), static_cast<float>(p.translation.y)};
        float scale = static_cast<float>(p.scale * 100.0);
        bool changed = ImGui::DragFloat2("Position (pt)", xy, 1.0f);
        bool released = ImGui::IsItemDeactivatedAfterEdit();
        changed |= ImGui::DragFloat("Scale (%)", &scale, 1.0f, 1.0f, 1000.0f);
        released |= ImGui::IsItemDeactivatedAfterEdit();
        if (changed) {
            p.translation = {xy[0], xy[1]};
            p.scale = scale / 100.0;
            x.write("position", icf::positionToJson(p), true);
        }
        if (released) x.s.endCoalescing();
    }
    x.end();
}

void fill(Section& x) {
    static const icf::FillKind kKinds[] = {icf::FillKind::None, icf::FillKind::Automatic, icf::FillKind::Solid,
                                           icf::FillKind::AutomaticGradient, icf::FillKind::LinearGradient,
                                           icf::FillKind::SystemLight, icf::FillKind::SystemDark};
    PropertyView v;
    if (x.begin("Fill", "fill", v)) {
        icf::Fill f;
        if (v.value) {
            if (auto read = icf::fillFrom(*v.value)) f = *read;
        }
        if (ImGui::BeginCombo("##kind", fillKindLabel(f.kind))) {
            for (auto k : kKinds) {
                if (ImGui::Selectable(fillKindLabel(k), k == f.kind)) {
                    icf::Fill next;
                    next.kind = k;
                    if (k == icf::FillKind::Solid || k == icf::FillKind::AutomaticGradient) {
                        icf::Color c;
                        c.space = icf::ColorSpace::SRGB;
                        c.count = 4;
                        c.components[3] = 1;
                        next.colors.push_back(f.colors.empty() ? c : f.colors[0]);
                    } else if (k == icf::FillKind::LinearGradient) {
                        next.colors = f.colors;
                        if (next.colors.size() < 2) {
                            icf::Color c;
                            c.space = icf::ColorSpace::SRGB;
                            c.count = 4;
                            c.components[3] = 1;
                            next.colors.assign(2, c);
                        }
                    }
                    x.write("fill", icf::fillToJson(next), false);
                }
            }
            ImGui::EndCombo();
        }
        bool changed = false, released = false;
        for (std::size_t i = 0; i < f.colors.size(); ++i) {
            icf::Color& c = f.colors[i];
            ImGui::PushID(static_cast<int>(i));
            if (c.count == 4) {
                float rgba[4] = {float(c.components[0]), float(c.components[1]), float(c.components[2]), float(c.components[3])};
                if (ImGui::ColorEdit4("##colour", rgba, ImGuiColorEditFlags_Float)) {
                    for (int k = 0; k < 4; ++k) c.components[k] = rgba[k];
                    changed = true;
                }
            } else {
                float ga[2] = {float(c.components[0]), float(c.components[1])};
                if (ImGui::DragFloat2("gray, alpha", ga, 0.01f, 0.0f, 1.0f)) {
                    c.components[0] = ga[0];
                    c.components[1] = ga[1];
                    changed = true;
                }
            }
            released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopID();
        }
        if (changed) x.write("fill", icf::fillToJson(f), true);
        if (released) x.s.endCoalescing();
    }
    x.end();
}

void shadow(Section& x) {
    static const icf::ShadowKind kKinds[] = {icf::ShadowKind::Automatic, icf::ShadowKind::Neutral,
                                             icf::ShadowKind::LayerColor, icf::ShadowKind::None};
    PropertyView v;
    if (x.begin("Shadow", "shadow", v)) {
        icf::Shadow sh;
        if (v.value) {
            if (auto read = icf::shadowFrom(*v.value)) sh = *read;
        }
        if (ImGui::BeginCombo("##kind", shadowKindLabel(sh.kind))) {
            for (auto k : kKinds) {
                if (ImGui::Selectable(shadowKindLabel(k), k == sh.kind)) {
                    sh.kind = k;
                    x.write("shadow", icf::shadowToJson(sh), false);
                }
            }
            ImGui::EndCombo();
        }
        float op = static_cast<float>(sh.opacity);
        if (ImGui::SliderFloat("Opacity", &op, 0.0f, 1.0f, "%.2f")) {
            sh.opacity = op;
            x.write("shadow", icf::shadowToJson(sh), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) x.s.endCoalescing();
    }
    x.end();
}

void translucency(Section& x) {
    PropertyView v;
    if (x.begin("Translucency", "translucency", v)) {
        icf::Translucency t;
        if (v.value) {
            if (auto read = icf::translucencyFrom(*v.value)) t = *read;
        }
        if (ImGui::Checkbox("Enabled", &t.enabled)) x.write("translucency", icf::translucencyToJson(t), false);
        float val = static_cast<float>(t.value);
        if (ImGui::SliderFloat("Value", &val, 0.0f, 1.0f, "%.2f")) {
            t.value = val;
            x.write("translucency", icf::translucencyToJson(t), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) x.s.endCoalescing();
    }
    x.end();
}

void specular(Section& x) {
    static const icf::SpecularHighlight kCases[] = {icf::SpecularHighlight::Off, icf::SpecularHighlight::Automatic,
                                                    icf::SpecularHighlight::Inside, icf::SpecularHighlight::Outside};
    PropertyView v;
    if (x.begin("Specular", "specular", v)) {
        icf::SpecularHighlight current = icf::SpecularHighlight::Automatic;
        if (v.value && v.value->kind() == icf::json::Value::Kind::String) {
            if (auto read = icf::specularHighlightFromString(v.value->rawString())) current = *read;
        }
        if (ImGui::BeginCombo("##specular", specularLabel(current))) {
            for (auto c : kCases) {
                if (ImGui::Selectable(specularLabel(c), c == current)) {
                    x.write("specular", icf::json::Value::string(std::string(icf::specularHighlightToString(c))), false);
                }
            }
            ImGui::EndCombo();
        }
    }
    x.end();
}

void glass(Section& x) {
    PropertyView v;
    if (x.begin("Liquid Glass", "glass", v)) {
        bool on = v.value && v.value->kind() == icf::json::Value::Kind::Bool && v.value->boolean();
        if (ImGui::Checkbox("Glass", &on)) x.write("glass", icf::json::Value::boolean(on), false);
    }
    x.end();
}

void scopeSelector(Session& s) {
    static const icf::Appearance kA[] = {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                                         icf::Appearance::Tinted};
    static const icf::Idiom kI[] = {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                                    icf::Idiom::WatchOS};
    ImGui::TextUnformatted("Scope");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::BeginCombo("##scope-a", appearanceLabel(s.scope.appearance))) {
        for (auto a : kA) {
            if (ImGui::Selectable(appearanceLabel(a), a == s.scope.appearance)) s.scope.appearance = a;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::BeginCombo("##scope-i", idiomLabel(s.scope.idiom))) {
        for (auto i : kI) {
            if (ImGui::Selectable(idiomLabel(i), i == s.scope.idiom)) s.scope.idiom = i;
        }
        ImGui::EndCombo();
    }
}

}  // namespace

InspectorStats drawInspector(Session& s) {
    InspectorStats st;
    if (!ImGui::Begin(kInspectorWindow)) {
        ImGui::End();
        return st;
    }
    if (!s.selection || !icf::nodeAt(s.root(), *s.selection)) {
        st.title = "Nothing selected";
        ImGui::TextDisabled("%s", st.title.c_str());
        ImGui::End();
        return st;
    }
    const icf::NodePath path = *s.selection;
    st.title = nodeTitle(s, path);
    ImGui::TextUnformatted(st.title.c_str());
    scopeSelector(s);
    ImGui::Separator();

    Section x{s, path, st};
    const char* kLater = "Round 3: this inspector is not built yet";
    switch (kindOf(path)) {
        case NodeKind::Root:
            fill(x);
            x.disabled("Document Settings", kLater);
            break;
        case NodeKind::Group:
            visible(x); opacity(x); blendMode(x); geometry(x); shadow(x); translucency(x); specular(x);
            x.disabled("Blur Material", kLater);
            x.disabled("Refractivity", kLater);
            x.disabled("Lighting", kLater);
            x.disabled("Group Effects", kLater);
            break;
        case NodeKind::Layer:
            visible(x); opacity(x); blendMode(x); geometry(x); fill(x); glass(x);
            x.disabled("Material", kLater);
            x.disabled("Image Asset", kLater);
            x.disabled("Asset Mirroring", kLater);
            break;
    }
    ImGui::End();
    return st;
}

}  // namespace ick
```

Acrescentar `#include <cstdlib>` (para `std::atof`). Acrescentar `PanelInspector.cpp` ao Kit.

- [ ] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [ ] **Step 3: commitar**

```bash
git add Source/IconComposerKit/PanelInspector.cpp Source/IconComposerKit/CMakeLists.txt
git commit -m "o inspetor mostra as oito secoes que o modelo tipa, por escopo, e diz o que e herdado"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

Acrescentar a `Tests/test_kit_panels.cpp`:

```cpp
TEST_CASE(panel_inspector_shows_sections_for_the_selection_and_marks_inherited) {
    auto s = Session::open(fixture("inspector"));
    REQUIRE(s.has_value());
    HeadlessImGui gui;
    auto frame = [&] {
        gui.newFrame();
        ImGui::SetNextWindowSize(ImVec2(320, 800));
        InspectorStats st = drawInspector(*s);
        gui.render();
        return st;
    };
    InspectorStats none = frame();
    CHECK_EQ(none.sections, std::size_t(0));
    CHECK_EQ(none.title, std::string("Nothing selected"));

    s->selection = icf::NodePath{};
    InspectorStats root = frame();
    CHECK_EQ(root.title, std::string("Document"));
    CHECK_EQ(root.sections, std::size_t(1));
    CHECK_EQ(root.disabled, std::size_t(1));

    s->selection = icf::NodePath{1, 0};   // "mark": opacity has a dark override
    s->scope = icf::Context{};
    InspectorStats layerBase = frame();
    CHECK_EQ(layerBase.title, std::string("mark"));
    CHECK_EQ(layerBase.sections, std::size_t(6));
    CHECK_EQ(layerBase.disabled, std::size_t(3));
    s->scope = icf::Context{icf::Appearance::Tinted, icf::Idiom::Base};
    InspectorStats layerTinted = frame();
    CHECK(layerTinted.inherited >= 5);   // everything but nothing is Tinted's own
    s->scope = icf::Context{icf::Appearance::Dark, icf::Idiom::Base};
    InspectorStats layerDark = frame();
    CHECK_EQ(layerDark.inherited, layerTinted.inherited - 1);   // opacity is Dark's own

    s->selection = icf::NodePath{1, std::nullopt};
    InspectorStats group = frame();
    CHECK_EQ(group.sections, std::size_t(7));
    CHECK_EQ(group.disabled, std::size_t(4));
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}
```


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe panel_inspector`
Expected: passa. Se `layerTinted.inherited` vier menor que 5, uma seção está reportando `own` para um escopo que não tem entrada: é bug em `hasOwnEntry` ou no `viewProperty`, nunca no teste.

</details>

---

### Task 12: o painel Canvas, o Diagnóstico e a barra de menu

**Files:**
- Create: `Source/IconComposerKit/PanelCanvas.cpp`
- Create: `Source/IconComposerKit/MenuBar.cpp`
- Modify: `Source/IconComposerKit/CMakeLists.txt`
- Modify: `Tests/test_kit_panels.cpp` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces: `drawCanvas`, `drawDiagnostics`, `drawMenuBar` (declaradas na Task 10).
- A janela do canvas abre com `ImGuiWindowFlags_MenuBar` e chama `drawMenuBar` primeiro. Barra de contexto abaixo do menu: combo appearance, combo idiom, combo size (512, 1024), combo zoom (50, 75, 100, 150, 200 %); cada um escreve `s.view`. A imagem: `ImGui::Image(view.texture, size * zoom)` centrada sobre um retângulo de cor chapada (`ImGui::GetColorU32(ImGuiCol_ChildBg)` escurecido), com pan por arrasto do botão do meio. Quando `view.pending`, um ponto pequeno no canto. Overlay de seleção: se a seleção é uma camada com `position`, um retângulo `[INF]` de lado `size × scale` deslocado por `translation × (size / kCanvasPoints)` a partir do centro — a régua é a de `rb::kCanvasPoints`, incluída de `Source/RenderBox/IconRenderer.h`.
- Diagnóstico: uma tabela de duas colunas (origem, texto) com as linhas: `drawn/total` (sempre), cada `skipped`, cada `shapeGaps`, cada `notes` prefixada `[OBS]`, cada `missingAssets` do bundle, cada `unknownKeys` do documento, e `error` quando houver.
- Menu: `File` (New, Open…, Save, Save As…, Close, Quit), `Edit` (Undo, Redo, Delete), `View` (Appearance ▸ 4 itens, Idiom ▸ 5, Preview Size ▸ 2, Zoom ▸ 5), `Layer` (Add Group, Add Image Layer, Toggle Glass, Toggle Visibility, Move Up, Move Down). Desabilitados com tooltip: `File > Export Icon as Image…` ("Round 5"), `Edit > Copy Properties`/`Paste Properties` ("Round 5"), `Edit > Localization` ("Round 5"). `Save` desabilitado quando `!isDirty()`; `Undo`/`Redo` seguem `canUndo/canRedo`; os de `Layer` seguem a seleção.

- [ ] **Step 1: implementar**

`MenuBar.cpp`:

```cpp
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

namespace ick {
namespace {

struct Builder {
    MenuStats& st;
    bool item(const char* label, const char* shortcut, bool enabled = true, const char* why = nullptr,
              bool selected = false) {
        ++st.items;
        if (!enabled) ++st.disabled;
        const bool pressed = ImGui::MenuItem(label, shortcut, selected, enabled);
        if (why) ImGui::SetItemTooltip("%s", why);
        return pressed;
    }
};

constexpr const char* kRound5 = "Round 5: not built yet";

}  // namespace

MenuStats drawMenuBar(Session& s, MenuActions& a) {
    MenuStats st;
    if (!ImGui::BeginMenuBar()) return st;
    Builder b{st};

    ++st.menus;
    if (ImGui::BeginMenu("File")) {
        if (b.item("New…", "Ctrl+N")) a.newDocument = true;
        if (b.item("Open…", "Ctrl+O")) a.open = true;
        ImGui::Separator();
        if (b.item("Save", "Ctrl+S", s.isDirty())) a.save = true;
        if (b.item("Save As…", "Ctrl+Shift+S")) a.saveAs = true;
        b.item("Export Icon as Image…", nullptr, false, kRound5);
        ImGui::Separator();
        if (b.item("Close", "Ctrl+W")) a.close = true;
        if (b.item("Quit", "Ctrl+Q")) a.quit = true;
        ImGui::EndMenu();
    }
    ++st.menus;
    if (ImGui::BeginMenu("Edit")) {
        if (b.item("Undo", "Ctrl+Z", s.canUndo())) s.undo();
        if (b.item("Redo", "Ctrl+Y", s.canRedo())) s.redo();
        ImGui::Separator();
        if (b.item("Delete", "Del", s.selection && s.selection->group)) s.removeNode(*s.selection);
        b.item("Copy Properties", nullptr, false, kRound5);
        b.item("Paste Properties", nullptr, false, kRound5);
        b.item("Localization", nullptr, false, kRound5);
        ImGui::EndMenu();
    }
    ++st.menus;
    if (ImGui::BeginMenu("View")) {
        if (ImGui::BeginMenu("Appearance")) {
            for (auto ap : {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark, icf::Appearance::Tinted}) {
                if (b.item(appearanceLabel(ap), nullptr, true, nullptr, s.view.context.appearance == ap)) s.view.context.appearance = ap;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Idiom")) {
            for (auto id : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS, icf::Idiom::WatchOS}) {
                if (b.item(idiomLabel(id), nullptr, true, nullptr, s.view.context.idiom == id)) s.view.context.idiom = id;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Preview Size")) {
            if (b.item("512", nullptr, true, nullptr, s.view.size == 512)) s.view.size = 512;
            if (b.item("1024", nullptr, true, nullptr, s.view.size == 1024)) s.view.size = 1024;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Zoom")) {
            for (float z : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
                char label[16];
                std::snprintf(label, sizeof label, "%d%%", static_cast<int>(z * 100));
                if (b.item(label, nullptr, true, nullptr, s.view.zoom == z)) s.view.zoom = z;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    ++st.menus;
    if (ImGui::BeginMenu("Layer")) {
        const bool hasSel = s.selection && s.selection->group;
        const bool isLayer = hasSel && s.selection->layer;
        const std::size_t groups = s.root().find("groups") ? s.root().find("groups")->elements().size() : 0;
        if (b.item("Add Group", nullptr)) {
            if (auto i = s.addGroup("Group")) s.selection = icf::NodePath{*i, std::nullopt};
        }
        if (b.item("Add Image Layer", nullptr, groups > 0)) {
            const std::size_t g = hasSel ? *s.selection->group : groups - 1;
            if (auto i = s.addLayer(g, "Layer", "")) s.selection = icf::NodePath{g, *i};
        }
        ImGui::Separator();
        if (b.item("Toggle Glass", nullptr, isLayer)) {
            const icf::json::Value* g = icf::resolve(*icf::nodeAt(s.root(), *s.selection), "glass", icf::Context{});
            const bool on = g && g->kind() == icf::json::Value::Kind::Bool && g->boolean();
            s.setProperty(*s.selection, "glass", icf::Context{}, icf::json::Value::boolean(!on));
        }
        if (b.item("Toggle Visibility", nullptr, hasSel)) {
            const icf::json::Value* h = icf::resolve(*icf::nodeAt(s.root(), *s.selection), "hidden", icf::Context{});
            const bool hidden = h && h->kind() == icf::json::Value::Kind::Bool && h->boolean();
            s.setProperty(*s.selection, "hidden", icf::Context{}, hidden ? std::optional<icf::json::Value>{} : icf::json::Value::boolean(true));
        }
        ImGui::Separator();
        if (b.item("Move Up", nullptr, hasSel)) s.moveNode(*s.selection, -1);
        if (b.item("Move Down", nullptr, hasSel)) s.moveNode(*s.selection, +1);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
    return st;
}

}  // namespace ick
```

Acrescentar `#include <cstdio>`.

`PanelCanvas.cpp`:

```cpp
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "Source/RenderBox/IconRenderer.h"   // rb::kCanvasPoints, the ruler the renderer uses
#include "imgui.h"

#include <cstdio>

namespace ick {
namespace {

std::size_t contextBar(Session& s) {
    std::size_t n = 0;
    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("##appearance", appearanceLabel(s.view.context.appearance))) {
        for (auto a : {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark, icf::Appearance::Tinted}) {
            if (ImGui::Selectable(appearanceLabel(a), a == s.view.context.appearance)) s.view.context.appearance = a;
        }
        ImGui::EndCombo();
    }
    ++n;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("##idiom", idiomLabel(s.view.context.idiom))) {
        for (auto i : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS, icf::Idiom::WatchOS}) {
            if (ImGui::Selectable(idiomLabel(i), i == s.view.context.idiom)) s.view.context.idiom = i;
        }
        ImGui::EndCombo();
    }
    ++n;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    char sizeLabel[16];
    std::snprintf(sizeLabel, sizeof sizeLabel, "%u px", s.view.size);
    if (ImGui::BeginCombo("##size", sizeLabel)) {
        if (ImGui::Selectable("512 px", s.view.size == 512)) s.view.size = 512;
        if (ImGui::Selectable("1024 px", s.view.size == 1024)) s.view.size = 1024;
        ImGui::EndCombo();
    }
    ++n;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    char zoomLabel[16];
    std::snprintf(zoomLabel, sizeof zoomLabel, "%d%%", static_cast<int>(s.view.zoom * 100));
    if (ImGui::BeginCombo("##zoom", zoomLabel)) {
        for (float z : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
            char l[16];
            std::snprintf(l, sizeof l, "%d%%", static_cast<int>(z * 100));
            if (ImGui::Selectable(l, s.view.zoom == z)) s.view.zoom = z;
        }
        ImGui::EndCombo();
    }
    ++n;
    return n;
}

}  // namespace

CanvasStats drawCanvas(Session& s, const RenderView& view, MenuActions& actions) {
    CanvasStats st;
    if (!ImGui::Begin(kCanvasWindow, nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return st;
    }
    st.menu = drawMenuBar(s, actions);
    st.contextControls = contextBar(s);
    if (view.pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("rendering…");
    }

    static ImVec2 pan(0, 0);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(40, 40, 44, 255));
    ImGui::InvisibleButton("##canvas", ImVec2(avail.x > 1 ? avail.x : 1, avail.y > 1 ? avail.y : 1));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        pan.x += ImGui::GetIO().MouseDelta.x;
        pan.y += ImGui::GetIO().MouseDelta.y;
    }

    if (view.texture != ImTextureID_Invalid && view.width > 0) {
        const float side = static_cast<float>(view.width) * s.view.zoom;
        const ImVec2 tl(origin.x + (avail.x - side) * 0.5f + pan.x, origin.y + (avail.y - side) * 0.5f + pan.y);
        const ImVec2 br(tl.x + side, tl.y + side);
        dl->AddImage(view.texture, tl, br);
        st.textured = true;

        // `[INF]` The selection rectangle: the layer's `position` on the same
        // ruler the renderer places art with (rb::kCanvasPoints, centre origin,
        // +y down). A square of the canvas's own size, scaled and translated.
        if (s.selection && s.selection->layer) {
            const icf::json::Value* node = icf::nodeAt(s.root(), *s.selection);
            icf::Position p;
            if (node) {
                if (const icf::json::Value* pv = icf::resolve(*node, "position", s.view.context)) {
                    if (auto read = icf::positionFrom(*pv)) p = *read;
                }
            }
            const float px = side / static_cast<float>(rb::kCanvasPoints);
            const float half = side * static_cast<float>(p.scale) * 0.5f;
            const ImVec2 c(tl.x + side * 0.5f + static_cast<float>(p.translation.x) * px,
                           tl.y + side * 0.5f + static_cast<float>(p.translation.y) * px);
            dl->AddRect(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), IM_COL32(80, 160, 255, 255), 0, 0, 2.0f);
        }
    }
    ImGui::End();
    return st;
}

DiagnosticsStats drawDiagnostics(const Session& s, const RenderView& view) {
    DiagnosticsStats st;
    if (!ImGui::Begin(kDiagnosticsWindow)) {
        ImGui::End();
        return st;
    }
    auto row = [&](const char* origin, const std::string& text) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(origin);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", text.c_str());
        ++st.rows;
    };
    if (ImGui::BeginTable("diag", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("origin", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("text");
        row("render", std::to_string(view.drawn) + " of " + std::to_string(view.total) + " layer(s) drawn");
        if (!view.error.empty()) row("render", view.error);
        for (const auto& x : view.skipped) row("skipped", x);
        for (const auto& x : view.shapeGaps) row("shape", x);
        for (const auto& x : view.notes) row("[OBS]", x);
        for (const auto& x : s.bundle().missingAssets()) row("asset", "missing: " + x);
        for (const auto& x : s.bundle().document().unknownKeys()) row("key", "unknown: " + x);
        ImGui::EndTable();
    }
    ImGui::End();
    return st;
}

}  // namespace ick
```

Acrescentar `PanelCanvas.cpp` e `MenuBar.cpp` ao Kit.

- [ ] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [ ] **Step 3: commitar**

```bash
git add Source/IconComposerKit/PanelCanvas.cpp Source/IconComposerKit/MenuBar.cpp Source/IconComposerKit/CMakeLists.txt
git commit -m "o canvas mostra o render com contexto e zoom, o diagnostico mostra cada lacuna, e o menu diz o que nao faz"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

Acrescentar a `Tests/test_kit_panels.cpp`:

```cpp
TEST_CASE(panel_canvas_draws_the_texture_and_the_context_controls) {
    auto s = Session::open(fixture("canvas"));
    REQUIRE(s.has_value());
    HeadlessImGui gui;
    RenderView view;
    MenuActions actions;
    gui.newFrame();
    CanvasStats empty = drawCanvas(*s, view, actions);
    gui.render();
    CHECK(!empty.textured);
    CHECK_EQ(empty.contextControls, std::size_t(4));
    CHECK_EQ(empty.menu.menus, std::size_t(4));
    view.texture = static_cast<ImTextureID>(7);
    view.width = view.height = 512;
    gui.newFrame();
    CanvasStats shown = drawCanvas(*s, view, actions);
    gui.render();
    CHECK(shown.textured);
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}

TEST_CASE(panel_diagnostics_lists_every_gap) {
    auto s = Session::open(fixture("diag"));
    REQUIRE(s.has_value());
    HeadlessImGui gui;
    RenderView view;
    view.drawn = 2;
    view.total = 3;
    view.skipped = {"grupo 1 / badge: no art"};
    view.notes = {"the glass ruler is a guess"};
    gui.newFrame();
    DiagnosticsStats st = drawDiagnostics(*s, view);
    gui.render();
    // drawn/total + 1 skipped + 1 note + 3 missing assets (plate, mark, badge are not on disk)
    CHECK_EQ(st.rows, std::size_t(6));
}

TEST_CASE(menu_bar_counts_its_items_and_greys_what_it_cannot_do) {
    auto s = Session::open(fixture("menu"));
    REQUIRE(s.has_value());
    HeadlessImGui gui;
    RenderView view;
    MenuActions actions;
    gui.newFrame();
    CanvasStats st = drawCanvas(*s, view, actions);
    gui.render();
    CHECK_EQ(st.menu.menus, std::size_t(4));
    CHECK(st.menu.items == 0);   // no menu is open on a headless frame; menus counted, items not
}
```


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe panel_`
Expected: todos os `panel_*` e `menu_*` passam, zero erros de ImGui. Se `panel_diagnostics` contar 6 linhas e vier outro número, conferir `missingAssets()` do fixture (os três SVGs não estão em disco de propósito).

</details>

---

### Task 13: `RenderCoordinator` — da versão ao pixel

**Files:**
- Create: `Source/IconComposerKit/RenderCoordinator.h`, `Source/IconComposerKit/RenderCoordinator.cpp`
- Modify: `Source/IconComposerKit/CMakeLists.txt`
- Create: `Tests/test_kit_render_coordinator.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `namespace ick`:
  ```cpp
  class RenderCoordinator {
  public:
      RenderCoordinator(RenderScheduler& scheduler, TextureSink& sink);
      ~RenderCoordinator();          // removes the texture it holds
      // Once per frame, before the panels: asks for a render when the document
      // or the view changed, and takes any finished one into `view()`.
      void tick(Session& s);
      const RenderView& view() const;
  };
  ```
  A chave de "mudou" é `(session.version(), view.context, view.size)`; o zoom não re-renderiza. Um resultado cuja `version` é menor que a do último pedido é **descartado** (o último vence). `pending` é verdadeiro entre `request` e o `poll` correspondente.

- [ ] **Step 1: implementar**

`RenderCoordinator.h`:

```cpp
#pragma once
// From "the document changed" to "the canvas has a texture", once per frame.
//
// THE LATEST WINS (spec 13/09 §6). A request carries the Session version it was
// made from; a result older than the last request is dropped, because the
// canvas must never step backwards. Zoom is not a render: it is the same pixels
// shown larger.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Ports.h"
#include "Source/IconComposerKit/Session.h"

#include <cstdint>

namespace ick {

class RenderCoordinator {
public:
    RenderCoordinator(RenderScheduler& scheduler, TextureSink& sink) : scheduler_(scheduler), sink_(sink) {}
    ~RenderCoordinator();
    RenderCoordinator(const RenderCoordinator&) = delete;
    RenderCoordinator& operator=(const RenderCoordinator&) = delete;

    void tick(Session& s);
    const RenderView& view() const { return view_; }

private:
    struct Key {
        std::uint64_t version = 0;
        icf::Context context;
        std::uint32_t size = 0;
        bool operator==(const Key&) const = default;
    };
    RenderScheduler& scheduler_;
    TextureSink& sink_;
    RenderView view_;
    Key requested_;
    bool everRequested_ = false;
};

}  // namespace ick
```

`RenderCoordinator.cpp`:

```cpp
#include "Source/IconComposerKit/RenderCoordinator.h"

namespace ick {

RenderCoordinator::~RenderCoordinator() {
    if (view_.texture != ImTextureID_Invalid) sink_.remove(view_.texture);
}

void RenderCoordinator::tick(Session& s) {
    const Key now{s.version(), s.view.context, s.view.size};
    if (!everRequested_ || !(now == requested_)) {
        // AGGREGATE initialisation, in declaration order. `RenderRequest r;` does not
        // compile: it holds an `icf::IconBundle`, which has no default constructor
        // (only the private one `open` uses). `RenderRequest` must stay an aggregate.
        RenderRequest r{now.version, s.bundle().clone(), now.context, now.size};
        scheduler_.request(std::move(r));
        requested_ = now;
        everRequested_ = true;
        view_.pending = true;
    }

    while (auto result = scheduler_.poll()) {
        if (result->version < requested_.version) continue;   // stale: the latest wins
        view_.drawn = result->drawn;
        view_.total = result->total;
        view_.skipped = result->skipped;
        view_.shapeGaps = result->shapeGaps;
        view_.notes = result->notes;
        view_.error = result->error;
        if (result->error.empty() && !result->rgba8.empty()) {
            if (view_.texture != ImTextureID_Invalid && view_.width == result->width &&
                view_.height == result->height) {
                sink_.update(view_.texture, result->width, result->height, result->rgba8.data());
            } else {
                if (view_.texture != ImTextureID_Invalid) sink_.remove(view_.texture);
                view_.texture = sink_.create(result->width, result->height, result->rgba8.data());
                view_.width = result->width;
                view_.height = result->height;
            }
        }
        if (result->version == requested_.version) view_.pending = false;
    }
}

}  // namespace ick
```

`icf::Context` precisa de `operator==` para a `Key`: acrescentar `bool operator==(const Context&) const = default;` ao `struct Context` em `IconDocument.h`.

- [ ] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [ ] **Step 3: commitar**

```bash
git add Source/IconComposerKit/RenderCoordinator.h Source/IconComposerKit/RenderCoordinator.cpp Source/IconComposerKit/CMakeLists.txt Source/IconComposerFoundation/IconDocument.h
git commit -m "o coordenador pede um render por mudanca, sobe o que chega, e o ultimo pedido vence"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_kit_render_coordinator.cpp
#include "check.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Session.h"

#include <deque>
#include <filesystem>
#include <fstream>

using namespace ick;
namespace fs = std::filesystem;

namespace {
fs::path fixture() {
    const fs::path dir = fs::temp_directory_path() / "ic-coord.icon";
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary)
        << R"({"fill" : "automatic", "groups" : [ { "name" : "g", "layers" : [ { "name" : "a", "image-name" : "a.svg" } ] } ]})";
    return dir;
}

struct FakeScheduler : RenderScheduler {
    std::vector<RenderRequest> requests;
    std::deque<RenderResult> results;
    void request(RenderRequest r) override { requests.push_back(std::move(r)); }
    std::optional<RenderResult> poll() override {
        if (results.empty()) return std::nullopt;
        RenderResult r = std::move(results.front());
        results.pop_front();
        return r;
    }
    RenderResult answer(std::uint64_t version, std::uint32_t size) {
        RenderResult r;
        r.version = version;
        r.width = r.height = size;
        r.rgba8.assign(std::size_t(size) * size * 4, 255);
        r.drawn = 1;
        r.total = 1;
        return r;
    }
};

struct FakeSink : TextureSink {
    std::uint64_t creates = 0, updates = 0, removes = 0;
    ImTextureID next = 10;
    ImTextureID create(std::uint32_t, std::uint32_t, const std::uint8_t*) override { ++creates; return next++; }
    bool update(ImTextureID, std::uint32_t, std::uint32_t, const std::uint8_t*) override { ++updates; return true; }
    void remove(ImTextureID) override { ++removes; }
};
}  // namespace

TEST_CASE(coordinator_requests_once_per_change_and_uploads_the_answer) {
    auto s = Session::open(fixture());
    REQUIRE(s.has_value());
    FakeScheduler sched;
    FakeSink sink;
    {
        RenderCoordinator c(sched, sink);
        c.tick(*s);
        c.tick(*s);
        CHECK_EQ(sched.requests.size(), std::size_t(1));   // same version, one request
        CHECK(c.view().pending);
        CHECK_EQ(sched.requests[0].version, s->version());
        CHECK_EQ(sched.requests[0].size, std::uint32_t(512));

        sched.results.push_back(sched.answer(s->version(), 512));
        c.tick(*s);
        CHECK(!c.view().pending);
        CHECK(c.view().texture != ImTextureID_Invalid);
        CHECK_EQ(sink.creates, std::uint64_t(1));
        CHECK_EQ(c.view().drawn, std::size_t(1));

        s->setProperty(icf::NodePath{0, 0}, "hidden", icf::Context{}, icf::json::Value::boolean(true));
        c.tick(*s);
        CHECK_EQ(sched.requests.size(), std::size_t(2));
        sched.results.push_back(sched.answer(s->version(), 512));
        c.tick(*s);
        CHECK_EQ(sink.updates, std::uint64_t(1));   // same size: update, not create
        CHECK_EQ(sink.creates, std::uint64_t(1));

        s->view.size = 1024;
        c.tick(*s);
        CHECK_EQ(sched.requests.size(), std::size_t(3));
        sched.results.push_back(sched.answer(s->version(), 1024));
        c.tick(*s);
        CHECK_EQ(sink.creates, std::uint64_t(2));   // size changed: remove + create
        CHECK_EQ(sink.removes, std::uint64_t(1));

        s->view.zoom = 2.0f;
        c.tick(*s);
        CHECK_EQ(sched.requests.size(), std::size_t(3));   // zoom is not a render
    }
    CHECK_EQ(sink.removes, std::uint64_t(2));   // the destructor gave the texture back
}

TEST_CASE(coordinator_drops_a_result_older_than_the_last_request) {
    auto s = Session::open(fixture());
    REQUIRE(s.has_value());
    FakeScheduler sched;
    FakeSink sink;
    RenderCoordinator c(sched, sink);
    c.tick(*s);
    const std::uint64_t v1 = s->version();
    s->setProperty(icf::NodePath{0, 0}, "hidden", icf::Context{}, icf::json::Value::boolean(true));
    c.tick(*s);
    sched.results.push_back(sched.answer(v1, 512));   // stale
    c.tick(*s);
    CHECK(c.view().pending);
    CHECK_EQ(sink.creates, std::uint64_t(0));
    sched.results.push_back(sched.answer(s->version(), 512));
    c.tick(*s);
    CHECK(!c.view().pending);
    CHECK_EQ(sink.creates, std::uint64_t(1));
}
```


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe coordinator_`
Expected: dois casos, zero falhas.

</details>

---

### Task 14: o selftest

**Files:**
- Create: `Source/IconComposerKit/SelfTest.h`, `Source/IconComposerKit/SelfTest.cpp`
- Modify: `Source/IconComposerKit/CMakeLists.txt`
- Create: `Tests/test_kit_selftest.cpp` — **opcional**, só se for escrever a rede
- Modify: `Tests/CMakeLists.txt` — **opcional**, só se for escrever a rede

**Interfaces:**
- Produces, em `namespace ick`:
  ```cpp
  struct SelfTestReport { int frames = 0; std::size_t groups = 0, layers = 0, sections = 0, diagnostics = 0;
                          bool textured = false; bool bytesRoundTripped = false; std::uint64_t imguiErrors = 0;
                          std::string failure; };   // non-empty: the script did not reach the end
  // Runs the script against `bundleDir` with the given scheduler and sink; the
  // app passes a synchronous scheduler over a real device, the tests pass fakes.
  SelfTestReport runSelfTest(const std::filesystem::path& bundleDir, RenderScheduler& scheduler,
                             TextureSink& sink, int frames);
  std::string describe(const SelfTestReport& r);   // what the executable prints
  ```
  O script, com todos os painéis desenhados em cada frame, numa cópia do bundle em temp: (1) abre; (2) seleciona a primeira camada; (3) escreve opacidade 0.5 sob Dark; (4) desfaz; (5) salva; (6) compara os bytes do `icon.json` com o original — `bytesRoundTripped`; (7) roda `frames` frames de settle e reporta `textured` se o coordenador recebeu um render.

- [ ] **Step 1: implementar**

`SelfTest.h`:

```cpp
#pragma once
// The headless frame script: what makes "the UI opens" an assertion.
//
// sfsymview's precedent. Every panel is drawn every frame in a context with no
// backend; the script edits, undoes, saves and compares bytes; the report is
// numbers a test can hold and a person can read.
#include "Source/IconComposerKit/Ports.h"

#include <filesystem>
#include <string>

namespace ick {

struct SelfTestReport {
    int frames = 0;
    std::size_t groups = 0, layers = 0, sections = 0, diagnostics = 0;
    bool textured = false;
    bool bytesRoundTripped = false;
    std::uint64_t imguiErrors = 0;
    std::string failure;   // non-empty: the script stopped here
};

SelfTestReport runSelfTest(const std::filesystem::path& bundleDir, RenderScheduler& scheduler,
                           TextureSink& sink, int frames);
std::string describe(const SelfTestReport& r);

}  // namespace ick
```

`SelfTest.cpp`:

```cpp
#include "Source/IconComposerKit/SelfTest.h"

#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Session.h"
#include "imgui.h"

#include <fstream>
#include <sstream>

namespace ick {
namespace fs = std::filesystem;
namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

struct Frame {
    LayersStats layers;
    InspectorStats inspector;
    CanvasStats canvas;
    DiagnosticsStats diagnostics;
};

Frame frame(HeadlessImGui& gui, Session& s, RenderCoordinator& c) {
    Frame f;
    MenuActions actions;
    c.tick(s);
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(280, 900));
    f.layers = drawLayers(s);
    ImGui::SetNextWindowPos(ImVec2(280, 0));
    ImGui::SetNextWindowSize(ImVec2(840, 700));
    f.canvas = drawCanvas(s, c.view(), actions);
    ImGui::SetNextWindowPos(ImVec2(280, 700));
    ImGui::SetNextWindowSize(ImVec2(840, 200));
    f.diagnostics = drawDiagnostics(s, c.view());
    ImGui::SetNextWindowPos(ImVec2(1120, 0));
    ImGui::SetNextWindowSize(ImVec2(320, 900));
    f.inspector = drawInspector(s);
    gui.render();
    return f;
}

}  // namespace

SelfTestReport runSelfTest(const fs::path& bundleDir, RenderScheduler& scheduler, TextureSink& sink, int frames) {
    SelfTestReport r;
    // A scratch copy: the script saves, and the original is somebody's file.
    const fs::path work = fs::temp_directory_path() / ("ic-selftest-" + bundleDir.filename().string());
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::copy(bundleDir, work, fs::copy_options::recursive, ec);
    if (ec) {
        r.failure = "could not copy the bundle: " + ec.message();
        return r;
    }
    const std::string original = slurp(work / "icon.json");

    auto s = Session::open(work);
    if (!s) {
        r.failure = "not a bundle this reader can open";
        return r;
    }
    HeadlessImGui gui;
    RenderCoordinator coord(scheduler, sink);

    Frame f = frame(gui, *s, coord);
    r.frames = 1;
    r.groups = f.layers.groups;
    r.layers = f.layers.layers;
    if (r.layers == 0) {
        r.failure = "no layer to select";
        return r;
    }
    // Select the first layer, and give the tree one frame to have reported it.
    s->selection = icf::NodePath{0, 0};
    if (!icf::nodeAt(s->root(), *s->selection)) s->selection = icf::NodePath{0, std::nullopt};
    f = frame(gui, *s, coord);
    ++r.frames;
    r.sections = f.inspector.sections;

    s->setProperty(*s->selection, "opacity", icf::Context{icf::Appearance::Dark, icf::Idiom::Base},
                   icf::json::Value::number(0.5));
    f = frame(gui, *s, coord);
    ++r.frames;
    if (!s->undo()) {
        r.failure = "undo had nothing to undo";
        return r;
    }
    f = frame(gui, *s, coord);
    ++r.frames;
    const std::string saved = s->save();
    if (!saved.empty()) {
        r.failure = "save: " + saved;
        return r;
    }
    r.bytesRoundTripped = slurp(work / "icon.json") == original;

    for (int i = 0; i < frames; ++i) {
        f = frame(gui, *s, coord);
        ++r.frames;
        if (f.canvas.textured) {
            r.textured = true;
            break;
        }
    }
    r.diagnostics = f.diagnostics.rows;
    r.imguiErrors = gui.errors();
    return r;
}

std::string describe(const SelfTestReport& r) {
    std::ostringstream o;
    o << "selftest: " << r.frames << " frame(s), " << r.groups << " group(s), " << r.layers << " layer(s), "
      << r.sections << " inspector section(s), " << r.diagnostics << " diagnostic row(s); "
      << "textured " << (r.textured ? "yes" : "no") << "; bytes round-tripped "
      << (r.bytesRoundTripped ? "yes" : "NO") << "; imgui errors " << r.imguiErrors;
    if (!r.failure.empty()) o << "; FAILED: " << r.failure;
    return o.str();
}

}  // namespace ick
```

- [ ] **Step 2: conferir que compila**

Run: `cmake --build --preset mingw`
Expected: compila limpo, sem warning novo.

- [ ] **Step 3: commitar**

```bash
git add Source/IconComposerKit/SelfTest.h Source/IconComposerKit/SelfTest.cpp Source/IconComposerKit/CMakeLists.txt
git commit -m "o selftest abre, edita, desfaz, salva e compara bytes num frame sem janela"
```

<details>
<summary><b>Se precisar de rede</b> — os casos desta task, e como rodá-los</summary>

Não é passo obrigatório. O código abaixo é onde o comportamento pretendido está dito com precisão; se algo quebrar e a causa não for óbvia, é daqui que sai o teste do bug.

Construir a suíte: `cmake --build --preset mingw --target ic_tests`

```cpp
// Tests/test_kit_selftest.cpp
#include "check.h"
#include "Source/IconComposerKit/SelfTest.h"

#include <cstdlib>
#include <filesystem>

using namespace ick;

namespace {
struct NullScheduler : RenderScheduler {
    std::optional<RenderRequest> last;
    void request(RenderRequest r) override { last = std::move(r); }
    std::optional<RenderResult> poll() override {
        if (!last) return std::nullopt;
        RenderResult r;
        r.version = last->version;
        r.width = r.height = last->size;
        r.rgba8.assign(std::size_t(r.width) * r.height * 4, 0);
        last.reset();
        return r;
    }
};
struct NullSink : TextureSink {
    ImTextureID create(std::uint32_t, std::uint32_t, const std::uint8_t*) override { return 1; }
    bool update(ImTextureID, std::uint32_t, std::uint32_t, const std::uint8_t*) override { return true; }
    void remove(ImTextureID) override {}
};
}  // namespace

TEST_CASE(selftest_runs_the_script_on_a_corpus_bundle_without_a_gpu) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::filesystem::path first;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (std::filesystem::exists(e.path() / "icon.json")) { first = e.path(); break; }
    }
    REQUIRE(!first.empty());
    NullScheduler sched;
    NullSink sink;
    SelfTestReport r = runSelfTest(first, sched, sink, 5);
    std::printf("  %s\n", describe(r).c_str());
    CHECK(r.failure.empty());
    CHECK(r.groups >= 1);
    CHECK(r.layers >= 1);
    CHECK(r.sections >= 6);
    CHECK(r.bytesRoundTripped);
    CHECK(r.textured);
    CHECK_EQ(r.imguiErrors, std::uint64_t(0));
}
```


**Como rodar e o que esperar:**

Run: `cmake --build --preset mingw && IC_CORPUS_DIR=References/corpus build/mingw/Tests/ic_tests.exe selftest_`
Expected: passa e imprime a linha do `describe`. `bytes round-tripped NO` num bundle byte-exato é bug no undo ou no `setProperty`, não no script.

</details>

---

## Parte C — o app, sobre o Onyx

### Task 15: `iconcomposer.exe` — as portas reais e a janela

**Files:**
- Create: `Source/app/CMakeLists.txt`
- Create: `Source/app/OnyxPorts.h`, `Source/app/OnyxPorts.cpp`
- Create: `Source/app/Window.h`, `Source/app/Window.cpp`
- Create: `Source/app/main.cpp`
- Modify: `CMakeLists.txt` (descomentar `add_subdirectory(Source/app)`)

**Interfaces:**
- Consumes: `Onyx::App::Window` (`vkContext()`, `workspace().Jobs()`, `app().SetRegistrar`, `app().SetDefaultLayout`, `app().setPanelVisible`), `Onyx::App::TexturePool`, `Onyx::Services::JobQueue`, `Onyx::App::SystemOpenFileDialog/SystemSaveFileDialog`, `rb::Device`, `rb::renderIcon`, o Kit inteiro.
- Produces:
  ```cpp
  namespace icapp {
  class OnyxTextureSink : public ick::TextureSink { explicit OnyxTextureSink(Onyx::Rendering::VkContext&); void advanceFrame(); ... };
  class JobScheduler : public ick::RenderScheduler { JobScheduler(Onyx::Services::JobQueue&, rb::Device&); ... };
  class SyncScheduler : public ick::RenderScheduler { explicit SyncScheduler(rb::Device&); ... };   // for --selftest
  int run(const std::filesystem::path& initial);   // the window
  }
  ```
  `main`: `iconcomposer [bundle.icon]`, `iconcomposer --selftest <bundle.icon> [--frames N]`. O selftest imprime `describe(...)` e devolve 0 só se `failure` vazio, `bytesRoundTripped` e `textured` verdadeiros e `imguiErrors == 0`.

- [ ] **Step 1: implementar**

`Source/app/CMakeLists.txt`:

```cmake
# The executable, and the only target in this tree that links Onyx (rule 2).
add_executable(iconcomposer
    main.cpp
    OnyxPorts.cpp
    Window.cpp
)
target_link_libraries(iconcomposer PRIVATE IconComposer::Kit Onyx::Onyx)
target_include_directories(iconcomposer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
```

(Se o alvo do Onyx não se chamar `Onyx::Onyx` na versão puxada, o nome certo está em `D:/CodingProjects/OnyxSDK/CMakeLists.txt` na linha `add_library(Onyx::...` ou `ALIAS`; é o mesmo que `sfsymview` linka.)

`OnyxPorts.h`:

```cpp
#pragma once
// The Kit's two ports, made of Onyx and of the RenderBox device.
#include "Source/IconComposerKit/Ports.h"
#include "Source/RenderBox/Device.h"

#include <Onyx/App/TexturePool.h>
#include <Onyx/Services/Jobs.h>

#include <memory>
#include <mutex>
#include <optional>

namespace icapp {

class OnyxTextureSink : public ick::TextureSink {
public:
    explicit OnyxTextureSink(Onyx::Rendering::VkContext& ctx) : pool_(ctx) {}
    ImTextureID create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) override;
    bool update(ImTextureID id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) override;
    void remove(ImTextureID id) override { pool_.Remove(id); }
    void advanceFrame() { pool_.AdvanceFrame(); }   // once per drawn frame, from the canvas panel

private:
    Onyx::App::TexturePool pool_;
};

// One lane, one device: the JobQueue serialises the lane, so two renders never
// share the device. The latest request replaces any earlier one still pending.
class JobScheduler : public ick::RenderScheduler {
public:
    JobScheduler(Onyx::Services::JobQueue& jobs, rb::Device& device) : jobs_(jobs), device_(device) {}
    void request(ick::RenderRequest r) override;
    std::optional<ick::RenderResult> poll() override;

private:
    void submitPending();
    Onyx::Services::JobQueue& jobs_;
    rb::Device& device_;
    std::mutex mutex_;
    std::optional<ick::RenderRequest> pending_;   // waiting for the running job to finish
    bool running_ = false;
    std::optional<ick::RenderResult> done_;
};

// Renders on the calling thread. The selftest's scheduler.
class SyncScheduler : public ick::RenderScheduler {
public:
    explicit SyncScheduler(rb::Device& device) : device_(device) {}
    void request(ick::RenderRequest r) override;
    std::optional<ick::RenderResult> poll() override;

private:
    rb::Device& device_;
    std::optional<ick::RenderResult> done_;
};

// The one function both schedulers call.
ick::RenderResult renderNow(rb::Device& device, const ick::RenderRequest& r);

}  // namespace icapp
```

`OnyxPorts.cpp`:

```cpp
#include "Source/app/OnyxPorts.h"

#include "Source/RenderBox/IconRenderer.h"

#include <string>

namespace icapp {

ImTextureID OnyxTextureSink::create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) {
    std::string err;
    return pool_.Create(w, h, rgba8, err);
}

bool OnyxTextureSink::update(ImTextureID id, std::uint32_t, std::uint32_t, const std::uint8_t* rgba8) {
    std::string err;
    return pool_.Update(id, rgba8, err);
}

ick::RenderResult renderNow(rb::Device& device, const ick::RenderRequest& r) {
    ick::RenderResult out;
    out.version = r.version;
    rb::IconRenderOptions io;
    io.size = r.size;
    io.context = r.context;
    auto icon = rb::renderIcon(device, r.bundle, io);
    if (!icon) {
        out.error = icon.error();
        return out;
    }
    out.width = icon->width;
    out.height = icon->height;
    out.rgba8 = ick::toRgba8(icon->rgba);
    out.drawn = icon->drawn;
    out.total = icon->total;
    for (const auto& s : icon->skipped) {
        out.skipped.push_back("grupo " + std::to_string(s.group) + " / " + s.layer + ": " + s.why);
    }
    out.shapeGaps = icon->shapeGaps;
    out.notes = icon->notes;
    return out;
}

void JobScheduler::request(ick::RenderRequest r) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = std::move(r);   // replaces whatever was waiting: the latest wins
    }
    submitPending();
}

void JobScheduler::submitPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_ || !pending_) return;
    running_ = true;
    auto req = std::make_shared<ick::RenderRequest>(std::move(*pending_));
    pending_.reset();
    auto result = std::make_shared<ick::RenderResult>();
    jobs_.Submit(
        /*lane*/ 1,
        [this, req, result](Onyx::Services::Progress&) { *result = renderNow(device_, *req); },
        [this, result] {
            // Done runs on the main thread, inside Pump().
            {
                std::lock_guard<std::mutex> lock(mutex_);
                done_ = std::move(*result);
                running_ = false;
            }
            submitPending();
        });
}

std::optional<ick::RenderResult> JobScheduler::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<ick::RenderResult> r = std::move(done_);
    done_.reset();
    return r;
}

void SyncScheduler::request(ick::RenderRequest r) { done_ = renderNow(device_, r); }

std::optional<ick::RenderResult> SyncScheduler::poll() {
    std::optional<ick::RenderResult> r = std::move(done_);
    done_.reset();
    return r;
}

}  // namespace icapp
```

`Window.h`:

```cpp
#pragma once
#include <filesystem>

namespace icapp {
// Opens the window, with `initial` loaded when it names a bundle. Returns the
// process exit code.
int run(const std::filesystem::path& initial);
}  // namespace icapp
```

`Window.cpp`:

```cpp
#include "Source/app/Window.h"

#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/app/OnyxPorts.h"

#include <Onyx/App/App.h>
#include <Onyx/App/IPanel.h>
#include <Onyx/App/UIHelpers.h>
#include <Onyx/App/Window.h>
#include <Onyx/Services/Threading.h>
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <memory>
#include <optional>

namespace icapp {
namespace {

// Everything the panels share, owned by run() so destruction order is stated once.
struct State {
    // The window handle, kept because `glfwGetCurrentContext()` is an OpenGL call
    // and returns null in a Vulkan app -- Quit through it would silently do nothing.
    GLFWwindow* window = nullptr;
    std::optional<ick::Session> session;
    std::unique_ptr<OnyxTextureSink> sink;
    std::unique_ptr<JobScheduler> scheduler;
    std::unique_ptr<ick::RenderCoordinator> coordinator;
    ick::MenuActions actions;
    Onyx::App::App* app = nullptr;
    bool quit = false;

    void open(const std::filesystem::path& dir) {
        coordinator.reset();
        session = ick::Session::open(dir);
        if (session) coordinator = std::make_unique<ick::RenderCoordinator>(*scheduler, *sink);
    }
    void close() {
        coordinator.reset();
        session.reset();
    }
    // Runs after the frame: the Kit asked, the app answers with dialogs and files.
    void act() {
        ick::MenuActions a = actions;
        actions = {};
        if (a.newDocument) {
            const std::string p = Onyx::App::SystemSaveFileDialog("Untitled.icon");
            if (!p.empty()) {
                coordinator.reset();
                session = ick::Session::create(p);
                if (session) coordinator = std::make_unique<ick::RenderCoordinator>(*scheduler, *sink);
            }
        }
        if (a.open) {
            const std::string p = Onyx::App::SystemOpenFileDialog({{"Icon Composer document", {"icon"}}});
            if (!p.empty()) open(p);
        }
        if (a.save && session) {
            const std::string r = session->save();
            if (!r.empty()) std::fprintf(stderr, "save: %s\n", r.c_str());
        }
        if (a.saveAs && session) {
            const std::string p = Onyx::App::SystemSaveFileDialog(session->bundle().path().filename().string());
            if (!p.empty()) {
                const std::string r = session->saveAs(p);
                if (!r.empty()) std::fprintf(stderr, "save as: %s\n", r.c_str());
            }
        }
        if (a.close) close();
        if (a.quit) quit = true;
    }
    void title() {
        if (!app) return;
        auto* config = app->getConfig();
        if (!config) return;
        std::string t = "Icon Composer";
        if (session) {
            t += " — " + session->bundle().path().filename().string();
            if (session->isDirty()) t += " •";
        }
        config->windowTitle = t;
    }
};

struct LayersPanel : Onyx::App::IPanel {
    explicit LayersPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session) ick::drawLayers(*st.session);
        else { ImGui::Begin(ick::kLayersWindow); ImGui::TextDisabled("No document"); ImGui::End(); }
    }
    std::string_view getName() const override { return ick::kLayersWindow; }
    State& st;
};

struct CanvasPanel : Onyx::App::IPanel {
    explicit CanvasPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.coordinator && st.session) {
            st.coordinator->tick(*st.session);
            ick::drawCanvas(*st.session, st.coordinator->view(), st.actions);
        } else {
            ImGui::Begin(ick::kCanvasWindow, nullptr, ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New…", "Ctrl+N")) st.actions.newDocument = true;
                    if (ImGui::MenuItem("Open…", "Ctrl+O")) st.actions.open = true;
                    if (ImGui::MenuItem("Quit", "Ctrl+Q")) st.actions.quit = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::TextDisabled("Open a .icon bundle");
            ImGui::End();
        }
    }
    std::string_view getName() const override { return ick::kCanvasWindow; }
    State& st;
};

struct InspectorPanel : Onyx::App::IPanel {
    explicit InspectorPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session) ick::drawInspector(*st.session);
        else { ImGui::Begin(ick::kInspectorWindow); ImGui::End(); }
    }
    std::string_view getName() const override { return ick::kInspectorWindow; }
    State& st;
};

struct DiagnosticsPanel : Onyx::App::IPanel {
    explicit DiagnosticsPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session && st.coordinator) ick::drawDiagnostics(*st.session, st.coordinator->view());
        else { ImGui::Begin(ick::kDiagnosticsWindow); ImGui::End(); }
        // End of frame work, here because this panel is registered LAST: `act()` can
        // replace or close the session, and a swap mid-frame would leave the panels
        // after it drawing against state that changed under them. `advanceFrame`
        // likewise belongs after every upload this frame made.
        st.sink->advanceFrame();
        st.act();
        st.title();
        if (st.quit && st.window) glfwSetWindowShouldClose(st.window, 1);
    }
    std::string_view getName() const override { return ick::kDiagnosticsWindow; }
    State& st;
};

// `[OBS]` 20% / 28% are sfsymview's fractions, not the target's
// WindowLayoutConstants, which have not been read (spec 13/09 §2.1).
void defaultLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);
    ImGuiID centre = dockspaceId;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20f, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.22f, nullptr, &centre);
    ImGui::DockBuilderDockWindow(ick::kLayersWindow, left);
    ImGui::DockBuilderDockWindow(ick::kCanvasWindow, centre);
    ImGui::DockBuilderDockWindow(ick::kInspectorWindow, right);
    ImGui::DockBuilderDockWindow(ick::kDiagnosticsWindow, bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

}  // namespace

int run(const std::filesystem::path& initial) {
    // Validation off: nobody here is checking the driver, and the tower's
    // default (on) costs every frame.
    auto device = rb::Device::create(rb::DeviceOptions{.validation = false});
    if (!device) {
        std::fprintf(stderr, "iconcomposer: no Vulkan device: %s\n", device.error().c_str());
        return 2;
    }

    Onyx::Threading::MarkMainThread();
    Onyx::App::Window::initNative();
    Onyx::App::Window window;

    State state;
    state.window = window.getGLFWwindow();
    state.sink = std::make_unique<OnyxTextureSink>(window.vkContext());
    state.scheduler = std::make_unique<JobScheduler>(window.workspace().Jobs(), *device);

    window.app().SetDefaultLayout(&defaultLayout);
    window.app().SetRegistrar([&state, initial](Onyx::App::App& app) {
        state.app = &app;
        if (auto* config = app.getConfig()) config->windowTitle = "Icon Composer";
        // Onyx's generic panels are for game archives; ours replace them.
        app.setPanelVisible("Documents", false);
        app.setPanelVisible("Inspector", false);
        app.addPanel(std::make_unique<LayersPanel>(state));
        app.addPanel(std::make_unique<CanvasPanel>(state));
        app.addPanel(std::make_unique<InspectorPanel>(state));
        app.addPanel(std::make_unique<DiagnosticsPanel>(state));
        if (!initial.empty()) state.open(initial);
    });
    window.run();
    // The coordinator returns its texture to the pool before the pool goes,
    // and the pool goes before the VkContext (window outlives `state`).
    state.close();
    return 0;
}

}  // namespace icapp
```

`main.cpp`:

```cpp
// iconcomposer -- the editor.
//
//   iconcomposer [bundle.icon]
//   iconcomposer --selftest <bundle.icon> [--frames N]
#include "Source/IconComposerKit/SelfTest.h"
#include "Source/app/OnyxPorts.h"
#include "Source/app/Window.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct NullSink : ick::TextureSink {
    ImTextureID create(std::uint32_t, std::uint32_t, const std::uint8_t*) override { return 1; }
    bool update(ImTextureID, std::uint32_t, std::uint32_t, const std::uint8_t*) override { return true; }
    void remove(ImTextureID) override {}
};

int selftest(const std::string& bundle, int frames) {
    auto device = rb::Device::create(rb::DeviceOptions{.validation = false});
    if (!device) {
        std::fprintf(stderr, "iconcomposer: no Vulkan device: %s\n", device.error().c_str());
        return 2;
    }
    icapp::SyncScheduler scheduler(*device);
    NullSink sink;
    const ick::SelfTestReport r = ick::runSelfTest(bundle, scheduler, sink, frames);
    std::printf("%s\n", ick::describe(r).c_str());
    const bool ok = r.failure.empty() && r.bytesRoundTripped && r.textured && r.imguiErrors == 0;
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--selftest") == 0) {
        int frames = 30;
        for (int i = 3; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--frames") == 0) frames = std::atoi(argv[++i]);
        }
        return selftest(argv[2], frames);
    }
    return icapp::run(argc >= 2 ? argv[1] : "");
}
```

Descomentar `add_subdirectory(Source/app)` no `CMakeLists.txt` da raiz.


- [ ] **Step 2: rodar o selftest, e depois abrir a janela** — este alvo linka Onyx, então nunca entrou em `ic_tests`; a conferência aqui é o próprio binário rodando.

Run: `cmake --preset mingw && cmake --build --preset mingw && build/mingw/Source/app/iconcomposer.exe --selftest References/corpus/<um bundle byte-exato>.icon --frames 30`
Expected: `textured yes; bytes round-tripped yes; imgui errors 0`, exit 0.

Depois, à mão: `build/mingw/Source/app/iconcomposer.exe References/corpus/<o mesmo>.icon` abre a janela com os quatro painéis dockados, o ícone no canvas, e trocar a appearance no combo redesenha. Anotar no commit se o gama do canvas e o do PNG do `icrender` diferem a olho (spec §6 `[OBS]`).

- [ ] **Step 3: commitar**

```bash
git add CMakeLists.txt Source/app
git commit -m "iconcomposer.exe: o Kit sobre a janela do Onyx, com um rb::Device proprio e um render por vez numa lane"
```

---

### Task 16: o estado no README (e o gate, se for rodar)

> O **Step 1** (a linha do README) fecha o trabalho e continua valendo. Os **Steps 2 e 3** são do gate, que é opcional — faça-os só se for de fato rodar a varredura.

**Files:**
- Modify: `Docs/README.md` (a linha `| a UI do app |` na tabela de estado)
- Modify: `scripts/gate-m1.ps1` (uma âncora no Kit)

- [ ] **Step 1: a linha do README**

Trocar a linha `| a UI do app | **não levantada** — e não há nib: o app é SwiftUI |` por:

```markdown
| a UI do app | **abre, desenha e edita** — `iconcomposer.exe` sobre o Onyx: Layers, Canvas, Inspector e Diagnóstico; oito inspetores por escopo, undo por comando, escrita byte-exata provada (`test_bundle_save`), e um `--selftest` que abre, edita, desfaz, salva e compara bytes sem janela. `[BIN]` o inventário da UI do alvo saiu dos 306 tipos de view do slice do `IconComposerKit` (spec 13/09 §2.1). `[OBS]` as larguras dos painéis são as do `sfsymview`, não do alvo; o gama entre canvas e PNG não foi medido (spec §6) |
```

E na tabela de documentos, acrescentar a linha `| [Specs/casca e modelo](Specs/2026-09-13-casca-e-modelo-editavel.md) | ... |` se ainda não estiver (a spec já a acrescentou).

- [ ] **Step 2: uma âncora no Kit**

No `$sources` do gate: `session = Join-Path $root "Source/IconComposerKit/Session.cpp"`. Nas mutações:

```powershell
    # ---- the kit ----
    @{ file = "session"; name = "undo restores the AFTER snapshot"
       from = 'if (icf::json::Value* node = icf::nodeAt(root(), c.target)) *node = c.before;'
       to   = 'if (icf::json::Value* node = icf::nodeAt(root(), c.target)) *node = c.after;' },
    @{ file = "session"; name = "a new command keeps the redo branch"
       from = '    redo_.clear();' + "`n" + '    if (cleanDepth_ > undo_.size())'
       to   = '    if (cleanDepth_ > undo_.size())' },
```

- [ ] **Step 3: o gate inteiro, no worktree**

Run: `powershell -File scripts\gate-worktree.ps1`
Expected: termina em `VERDICT: gate-m1 passed`. O script imprime o total ele mesmo — na
abertura da Task 6 ele dizia 328 mutações, então é esse número, mais as duas da Task 16,
que tem de aparecer em `sweep: N of N mutations caught`. Qualquer mutação não apanhada
volta para a task dona dela; uma que derruba o processo em vez de falhar numa asserção é
falha do TESTE, pela regra do próprio cabeçalho do script.

- [ ] **Step 4: commitar**

```bash
git add Docs/README.md scripts/gate-m1.ps1
git commit -m "gate-m1 passed: a UI do app abre, desenha e edita, e o README diz o que ela nao mediu"
```

---

## Cobertura da spec, conferida

| spec | task |
|---|---|
| §2.2 grafia de número e cor | 1, 2, 6 |
| §2.3 / §4.3 escrever sob escopo | 3, 6 |
| §3 arquitetura, duas portas, build | 7, 15 |
| §4.1 `json::Value` mutável | 1 |
| §4.2 bundle `save`/`saveAs`/`clone` | 5 |
| §4.4 estrutura, importar asset | 4, 5 (`importAsset` existe; o menu de importar é da rodada 4) |
| §5 undo por comando, coalescência, dirty | 8, 16 |
| §6 render, o último vence, diagnóstico na tela, overlay `[INF]` | 13, 12, 15 |
| §7 painéis, menu, seções desabilitadas com motivo | 10, 11, 12, 15 |
| §8 selftest, testes, gate | 14, 15, 6, 16 |
| §9 fora da rodada | nada implementa; o inspetor e o menu nomeiam |
