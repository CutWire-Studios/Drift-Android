#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace drift {
class Project;
}

// Named scale targets used by the simple downscale chips. Resolution is derived
// from the project's aspect ratio so export never distorts.
struct ExportScalePreset
{
    QString id;
    QString label;
    int targetHeight = 0; // 0 = keep project height
    int videoBitrateKbps = 12000;
};

// Full encode settings passed from the export dialog.
struct ExportSettings
{
    int targetHeight = 0; // 0 = keep project height
    QString videoCodecId = QStringLiteral("h264");
    QString rateControl = QStringLiteral("crf"); // "crf" | "bitrate"
    int crf = 23;
    int videoBitrateKbps = 12000;
    QString videoPreset = QStringLiteral("medium");
    QString audioCodecId = QStringLiteral("aac");
    int audioBitrateKbps = 192;
};

// WYSIWYG exporter: encodes frames straight from FrameCompositor and audio from
// AudioMixer, so the exported file matches the preview exactly (single compositor).
class Exporter
{
public:
    // Called with progress in [0,1]; return false to cancel the export.
    using ProgressFn = std::function<bool(double)>;

    static const QList<ExportScalePreset> &scalePresets();
    static const ExportScalePreset *scalePresetById(const QString &id);

    // HandBrake-like catalogs; `available` reflects runtime libav encoder presence.
    static QVariantList videoCodecs();
    static QVariantList audioCodecs();
    static QVariantMap videoCodecById(const QString &id);
    static QVariantMap audioCodecById(const QString &id);

    // Preferred container extension for a video+audio pair (mp4 / webm / mkv).
    static QString preferredContainer(const QString &videoCodecId, const QString &audioCodecId);
    static QStringList saveFilters(const QString &container);
    static QString defaultSuffix(const QString &container);

    // Downscale chip options for the current project size (no upscale).
    static QVariantList scaleOptions(int projectWidth, int projectHeight);

    static ExportSettings defaultSettings();
    static ExportSettings settingsFromMap(const QVariantMap &map);

    static bool run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                    QString *errorOut, const ProgressFn &onProgress = {});
};
