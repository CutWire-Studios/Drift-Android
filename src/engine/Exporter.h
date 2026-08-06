#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
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

// Upper bound for a hand-typed export frame rate; anything above is clamped.
inline constexpr int kMaxExportFps = 480;

// Full encode settings passed from the export dialog.
struct ExportSettings
{
    int targetHeight = 0; // 0 = keep project height
    // Output frame rate. 0 = follow the project fps. Rational so NTSC rates
    // (24000/1001, 30000/1001, 60000/1001) round-trip exactly.
    int fpsNum = 0;
    int fpsDen = 1;
    QString videoCodecId = QStringLiteral("h264");
    QString rateControl = QStringLiteral("crf"); // "crf" | "bitrate"
    int crf = 18;
    int videoBitrateKbps = 12000;
    QString videoPreset = QStringLiteral("medium");
    QString audioCodecId = QStringLiteral("aac");
    int audioBitrateKbps = 192;
    bool audioOnly = false;
    QString metadataTitle;
    QString metadataArtist;
    QString metadataAlbum;
    QString metadataComment;
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
    // Standalone audio muxer (m4a / mp3 / opus / ac3 / flac).
    static QString preferredAudioOnlyContainer(const QString &audioCodecId);
    static QStringList saveFilters(const QString &container, bool audioOnly = false);
    static QString defaultSuffix(const QString &container, bool audioOnly = false);

    // Downscale chip options for the current project size (no upscale).
    static QVariantList scaleOptions(int projectWidth, int projectHeight);

    // Frame rate choices for the export dialog; the first entry follows `projectFps`.
    static QVariantList frameRateOptions(int projectFps);

    static ExportSettings defaultSettings();
    static ExportSettings settingsFromMap(const QVariantMap &map);

    // `outputPath` is a filesystem path, or — on Android — the fully encoded content:// URI of a
    // document the save picker created (AndroidUri::filePath of what FileDialogs returned).
    static bool run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                    QString *errorOut, const ProgressFn &onProgress = {});

    // Copies a finished export into the shared Movies (or Music) collection and returns the
    // MediaStore URI it now lives at, so the gallery and the share sheet can see it: a file left
    // in app storage is reachable from neither. Empty with *errorOut set on failure, and always
    // empty on desktop, where an export already lands wherever the user pointed it.
    static QUrl publishToGallery(const QUrl &source, const QString &displayName,
                                 QString *errorOut = nullptr);
};
