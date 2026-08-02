#include "AssetLibrary.h"

#include "core/Project.h"

#include "engine/MediaProbe.h"
#include "engine/MediaThumbnail.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QJsonObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>

namespace {

// FFmpeg and the rest of the media pipeline need real filesystem paths. On Android the SAF
// picker returns content:// URIs; Qt can read them via QFile, but avformat cannot. Copy into
// app cache once so the rest of the code stays path-based. Desktop file:// URLs pass through.
QString materializeImportUrl(const QUrl &url)
{
    if (url.isLocalFile()) {
        const QString path = url.toLocalFile();
        return QFileInfo::exists(path) ? QFileInfo(path).absoluteFilePath() : QString();
    }

    if (url.scheme().compare(QLatin1String("content"), Qt::CaseInsensitive) != 0)
        return {};

    const QString uri = url.toString(QUrl::FullyEncoded);
    QFile src(uri);
    if (!src.open(QIODevice::ReadOnly)) {
        qWarning("import: cannot open %s (%s)", qPrintable(uri), qPrintable(src.errorString()));
        return {};
    }

    QString name = QFileInfo(uri).fileName();
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("import.bin");

    const QString destDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/imports");
    if (!QDir().mkpath(destDir)) {
        qWarning("import: cannot create cache dir %s", qPrintable(destDir));
        return {};
    }

    // Stable name per URI so re-importing the same document reuses the cached copy.
    const QString destPath =
        destDir + QLatin1Char('/') + QString::number(qHash(uri), 16) + QLatin1Char('_') + name;
    if (QFileInfo::exists(destPath) && QFileInfo(destPath).size() > 0)
        return destPath;

    QFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("import: cannot write %s (%s)", qPrintable(destPath), qPrintable(dst.errorString()));
        return {};
    }

    constexpr qint64 kChunk = 1024 * 1024;
    while (!src.atEnd()) {
        const QByteArray chunk = src.read(kChunk);
        if (chunk.isEmpty() && src.error() != QFile::NoError) {
            qWarning("import: read failed for %s (%s)", qPrintable(uri), qPrintable(src.errorString()));
            dst.close();
            QFile::remove(destPath);
            return {};
        }
        if (dst.write(chunk) != chunk.size()) {
            qWarning("import: write failed for %s (%s)", qPrintable(destPath),
                     qPrintable(dst.errorString()));
            dst.close();
            QFile::remove(destPath);
            return {};
        }
    }
    return destPath;
}

bool isImagePath(const QString &path)
{
    static const QStringList extensions = {
        QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("gif"),  QStringLiteral("webp"), QStringLiteral("bmp"),
        QStringLiteral("tiff"), QStringLiteral("tif"),  QStringLiteral("svg"),
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

bool isAudioPath(const QString &path)
{
    static const QStringList extensions = {
        QStringLiteral("mp3"),  QStringLiteral("wav"),  QStringLiteral("aac"),
        QStringLiteral("flac"), QStringLiteral("ogg"),  QStringLiteral("m4a"),
        QStringLiteral("wma"),  QStringLiteral("aiff"), QStringLiteral("aif"),
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

drift::MediaKind kindFrom(const MediaInfo &info, const QString &path)
{
    if (isImagePath(path))
        return drift::MediaKind::Image;

    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Video && !stream.attachedPicture)
            return drift::MediaKind::Video;
    }
    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Audio)
            return drift::MediaKind::Audio;
    }
    return drift::MediaKind::Other;
}

drift::MediaKind provisionalKind(const QString &path)
{
    if (isImagePath(path))
        return drift::MediaKind::Image;
    if (isAudioPath(path))
        return drift::MediaKind::Audio;
    return drift::MediaKind::Video;
}

QString formatDuration(drift::TimeUs durationUs)
{
    if (durationUs <= 0)
        return {};

    const int totalSeconds = static_cast<int>(durationUs / drift::kUsPerSecond);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void fillAudioPresence(drift::MediaAsset &asset, const MediaInfo &info)
{
    bool hasAudio = false;
    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Audio) {
            hasAudio = true;
            asset.sampleRate = stream.sampleRate;
            asset.channels = stream.channels;
            if (asset.codecName.isEmpty())
                asset.codecName = stream.codecName;
        }
    }
    asset.hasAudio = hasAudio;
    asset.hasAudioKnown = true;
}

drift::MediaAsset buildProbedAsset(const QString &absolutePath, const QString &name, const MediaInfo &info)
{
    drift::MediaAsset asset;
    asset.name = name;
    asset.path = absolutePath;
    asset.kind = kindFrom(info, absolutePath);
    asset.durationUs = info.durationUs;
    asset.durationLabel =
        asset.kind == drift::MediaKind::Image ? QString() : formatDuration(info.durationUs);

    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Video && !stream.attachedPicture) {
            asset.width = stream.width;
            asset.height = stream.height;
            asset.fps = stream.fps;
            asset.rotationDegrees = stream.rotationDegrees;
            asset.codecName = stream.codecName;
        }
    }
    fillAudioPresence(asset, info);

    const QString kindString = drift::mediaKindToString(asset.kind);
    asset.thumbnailPath = MediaThumbnail::generate(absolutePath, kindString);
    asset.filmstripPath = asset.kind == drift::MediaKind::Video
                              ? MediaThumbnail::generateFilmstrip(absolutePath, kindString)
                              : asset.thumbnailPath;
    return asset;
}

} // namespace

AssetLibrary::AssetLibrary(QObject *parent)
    : QAbstractListModel(parent)
{
    // Re-broadcast every row-count change as countChanged so QML bindings on
    // `count` stay live without each mutation site having to remember to emit.
    connect(this, &QAbstractItemModel::rowsInserted, this, &AssetLibrary::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &AssetLibrary::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &AssetLibrary::countChanged);
}

void AssetLibrary::setProject(drift::Project *project)
{
    beginResetModel();
    m_project = project;
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();
    endResetModel();
}

int AssetLibrary::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_project)
        return 0;
    return m_project->assetOrder().size();
}

const drift::MediaAsset *AssetLibrary::assetAtIndex(int index) const
{
    if (!m_project || index < 0 || index >= m_project->assetOrder().size())
        return nullptr;
    return m_project->asset(m_project->assetIdAt(index));
}

drift::MediaAsset *AssetLibrary::assetAtIndex(int index)
{
    if (!m_project || index < 0 || index >= m_project->assetOrder().size())
        return nullptr;
    return m_project->asset(m_project->assetIdAt(index));
}

QVariant AssetLibrary::data(const QModelIndex &index, int role) const
{
    const drift::MediaAsset *asset = assetAtIndex(index.row());
    if (!index.isValid() || !asset)
        return {};

    switch (role) {
    case IdRole:
        return asset->id;
    case NameRole:
        return asset->name;
    case KindRole:
        return drift::mediaKindToString(asset->kind);
    case DurationRole:
        return asset->durationLabel;
    case DurationSecondsRole:
        return drift::usToSeconds(asset->durationUs);
    case PathRole:
        return asset->path;
    case ThumbnailPathRole:
        return asset->thumbnailPath;
    case FilmstripPathRole:
        return asset->filmstripPath;
    default:
        return {};
    }
}

QHash<int, QByteArray> AssetLibrary::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {KindRole, "kind"},
        {DurationRole, "duration"},
        {DurationSecondsRole, "durationSeconds"},
        {PathRole, "path"},
        {ThumbnailPathRole, "thumbnailPath"},
        {FilmstripPathRole, "filmstripPath"},
    };
}

bool AssetLibrary::containsPath(const QString &path) const
{
    return indexOfPath(path) >= 0;
}

int AssetLibrary::indexOfPath(const QString &path) const
{
    if (!m_project)
        return -1;

    const QString normalized = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_project->assetOrder().size(); ++i) {
        const drift::MediaAsset *asset = assetAtIndex(i);
        if (asset && asset->path == normalized)
            return i;
    }
    return -1;
}

int AssetLibrary::indexOfId(const QString &id) const
{
    if (!m_project)
        return -1;
    return m_project->assetIndex(id);
}

QString AssetLibrary::assetIdAt(int index) const
{
    if (!m_project)
        return {};
    return m_project->assetIdAt(index);
}

void AssetLibrary::emitAssetRowChanged(int index, const QList<int> &roles)
{
    if (index < 0)
        return;
    const QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
}

void AssetLibrary::startThumbJob(const QString &assetId)
{
    if (!m_project || assetId.isEmpty() || m_thumbPending.contains(assetId))
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset)
        return;

    const bool needThumb = asset->thumbnailPath.isEmpty() || !QFileInfo::exists(asset->thumbnailPath);
    const bool needStrip = asset->kind == drift::MediaKind::Video
                           && (asset->filmstripPath.isEmpty() || !QFileInfo::exists(asset->filmstripPath));
    if (!needThumb && !needStrip) {
        if (asset->kind != drift::MediaKind::Video && !asset->thumbnailPath.isEmpty()
            && asset->filmstripPath != asset->thumbnailPath) {
            asset->filmstripPath = asset->thumbnailPath;
            emitAssetRowChanged(indexOfId(assetId), {FilmstripPathRole});
        }
        return;
    }

    m_thumbPending.insert(assetId);
    const QString path = asset->path;
    const drift::MediaKind kind = asset->kind;

    (void)QtConcurrent::run([this, assetId, path, kind, needThumb, needStrip]() {
        const QString kindString = drift::mediaKindToString(kind);
        QString thumb;
        QString strip;
        if (needThumb)
            thumb = MediaThumbnail::generate(path, kindString);
        if (needStrip)
            strip = MediaThumbnail::generateFilmstrip(path, kindString);
        else if (!thumb.isEmpty() && kind != drift::MediaKind::Video)
            strip = thumb;

        QMetaObject::invokeMethod(
            this,
            [this, assetId, thumb, strip]() { applyThumbResult(assetId, thumb, strip); },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::applyThumbResult(const QString &assetId, const QString &thumb, const QString &strip)
{
    m_thumbPending.remove(assetId);
    if (!m_project)
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset)
        return;

    bool changed = false;
    if (!thumb.isEmpty() && asset->thumbnailPath != thumb) {
        asset->thumbnailPath = thumb;
        changed = true;
    }
    if (!strip.isEmpty() && asset->filmstripPath != strip) {
        asset->filmstripPath = strip;
        changed = true;
    } else if (asset->kind != drift::MediaKind::Video && !asset->thumbnailPath.isEmpty()
               && asset->filmstripPath != asset->thumbnailPath) {
        asset->filmstripPath = asset->thumbnailPath;
        changed = true;
    }

    if (!changed)
        return;

    emitAssetRowChanged(indexOfId(assetId), {ThumbnailPathRole, FilmstripPathRole});
    emit assetMetadataChanged(assetId);
}

void AssetLibrary::refreshMediaAt(int index)
{
    drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset)
        return;
    startThumbJob(asset->id);
}

void AssetLibrary::startImportJob(const QString &assetId, const QString &absolutePath, bool imageOnly)
{
    if (assetId.isEmpty() || m_importPending.contains(assetId))
        return;

    m_importPending.insert(assetId);
    const QString name = QFileInfo(absolutePath).fileName();

    (void)QtConcurrent::run([this, assetId, absolutePath, name, imageOnly]() {
        drift::MediaAsset filled;
        bool ok = false;

        if (imageOnly) {
            const QString kind = drift::mediaKindToString(drift::MediaKind::Image);
            const QString thumb = MediaThumbnail::generate(absolutePath, kind);
            QImageReader reader(absolutePath);
            reader.setAutoTransform(true);
            QSize size = reader.size();
            if (reader.transformation() & QImageIOHandler::TransformationRotate90)
                size.transpose();
            filled.name = name;
            filled.path = absolutePath;
            filled.kind = drift::MediaKind::Image;
            filled.width = size.width();
            filled.height = size.height();
            filled.thumbnailPath = thumb;
            filled.filmstripPath = thumb;
            filled.hasAudio = false;
            filled.hasAudioKnown = true;
            ok = true;
        } else {
            const MediaInfo info = MediaProbe::probe(absolutePath);
            if (info.ok) {
                filled = buildProbedAsset(absolutePath, name, info);
                ok = true;
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, assetId, filled, ok]() { applyImportResult(assetId, filled, ok); },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::applyImportResult(const QString &assetId, const drift::MediaAsset &filled, bool ok)
{
    m_importPending.remove(assetId);
    if (!m_project)
        return;

    const int index = indexOfId(assetId);
    if (index < 0)
        return;

    if (!ok) {
        beginRemoveRows({}, index, index);
        m_project->assets().remove(assetId);
        m_project->assetOrder().removeAll(assetId);
        endRemoveRows();
        return;
    }

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset)
        return;

    asset->name = filled.name;
    asset->kind = filled.kind;
    asset->durationUs = filled.durationUs;
    asset->durationLabel = filled.durationLabel;
    asset->path = filled.path;
    asset->width = filled.width;
    asset->height = filled.height;
    asset->fps = filled.fps;
    asset->rotationDegrees = filled.rotationDegrees;
    asset->sampleRate = filled.sampleRate;
    asset->channels = filled.channels;
    asset->codecName = filled.codecName;
    asset->hasAudio = filled.hasAudio;
    asset->hasAudioKnown = filled.hasAudioKnown;
    asset->thumbnailPath = filled.thumbnailPath;
    asset->filmstripPath = filled.filmstripPath;

    emitAssetRowChanged(index,
                        {NameRole, KindRole, DurationRole, DurationSecondsRole, PathRole,
                         ThumbnailPathRole, FilmstripPathRole});
    emit assetMetadataChanged(assetId);
}

QVariantMap AssetLibrary::assetAt(int index) const
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset)
        return {};

    return {
        {QStringLiteral("id"), asset->id},
        {QStringLiteral("name"), asset->name},
        {QStringLiteral("kind"), drift::mediaKindToString(asset->kind)},
        {QStringLiteral("duration"), asset->durationLabel},
        {QStringLiteral("durationSeconds"), drift::usToSeconds(asset->durationUs)},
        {QStringLiteral("path"), asset->path},
        {QStringLiteral("width"), asset->width},
        {QStringLiteral("height"), asset->height},
        {QStringLiteral("fps"), asset->fps},
        {QStringLiteral("rotationDegrees"), asset->rotationDegrees},
        {QStringLiteral("thumbnailPath"), asset->thumbnailPath},
        {QStringLiteral("filmstripPath"), asset->filmstripPath},
        {QStringLiteral("assetIndex"), index},
    };
}

QString AssetLibrary::thumbnailAt(int index) const
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    return asset ? asset->thumbnailPath : QString{};
}

QString AssetLibrary::filmstripAt(int index) const
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    return asset ? asset->filmstripPath : QString{};
}

void AssetLibrary::ensureMedia(int index)
{
    refreshMediaAt(index);
}

void AssetLibrary::ensureAllMedia()
{
    if (!m_project)
        return;
    for (int i = 0; i < m_project->assetOrder().size(); ++i)
        refreshMediaAt(i);
}

void AssetLibrary::ensureAudioPresence(const QString &assetId)
{
    if (!m_project || assetId.isEmpty() || m_audioProbePending.contains(assetId))
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset || asset->hasAudioKnown)
        return;

    if (asset->channels > 0 || asset->sampleRate > 0) {
        asset->hasAudio = true;
        asset->hasAudioKnown = true;
        emit assetMetadataChanged(assetId);
        return;
    }

    m_audioProbePending.insert(assetId);
    const QString path = asset->path;

    (void)QtConcurrent::run([this, assetId, path]() {
        const MediaInfo info = MediaProbe::probe(path);
        bool hasAudio = false;
        int sampleRate = 0;
        int channels = 0;
        if (info.ok) {
            for (const StreamInfo &stream : info.streams) {
                if (stream.type == StreamInfo::Type::Audio) {
                    hasAudio = true;
                    sampleRate = stream.sampleRate;
                    channels = stream.channels;
                    break;
                }
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, assetId, hasAudio, sampleRate, channels]() {
                applyAudioPresence(assetId, hasAudio, sampleRate, channels);
            },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::applyAudioPresence(const QString &assetId, bool hasAudio, int sampleRate, int channels)
{
    m_audioProbePending.remove(assetId);
    if (!m_project)
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset)
        return;

    asset->hasAudio = hasAudio;
    asset->hasAudioKnown = true;
    if (hasAudio) {
        if (sampleRate > 0)
            asset->sampleRate = sampleRate;
        if (channels > 0)
            asset->channels = channels;
    }
    emit assetMetadataChanged(assetId);
}

void AssetLibrary::sortByName()
{
    if (!m_project || m_project->assetOrder().size() < 2)
        return;

    beginResetModel();
    QList<QString> order = m_project->assetOrder();
    std::sort(order.begin(), order.end(), [this](const QString &a, const QString &b) {
        const drift::MediaAsset *assetA = m_project->asset(a);
        const drift::MediaAsset *assetB = m_project->asset(b);
        if (!assetA || !assetB)
            return a < b;
        return assetA->name.compare(assetB->name, Qt::CaseInsensitive) < 0;
    });
    m_project->assetOrder() = order;
    endResetModel();
}

void AssetLibrary::sortByKind()
{
    if (!m_project || m_project->assetOrder().size() < 2)
        return;

    beginResetModel();
    QList<QString> order = m_project->assetOrder();
    std::sort(order.begin(), order.end(), [this](const QString &a, const QString &b) {
        const drift::MediaAsset *assetA = m_project->asset(a);
        const drift::MediaAsset *assetB = m_project->asset(b);
        if (!assetA || !assetB)
            return a < b;
        const int cmp = drift::mediaKindToString(assetA->kind)
                            .compare(drift::mediaKindToString(assetB->kind), Qt::CaseInsensitive);
        return cmp != 0 ? cmp < 0 : assetA->name.compare(assetB->name, Qt::CaseInsensitive) < 0;
    });
    m_project->assetOrder() = order;
    endResetModel();
}

void AssetLibrary::clear()
{
    if (!m_project || m_project->assetOrder().isEmpty())
        return;

    beginResetModel();
    m_project->assets().clear();
    m_project->assetOrder().clear();
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();
    endResetModel();
}

QJsonArray AssetLibrary::toJsonArray() const
{
    if (!m_project)
        return {};

    QJsonArray assets;
    for (const QString &id : m_project->assetOrder()) {
        const drift::MediaAsset *asset = m_project->asset(id);
        if (!asset)
            continue;
        QJsonObject object{
            {QStringLiteral("id"), asset->id},
            {QStringLiteral("name"), asset->name},
            {QStringLiteral("kind"), drift::mediaKindToString(asset->kind)},
            {QStringLiteral("durationUs"), static_cast<double>(asset->durationUs)},
            {QStringLiteral("duration"), asset->durationLabel},
            {QStringLiteral("path"), asset->path},
            {QStringLiteral("width"), asset->width},
            {QStringLiteral("height"), asset->height},
            {QStringLiteral("fps"), asset->fps},
            {QStringLiteral("rotationDegrees"), asset->rotationDegrees},
            {QStringLiteral("sampleRate"), asset->sampleRate},
            {QStringLiteral("channels"), asset->channels},
            {QStringLiteral("codecName"), asset->codecName},
            {QStringLiteral("thumbnailPath"), asset->thumbnailPath},
            {QStringLiteral("filmstripPath"), asset->filmstripPath},
        };
        if (asset->hasAudioKnown)
            object.insert(QStringLiteral("hasAudio"), asset->hasAudio);
        assets.append(object);
    }
    return assets;
}

void AssetLibrary::loadFromJsonArray(const QJsonArray &assets)
{
    if (!m_project)
        return;

    beginResetModel();
    m_project->assets().clear();
    m_project->assetOrder().clear();
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();

    for (const QJsonValue &value : assets) {
        const QJsonObject object = value.toObject();
        drift::MediaAsset asset;
        asset.id = object.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        asset.name = object.value(QStringLiteral("name")).toString();
        asset.kind = drift::mediaKindFromString(object.value(QStringLiteral("kind")).toString());
        asset.durationLabel = object.value(QStringLiteral("duration")).toString();
        if (object.contains(QStringLiteral("durationUs"))) {
            asset.durationUs = static_cast<drift::TimeUs>(object.value(QStringLiteral("durationUs")).toDouble());
        } else {
            asset.durationUs = drift::secondsToUs(object.value(QStringLiteral("durationSeconds")).toDouble());
        }
        asset.path = object.value(QStringLiteral("path")).toString();
        asset.width = object.value(QStringLiteral("width")).toInt();
        asset.height = object.value(QStringLiteral("height")).toInt();
        asset.fps = object.value(QStringLiteral("fps")).toDouble();
        asset.rotationDegrees = object.value(QStringLiteral("rotationDegrees")).toInt();
        asset.sampleRate = object.value(QStringLiteral("sampleRate")).toInt();
        asset.channels = object.value(QStringLiteral("channels")).toInt();
        asset.codecName = object.value(QStringLiteral("codecName")).toString();
        asset.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
        asset.filmstripPath = object.value(QStringLiteral("filmstripPath")).toString();
        if (object.contains(QStringLiteral("hasAudio"))) {
            asset.hasAudioKnown = true;
            asset.hasAudio = object.value(QStringLiteral("hasAudio")).toBool();
        } else if (asset.channels > 0 || asset.sampleRate > 0) {
            asset.hasAudioKnown = true;
            asset.hasAudio = true;
        }
        m_project->addAsset(asset);
    }

    endResetModel();

    for (int i = 0; i < m_project->assetOrder().size(); ++i)
        refreshMediaAt(i);
}

void AssetLibrary::importUrls(const QList<QUrl> &urls)
{
    QStringList paths;
    paths.reserve(urls.size());
    for (const QUrl &url : urls) {
        if (url.isEmpty())
            continue;
        const QString path = materializeImportUrl(url);
        if (path.isEmpty()) {
            qWarning("import: skipped unreadable URL %s", qPrintable(url.toString()));
            continue;
        }
        paths.append(path);
    }
    importFiles(paths);
}

void AssetLibrary::importFiles(const QStringList &paths)
{
    if (!m_project)
        return;

    for (const QString &path : paths) {
        const QFileInfo fileInfo(path);
        const QString absolutePath = fileInfo.absoluteFilePath();
        if (!fileInfo.isFile())
            continue;

        const int existingIndex = indexOfPath(absolutePath);
        if (existingIndex >= 0) {
            refreshMediaAt(existingIndex);
            continue;
        }

        drift::MediaAsset placeholder;
        placeholder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        placeholder.name = fileInfo.fileName();
        placeholder.path = absolutePath;
        placeholder.kind = provisionalKind(absolutePath);

        const int row = m_project->assetOrder().size();
        beginInsertRows({}, row, row);
        m_project->addAsset(placeholder);
        endInsertRows();

        startImportJob(placeholder.id, absolutePath, isImagePath(path));
    }
}
