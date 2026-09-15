// The Effects family of inspector sections. Empty of content on purpose: it is
// the file a Round 3 front fills, and until then it draws what was drawn
// before -- the headers greyed, with the reason in the tooltip (spec 13/09 §7).
#include "Source/IconComposerKit/InspectorSection.h"

namespace ick {

void drawGroupEffectSections(Section& x) {
    const char* kLater = "Round 3: this inspector is not built yet";
    x.disabled("Blur Material", kLater);
    x.disabled("Refractivity", kLater);
    x.disabled("Lighting", kLater);
    x.disabled("Group Effects", kLater);
}

}  // namespace ick
