#pragma once

#include "TransitionCatalog.h"

#include <QList>
#include <QString>
#include <QStringList>

// Load file-based GPU transition packages (transitions/<name>/transition.json).
namespace TransitionPackageLoader {

// Parse a single package directory. On failure, entry.gpu.valid is false and
// entry.gpu.errorMessage is set.
TransitionPresetEntry loadPackage(const QString &packageDir, QString *errorOut = nullptr);

// Scan each root for immediate subdirectories that contain transition.json.
QList<TransitionPresetEntry> scanDirectories(const QStringList &rootDirs);

// Default search roots: DRIFT_TRANSITIONS_DIR, <appDir>/transitions, <AppDataLocation>/transitions.
QStringList defaultSearchPaths();

} // namespace TransitionPackageLoader
