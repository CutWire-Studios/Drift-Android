#pragma once

#include "EffectCatalog.h"

#include <QList>
#include <QString>
#include <QStringList>

// Load file-based GPU effect packages (effects/<name>/effect.json).
namespace EffectPackageLoader {

// Parse a single package directory containing effect.json. On failure, entry.gpu.valid
// is false and entry.gpu.errorMessage is set; the returned entry may still be incomplete.
EffectPresetEntry loadPackage(const QString &packageDir, QString *errorOut = nullptr);

// Scan each root for immediate subdirectories that contain effect.json.
QList<EffectPresetEntry> scanDirectories(const QStringList &rootDirs);

// Default search roots: <appDir>/effects, <AppDataLocation>/effects, and
// DRIFT_EFFECTS_DIR if set (colon-separated on Unix).
QStringList defaultSearchPaths();

} // namespace EffectPackageLoader
