// The Asset family of inspector sections. Empty of content on purpose: it is
// the file a Round 3 front fills, and until then it draws what was drawn
// before -- the headers greyed, with the reason in the tooltip (spec 13/09 §7).
#include "Source/IconComposerKit/InspectorSection.h"

namespace ick {

void drawLayerAssetSections(Section& x) {
    const char* kLater = "Round 3: this inspector is not built yet";
    x.disabled("Material", kLater);
    x.disabled("Image Asset", kLater);
    x.disabled("Asset Mirroring", kLater);
}

}  // namespace ick
