#pragma once
// The typed view over a parsed `.icon` document.
//
// It is a VIEW, not a copy: the JSON tree stays the storage. That keeps the
// round-trip total -- a key this layer does not model still survives a read and
// a write -- which matters for a format that ships a new build every few weeks.
#include "Source/IconComposerFoundation/Json.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icf {

enum class Appearance { Base, Light, Dark, Tinted };
enum class Idiom { Base, Square, IOS, MacOS, WatchOS };

std::optional<Appearance> appearanceFromString(std::string_view s);
std::optional<Idiom> idiomFromString(std::string_view s);

// The disk spelling of each case. `Base` answers "base", which the format never
// writes (doc 01 §6: 0 of 1,740 entries) -- a writer tests for Base first.
std::string_view appearanceToString(Appearance a);
std::string_view idiomToString(Idiom i);

// ─── OS DOIS EIXOS SÃO ÁRVORES, E O IDIOM TEM DOIS NÍVEIS ────────────────────
//
// `[BIN]` `IconComposerFoundation.Idiom.parent` (`0x376F4`, fatia 27.0-129) é um
// dicionário montado em `0x37360` a partir de três constantes: o laço percorre
// `0x157B28` (`00 01 02 03 04`, os cinco casos) e, para cada um, escreve-se como
// PAI de cada membro de uma lista de filhos -- `0x157B68` para o caso 0 e
// `0x157B38` para o caso 1, ambos arrays Swift estáticos de contagem 2 em `+0x10`
// e elementos em `+0x20`:
//
//     0x157B68  ->  [01, 04]     base   é pai de  square  e  watchOS
//     0x157B38  ->  [02, 03]     square é pai de  iOS     e  macOS
//
// Os casos 2, 3 e 4 caem no array vazio (`0x373DC`: `parent - 2 < 3` desvia para
// o singleton de `0x150438`), logo iOS, macOS e watchOS não têm filhos. O que
// não está na tabela devolve 5, que é o `nil` de `Optional<Idiom>`.
//
// **É ISTO que consertou o "todo modo além de Square vira um quadrado
// desalinhado".** Uma entrada predicada `idiom: square` precisa casar um
// contexto `iOS` ou `macOS`, porque `square` é a FAMÍLIA dos dois. Com igualdade
// simples ela não casava, o documento caía no default, e a composição que o
// autor escreveu para as plataformas quadradas sumia.
//
// `[BIN]` `Appearance.parent` (`0x20948`) é `tst w0,#0xff; mov w8,#4; csel` --
// `base` devolve 4 (o `nil` de `Optional<Appearance>`) e **todo o resto devolve
// `base`**. É uma árvore de um nível só, e como `base` nunca é escrito em disco
// (doc 01 §6: 0 de 1.740 entradas) ela não move nenhum documento do corpus; está
// aqui porque a regra é dos dois eixos e metade dela escrita é uma armadilha.
std::optional<Idiom> idiomParent(Idiom i);
std::optional<Appearance> appearanceParent(Appearance a);

// Um predicado `slot` alcança `ctx` quando é ele mesmo ou um ancestral dele.
bool idiomCovers(Idiom slot, Idiom ctx);
bool appearanceCovers(Appearance slot, Appearance ctx);

// `[BIN]` `Idiom.precedent` (`0xD5580`, um `b` para `0x3A594`) e
// `Appearance.precedent` (`0x3A594`) são A MESMA função: `and x0, x0, #0xff` --
// o precedente É o valor cru do caso. Base 0 < square 1 < iOS 2 < macOS 3 <
// watchOS 4; base 0 < light 1 < dark 2 < tinted 3.
int idiomPrecedent(Idiom i);
int appearancePrecedent(Appearance a);

// What a property is resolved FOR.
struct Context {
    Appearance appearance = Appearance::Base;
    Idiom idiom = Idiom::Base;

    // Two contexts are the same when they resolve the same, which for an
    // aggregate of two enums is memberwise. The canvas needs this to ask
    // "is what is on screen still what was asked for?" without spelling the
    // fields out at each call site.
    bool operator==(const Context&) const = default;
};

// The value of `property` on `owner` under `ctx`, or nullptr when neither the
// plain property nor a matching specialization provides one.
//
// A REGRA, E DE ONDE CADA METADE DELA VEM
//
// `[ART]` Doc 01 §5, sobre as 890 listas do corpus: entre as entradas cujo
// predicado casa o contexto vence a mais específica, a entrada sem predicado é
// o default, e uma especialização supera a propriedade simples.
//
// `[BIN]` O que "casa" e o que "mais específica" querem dizer não é contagem de
// chaves -- é a máquina que a fatia 27.0-129 carrega inteira:
//
//   * O predicado de uma entrada é um `SpecializationSlot`, o par
//     `(idiom, appearance)` com `base` no lugar da chave ausente
//     (`IconComposerFoundation` `0x1319E8`, os dois campos nessa ordem).
//   * Ele CASA quando cada componente é ancestral-ou-si do componente do
//     contexto -- ver `idiomParent` acima para a árvore.
//   * `SpecializationSlot.precedent` (`0xD55B8`) desempacota o slot em
//     `(idiom, appearance)` -- `and x0,x0,#0xff` no idiom, `ubfx w1,w0,#8,#8` na
//     aparência, então o IDIOM é o componente 0.
//   * `SpecializationSlot.Precedent.<` (`0xD5A40`) é lexicográfico com o
//     componente 0 como chave PRIMÁRIA: `cmp x0,x2` decide, e `cmp x1,x3` só
//     desempata. **Idiom domina aparência** -- uma `{idiom: square}` vence uma
//     `{appearance: dark}`, o que a contagem de chaves empatava.
//
// `[INF]` Que o vencedor seja o máximo desse precedente entre os que casam, e
// não a última que casa, é inferência: `parent`, um `precedent` ordenado e um
// `Precedent` Comparable só compõem desse jeito, mas eu não desmontei até o fim
// o caminhador genérico (`SpecializableProperty.slotProvidingValue(for:)`,
// `0xD3C28`, que delega a `0xD3A44` com um closure). O corpus não distingue as
// duas leituras: a medição abaixo fecha com 0 empates.
//
// `[ART]` O que a hierarquia muda, medido sobre o corpus inteiro (890 listas ×
// os 20 contextos = 17.800 células): **432 células mudam, em 57 listas, e todas
// as 432 caem em `iOS` ou `macOS`** -- exatamente os dois filhos de `square`, e
// nenhuma em base, square ou watchOS. **0 células ficam ambíguas**, isto é, em
// nenhuma delas duas entradas empatam no precedente máximo, então a regra é
// total sobre este corpus.
const json::Value* resolve(const json::Value& owner, std::string_view property, Context ctx);


// A layer inside a group. A view: it borrows the JSON node, it does not own it.
class Layer {
public:
    explicit Layer(const json::Value& node) : node_(&node) {}

    const json::Value& json() const { return *node_; }
    std::string_view name() const { return textOf("name"); }
    std::string_view imageName() const { return textOf("image-name"); }
    const json::Value* resolve(std::string_view property, Context ctx) const;

private:
    std::string_view textOf(std::string_view key) const;
    const json::Value* node_;
};

// A group of layers.
class Group {
public:
    explicit Group(const json::Value& node) : node_(&node) {}

    const json::Value& json() const { return *node_; }
    std::string_view name() const;
    std::vector<Layer> layers() const;
    const json::Value* resolve(std::string_view property, Context ctx) const;

private:
    const json::Value* node_;
};

// The document. `open` refuses anything without the obligatory core -- an object
// carrying `groups` -- because a view over something that is not a `.icon` would
// answer questions with silence instead of an error.
class IconDocument {
public:
    static std::optional<IconDocument> open(const json::Value& root);

    const json::Value& json() const { return *root_; }
    std::vector<Group> groups() const;

    // Every key in the document this layer does not model, as a path
    // (`groups[0]/layers[1]/wibble`), in document order. Empty means the model
    // recognised everything -- which the corpus gate turns into the claim that
    // it knows the format.
    std::vector<std::string> unknownKeys() const;

private:
    explicit IconDocument(const json::Value& root) : root_(&root) {}
    const json::Value* root_;
};

}  // namespace icf
