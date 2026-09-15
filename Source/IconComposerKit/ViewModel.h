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

const char* appearanceLabel(icf::Appearance a);
const char* idiomLabel(icf::Idiom i);
const char* blendModeLabel(icf::BlendMode m);
const char* shadowKindLabel(icf::ShadowKind k);
const char* specularLabel(icf::SpecularHighlight h);
const char* fillKindLabel(icf::FillKind k);

}  // namespace ick
