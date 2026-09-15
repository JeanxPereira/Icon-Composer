// The Document family of inspector sections. Empty of content on purpose: it is
// the file a Round 3 front fills, and until then it draws what was drawn
// before -- the headers greyed, with the reason in the tooltip (spec 13/09 §7).
#include "Source/IconComposerKit/InspectorSection.h"

namespace ick {

void drawDocumentSections(Section& x) {
    const char* kLater = "Round 3: this inspector is not built yet";
    x.disabled("Document Settings", kLater);
}

}  // namespace ick
