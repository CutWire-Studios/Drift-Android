#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace drift {

class ChainProcessor;

namespace audiofx {

// Build the stage chain a manifest's "processor" key names, with every parameter identifier that
// manifest can carry bound to a stage setter. Returns nullptr for an id the factory does not know.
//
// Values are not applied here — the caller seeds them from resolvedAudioEffectParameters().
std::unique_ptr<ChainProcessor> createProcessor(const QString &processorId);

// The catalog checks this at load time so a manifest naming a processor that does not exist is
// rejected with a warning instead of turning into a silent no-op effect.
bool hasProcessor(const QString &processorId);

QStringList processorIds();

} // namespace audiofx
} // namespace drift
