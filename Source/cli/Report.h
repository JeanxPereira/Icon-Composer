#pragma once
// What `ictool` prints.
//
// Kept apart from the executable on purpose, and for the reason `sfsymview_lib`
// exists in SF-Symbols: a report is a pure function from a bundle to text, so it
// is testable without a process, an argv or a terminal. `main.cpp` is the thin
// part that has none of that.
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"

#include <string>

namespace icf::cli {

// What the bundle holds, in four lines.
std::string summary(const IconBundle& bundle);

// The composition RESOLVED for one context: groups, layers, and the properties
// each layer ends up with.
std::string tree(const IconBundle& bundle, Context ctx);

// The reference between the document and `Assets/`, both directions.
std::string assets(const IconBundle& bundle);

}  // namespace icf::cli
