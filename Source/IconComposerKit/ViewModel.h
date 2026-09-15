#pragma once
// What a panel needs to know about a node, computed from the Session and never
// stored: the resolved value under the inspector's scope and whether that scope
// OWNS it, the node's kind and title, and the UI spelling of each vocabulary.
//
// "Own or inherited" is the one thing the inspector cannot afford to guess
// (spec 13/09 §7): a section shows the value `resolve` gives for the scope, and
// marks it "herdado de Base" exactly when that scope has no entry of its own --
// which is also when "remover override" has nothing to remove. The question is
// already answered by `icf::hasOwnEntry` (spec 13/09 §4.3, over the corpus's 890
// specialization lists), and this layer only carries the answer to the panel.
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

// THE IDIOM THE CANVAS OPENS AT, CHOSEN BY THE DOCUMENT
// -----------------------------------------------------------------------------
// `supported-platforms` is the one key that says where the icon actually ships,
// and `[ART]` 145 of 145 corpus documents carry it -- it is not optional in
// practice. `Idiom::Base` is the format's FALLBACK, not a platform: no document
// ships its Base composition anywhere, and under Base not one of the corpus's
// 84 idiom-predicated specialization entries resolves. Opening there shows a
// composition the document never claimed to support.
//
// So the opening idiom is read off the declaration, and the mapping was
// measured, not guessed (laudo 15/09 §2):
//
//   squares "shared"            120 docs -> square    the family, unnarrowed
//   squares ["macOS"]            22      -> macOS     one member, so name it
//   squares ["iOS"]               3      -> iOS
//   squares ["iOS","macOS"]       -      -> square    two members: the family
//   no squares, circles present   0      -> watchOS   the round family
//   no supported-platforms        0      -> Base      nothing declared, nothing to honour
//
// The narrowing on the list form is the documents' own habit: `[ART]` all seven
// documents that declare `squares: ["macOS"]` AND carry an idiom specialization
// write that specialization as `idiom: macOS`, never as `square`. Over the whole
// corpus the rule reaches 71 of the 84 idiom entries against Base's 0.
icf::Idiom declaredIdiom(const icf::json::Value& root);

// `supported-platforms` in one line, for the diagnostics row that keeps the
// canvas from lying about which composition is on screen.
std::string declaredPlatformsText(const icf::json::Value& root);

const char* appearanceLabel(icf::Appearance a);
const char* idiomLabel(icf::Idiom i);
const char* blendModeLabel(icf::BlendMode m);
const char* shadowKindLabel(icf::ShadowKind k);
const char* specularLabel(icf::SpecularHighlight h);
const char* fillKindLabel(icf::FillKind k);

}  // namespace ick
