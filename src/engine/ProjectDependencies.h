#pragma once

#include "ProjectBundle.h"

#include <QList>
#include <QString>

namespace drift {
class Project;
}

// What a project needs from outside its own document, in the shape ProjectBundle wants: every file
// it points at, and every addon that must be installed for it to render as saved.

namespace drift::bundle {

// Every external file the project references, in document order: source media first, then the
// derived artifacts (mattes, face tracks). `embedded` is preset — derived artifacts are always
// embedded, source media follows `embedSource` — and callers may still flip source entries.
//
// Thumbnails and filmstrips are deliberately absent: they are cache renders regenerated on load.
QList<MediaEntry> collectMedia(const Project &project, bool embedSource);

// Addons that must be present for the project to render as saved. Resolved by tracing each catalog
// entry the project uses back to the install directory it came from, so an effect that ships with
// the build contributes nothing. Baked artifacts (subtitle cues, mattes, face tracks) create no
// dependency — the model that produced them is not needed again.
QList<AddonRef> collectAddons(const Project &project);

} // namespace drift::bundle
