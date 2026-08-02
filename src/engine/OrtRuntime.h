#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <onnxruntime_cxx_api.h>

// Loading ONNX Runtime, which Drift does not link.
//
// The runtime is an addon like any other bulky optional thing: the app ships headers only and
// dlopens whichever build the user installed, so the CPU / CUDA / WebGPU decision is theirs and
// costs nobody who does not want it 267 MB. That is only possible because the C++ API is a header
// wrapper over one function table — with ORT_API_MANUAL_INIT defined, `OrtGetApiBase` is the sole
// symbol that has to come from the library, and everything else follows from the table it returns.
//
// The consequence for every caller: nothing in Ort:: may be touched before ensureLoaded() has
// returned true, including construction of an Ort::Env or Ort::MemoryInfo, because until then the
// table pointer is null. That is why env() and cpuMemory() live here instead of being members of
// the four model classes.
namespace drift::ort {

// Addon kinds. "onnxruntime" is a complete runtime distribution and replaces the core;
// "onnxruntime-ep" is a plugin execution provider library that layers onto whatever core is
// loaded (ONNX Runtime >= 1.23), which is how WebGPU reaches AMD and Intel GPUs without a
// second 200 MB core.
inline constexpr char kRuntimeKind[] = "onnxruntime";
inline constexpr char kPluginEpKind[] = "onnxruntime-ep";

struct RuntimeInfo
{
    QString variant; // "cpu", "cuda", …
    QString version; // ONNX Runtime version, when the directory says
    QString dir;     // root holding lib/
};

struct PluginEp
{
    QString variant; // selection token from the package, e.g. "webgpu"
    QString library; // absolute path
    // Filled in at registration from what the library actually contributed to the env, not from
    // the package — the provider is the authority on what ONNX Runtime will call it.
    QString epName;
    int minApiVersion{};
};

// Idempotent, and cheap after the first call. False leaves *error set to something a user can act
// on; the failure is sticky, so a broken runtime is reported identically to every feature that
// asks rather than retried per model.
bool ensureLoaded(QString *error);

// Whether a runtime is installed, without loading it. For UI gating — asking this question must
// not have the side effect of pulling 23 MB of library into the process.
bool available();

// Valid only once ensureLoaded() has returned true. The Env is process-wide: ONNX Runtime keeps
// one internally regardless, and plugin EP libraries are registered against it exactly once.
Ort::Env &env();
const Ort::MemoryInfo &cpuMemory();

// What is installed, in search order — the first entry is what ensureLoaded() would pick when no
// variant is preferred.
QList<RuntimeInfo> installedRuntimes();
QList<PluginEp> installedPluginEps();

// The subset of installedPluginEps() the loaded core actually accepted. Empty until env() has
// been constructed.
QList<PluginEp> registeredPluginEps();

// Empty until a runtime is loaded.
QString activeVariant();
QString activeVersion();

// Selection tokens that could work here and now: "cpu", plus "cuda" when the loaded core carries
// the provider, plus one per registered plugin EP. Drives the picker in the Addon Manager.
QStringList selectableVariants();

// The user's choice, from the "ort/variant" setting: "auto" (the default), "cpu", "cuda",
// "webgpu", … DRIFT_ORT_EP overrides it, since it exists to investigate exactly this.
QString preferredVariant();
void setPreferredVariant(const QString &variant);

// True when the choice was made rather than left on "auto". Asking for a provider and quietly
// getting CPU hides exactly what was being investigated, so an explicit choice fails loudly where
// an automatic one falls back.
bool variantExplicit();

// The EP name to hand ONNX Runtime for a plugin-EP variant token, or empty when the variant is
// not a plugin EP registered on this env. Requires the runtime to be loaded.
QString pluginEpNameForVariant(const QString &variant);

} // namespace drift::ort
