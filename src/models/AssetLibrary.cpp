#include "AssetLibrary.h"

#include "core/Project.h"

#include "engine/AndroidUri.h"
#include "engine/MediaProbe.h"
#include "engine/MediaThumbnail.h"

#include <QCryptographicHash>
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
#include <optional>

namespace {

#ifdef Q_OS_ANDROID

constexpr qint64 kChunk = 1024 * 1024;

// Copies of SAF documents. AppDataLocation and not CacheLocation: a saved project points
// straight at these files, and Android reclaims the cache under storage pressure while
// Settings > Clear cache wipes it outright.
QString importsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/imports");
}

// Where migrateLegacyImports() parks copies made by a build that materialized into the cache.
// They keep their old file names because that is all a project saved by that build recorded.
QString legacyImportsDir()
{
    return importsDir() + QStringLiteral("/legacy");
}

// Builds before this one materialized into CacheLocation and threw the content:// URI away, so
// a cache wipe left every saved project pointing at bytes that no longer existed anywhere.
// Move what is still there somewhere durable; restoreMissingSources() repoints the projects.
void migrateLegacyImports()
{
    QDir stale(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
               + QStringLiteral("/imports"));
    if (!stale.exists())
        return;

    const QString destDir = legacyImportsDir();
    if (!QDir().mkpath(destDir)) {
        qWarning("import: cannot create %s", qPrintable(destDir));
        return;
    }

    for (const QString &name : stale.entryList(QDir::Files)) {
        const QString from = stale.filePath(name);
        const QString to = destDir + QLatin1Char('/') + name;
        if (QFileInfo::exists(to))
            QFile::remove(from);
        else
            QFile::rename(from, to);
    }
    QDir().rmdir(stale.absolutePath());
}

// Deletes a copy this app made, and the per-file directory holding it. Anything else on disk is
// left alone — on desktop an asset points at the user's own file and never matches.
void discardMaterializedCopy(const QString &path)
{
    const QString root = importsDir();
    if (path.isEmpty() || !path.startsWith(root + QLatin1Char('/')))
        return;

    QFile::remove(path);
    const QString dir = QFileInfo(path).absolutePath();
    if (dir != root && dir != legacyImportsDir())
        QDir().rmdir(dir); // no-op unless that was the last file in it
}

QString sanitizedFileName(QString name)
{
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("import.bin");
    return name;
}

#else

inline void migrateLegacyImports() {}
inline void discardMaterializedCopy(const QString &) {}

#endif // Q_OS_ANDROID

// FFmpeg and the rest of the media pipeline need real filesystem paths. On Android the SAF
// picker returns content:// URIs; Qt can read them via QFile, but avformat cannot. Copy into
// app storage once so the rest of the code stays path-based. Desktop file:// URLs pass through.
ImportSource materializeImportUrl(const QUrl &url)
{
    if (url.isLocalFile()) {
        const QString path = url.toLocalFile();
        if (!QFileInfo::exists(path))
            return {};
        return {QFileInfo(path).absoluteFilePath(), {}};
    }

#ifdef Q_OS_ANDROID
    if (!AndroidUri::isContentUri(url))
        return {};

    const QString uri = url.toString(QUrl::FullyEncoded);
    std::unique_ptr<QFile> src = AndroidUri::openForRead(url);
    if (!src) {
        qWarning("import: cannot open %s", qPrintable(uri));
        return {};
    }

    // The grant that came with the picker result dies with the process, which would make the URI
    // stored on the asset worthless the next time the project is opened.
    AndroidUri::takePersistableReadPermission(url);

    // The same file picked through Photos, Files and a cloud provider arrives as three unrelated
    // URIs, so the URI cannot key the copy: the head of the stream and its length can, and the
    // head is a chunk of the copy that is about to happen anyway.
    const QByteArray head = src->read(kChunk);
    if (head.isEmpty() && src->error() != QFile::NoError) {
        qWarning("import: read failed for %s (%s)", qPrintable(uri), qPrintable(src->errorString()));
        return {};
    }
    QCryptographicHash key(QCryptographicHash::Sha1);
    key.addData(head);
    key.addData(QByteArray::number(src->size()));

    // One directory per file so the copy can keep the document's real name — the bin, the clip
    // labels and the export default all show it, and probeAsset reads the kind off its suffix.
    const QString destDir = importsDir() + QLatin1Char('/')
                            + QString::fromLatin1(key.result().left(8).toHex());
    const QString destPath =
        destDir + QLatin1Char('/') + sanitizedFileName(AndroidUri::displayName(url));
    if (QFileInfo::exists(destPath))
        return {destPath, uri};

    if (!QDir().mkpath(destDir)) {
        qWarning("import: cannot create %s", qPrintable(destDir));
        return {};
    }

    // Copy aside and rename, so a process death mid-copy cannot leave a truncated file that
    // every later import of the same media would reuse.
    const QString partPath = destPath + QStringLiteral(".part");
    QFile dst(partPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("import: cannot write %s (%s)", qPrintable(partPath), qPrintable(dst.errorString()));
        return {};
    }

    QByteArray chunk = head;
    while (!chunk.isEmpty()) {
        if (dst.write(chunk) != chunk.size()) {
            qWarning("import: write failed for %s (%s)", qPrintable(partPath),
                     qPrintable(dst.errorString()));
            dst.remove();
            return {};
        }
        chunk = src->read(kChunk);
        if (chunk.isEmpty() && src->error() != QFile::NoError) {
            qWarning("import: read failed for %s (%s)", qPrintable(uri),
                     qPrintable(src->errorString()));
            dst.remove();
            return {};
        }
    }

    dst.close();
    if (!dst.rename(destPath)) {
        qWarning("import: cannot finish %s (%s)", qPrintable(destPath), qPrintable(dst.errorString()));
        dst.remove();
        return {};
    }
    return {destPath, uri};
#else
    return {};
#endif
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

drift::MediaAsset buildImageAsset(const QString &absolutePath, const QString &name)
{
    const QString kindString = drift::mediaKindToString(drift::MediaKind::Image);
    const QString thumb = MediaThumbnail::generate(absolutePath, kindString);
    QImageReader reader(absolutePath);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (reader.transformation() & QImageIOHandler::TransformationRotate90)
        size.transpose();

    drift::MediaAsset asset;
    asset.name = name;
    asset.path = absolutePath;
    asset.kind = drift::MediaKind::Image;
    asset.width = size.width();
    asset.height = size.height();
    asset.thumbnailPath = thumb;
    asset.filmstripPath = thumb;
    asset.hasAudio = false;
    asset.hasAudioKnown = true;
    return asset;
}

// Reads everything the bin needs about a file. Blocking, so it only ever runs on a worker
// thread — shared by the import path and the replace path.
std::optional<drift::MediaAsset> probeAsset(const QString &absolutePath, bool imageOnly)
{
    const QString name = QFileInfo(absolutePath).fileName();
    if (imageOnly)
        return buildImageAsset(absolutePath, name);

    const MediaInfo info = MediaProbe::probe(absolutePath);
    if (!info.ok)
        return std::nullopt;
    return buildProbedAsset(absolutePath, name, info);
}

} // namespace

AssetLibrary::AssetLibrary(QObject *parent)
    : QAbstractListModel(parent)
{
    migrateLegacyImports();

    // Re-broadcast every row-count change as countChanged so QML bindings on
    // `count` stay live without each mutation site having to remember to emit.
    connect(this, &QAbstractItemModel::rowsInserted, this, &AssetLibrary::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &AssetLibrary::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &AssetLibrary::countChanged);

    connect(this, &QAbstractItemModel::rowsInserted, this, &AssetLibrary::snapshotAssets);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &AssetLibrary::snapshotAssets);
    connect(this, &QAbstractItemModel::modelReset, this, &AssetLibrary::snapshotAssets);
}

QList<QString> AssetLibrary::currentPaths() const
{
    if (!m_project)
        return {};

    QList<QString> paths;
    paths.reserve(m_project->assetOrder().size());
    for (const QString &id : m_project->assetOrder()) {
        const drift::MediaAsset *asset = m_project->asset(id);
        paths.append(asset ? asset->path : QString{});
    }
    return paths;
}

void AssetLibrary::snapshotAssets()
{
    m_syncedOrder = m_project ? m_project->assetOrder() : QList<QString>{};
    m_syncedPaths = currentPaths();
}

void AssetLibrary::syncToProject()
{
    if (!m_project)
        return;

    // Undo/redo assigns the whole project behind this model's back. Resetting
    // unconditionally would rebuild every card on every unrelated timeline
    // undo, so only an actual order change is worth the churn.
    if (m_syncedOrder != m_project->assetOrder()) {
        beginResetModel();
        endResetModel();
        // An undone removal brings back a row whose materialized copy removeAssetAt deleted.
        restoreMissingSources();
        return;
    }

    // An undone source replace leaves the order untouched — same row, same id, different file —
    // so the paths have to be compared too or the card keeps showing the media it no longer
    // points at. Only the rows that actually moved are re-read.
    const QList<QString> paths = currentPaths();
    if (paths == m_syncedPaths)
        return;

    for (int i = 0; i < paths.size(); ++i) {
        if (i < m_syncedPaths.size() && m_syncedPaths.at(i) == paths.at(i))
            continue;
        emitAssetRowChanged(i, {}); // empty roles: every role may have moved with the file
        emit assetMetadataChanged(m_project->assetIdAt(i));
    }
    m_syncedPaths = paths;
}

void AssetLibrary::setProject(drift::Project *project)
{
    beginResetModel();
    m_project = project;
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();
    endResetModel();

    restoreMissingSources();
}

void AssetLibrary::restoreMissingSources()
{
#ifdef Q_OS_ANDROID
    // Queued: the call sites run in the middle of a project load or an undo, and repointing an
    // asset emits into AppController, which is not finished wiring the project up yet.
    QMetaObject::invokeMethod(
        this,
        [this]() {
            if (!m_project)
                return;

            for (const QString &id : m_project->assetOrder()) {
                const drift::MediaAsset *asset = m_project->asset(id);
                if (!asset || asset->path.isEmpty() || QFileInfo::exists(asset->path))
                    continue;

                // Imports an older build left in the cache were moved out of it at startup; the
                // project still names the cache path they had.
                const QString moved =
                    legacyImportsDir() + QLatin1Char('/') + QFileInfo(asset->path).fileName();
                if (QFileInfo::exists(moved)) {
                    repointAssetSource(id, moved);
                    continue;
                }

                if (!asset->sourceUri.isEmpty())
                    startRestoreJob(id, asset->sourceUri);
            }
        },
        Qt::QueuedConnection);
#endif
}

void AssetLibrary::startRestoreJob(const QString &assetId, const QString &sourceUri)
{
    if (m_importPending.contains(assetId))
        return;

    m_importPending.insert(assetId);
    (void)QtConcurrent::run([this, assetId, sourceUri]() {
        const ImportSource restored = materializeImportUrl(QUrl(sourceUri));
        QMetaObject::invokeMethod(
            this,
            [this, assetId, sourceUri, restored]() {
                m_importPending.remove(assetId);
                if (restored.path.isEmpty()) {
                    qWarning("import: cannot restore %s", qPrintable(sourceUri));
                    return;
                }
                repointAssetSource(assetId, restored.path);
            },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::repointAssetSource(const QString &assetId, const QString &path)
{
    const int index = indexOfId(assetId);
    drift::MediaAsset *asset = index < 0 ? nullptr : m_project->asset(assetId);
    if (!asset || asset->path == path)
        return;

    asset->path = path;
    // Clips keep their own copy of the source path and nothing else updates it, so the media
    // would still be missing everywhere it is actually played from.
    for (drift::Track &track : m_project->tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (clip.assetId == assetId)
                clip.path = path;
        }
    }

    snapshotAssets();
    emitAssetRowChanged(index, {PathRole});
    emit assetMetadataChanged(assetId);
    refreshMediaAt(index);
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
            [this, assetId, path, thumb, strip]() { applyThumbResult(assetId, path, thumb, strip); },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::applyThumbResult(const QString &assetId, const QString &sourcePath,
                                    const QString &thumb, const QString &strip)
{
    m_thumbPending.remove(assetId);
    if (!m_project)
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    // The source was replaced while this job ran, so these frames are of a file the row no
    // longer points at.
    if (!asset || asset->path != sourcePath)
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

    (void)QtConcurrent::run([this, assetId, absolutePath, imageOnly]() {
        const std::optional<drift::MediaAsset> probed = probeAsset(absolutePath, imageOnly);
        const drift::MediaAsset filled = probed.value_or(drift::MediaAsset{});
        const bool ok = probed.has_value();

        QMetaObject::invokeMethod(
            this,
            [this, assetId, filled, ok]() { applyImportResult(assetId, filled, ok); },
            Qt::QueuedConnection);
    });
}

bool AssetLibrary::startReplaceProbe(int index, const QString &absolutePath)
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset || absolutePath.isEmpty())
        return false;

    const QString assetId = asset->id;
    if (m_importPending.contains(assetId))
        return false;

    m_importPending.insert(assetId);
    const bool imageOnly = isImagePath(absolutePath);

    (void)QtConcurrent::run([this, assetId, absolutePath, imageOnly]() {
        const std::optional<drift::MediaAsset> probed = probeAsset(absolutePath, imageOnly);
        const drift::MediaAsset filled = probed.value_or(drift::MediaAsset{});
        const bool ok = probed.has_value();

        QMetaObject::invokeMethod(
            this,
            [this, assetId, filled, ok]() {
                m_importPending.remove(assetId);
                emit assetSourceProbed(assetId, filled, ok);
            },
            Qt::QueuedConnection);
    });
    return true;
}

bool AssetLibrary::applyProbedSource(const QString &assetId, const drift::MediaAsset &filled)
{
    if (!m_project)
        return false;

    const int index = indexOfId(assetId);
    drift::MediaAsset *asset = index < 0 ? nullptr : m_project->asset(assetId);
    if (!asset)
        return false;

    const QString id = asset->id;
    *asset = filled;
    asset->id = id;

    // Jobs still in flight were started against the old file. They drop themselves on landing
    // because the path they probed no longer matches; clearing the pending flags is what lets
    // the replacement start its own.
    m_thumbPending.remove(assetId);
    m_audioProbePending.remove(assetId);

    snapshotAssets();
    emitAssetRowChanged(index,
                        {NameRole, KindRole, DurationRole, DurationSecondsRole, PathRole,
                         ThumbnailPathRole, FilmstripPathRole});
    emit assetMetadataChanged(assetId);
    return true;
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
            [this, assetId, path, hasAudio, sampleRate, channels]() {
                applyAudioPresence(assetId, path, hasAudio, sampleRate, channels);
            },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::applyAudioPresence(const QString &assetId, const QString &sourcePath,
                                      bool hasAudio, int sampleRate, int channels)
{
    m_audioProbePending.remove(assetId);
    if (!m_project)
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    // Answered for a file the row no longer points at; the replacement brought its own.
    if (!asset || asset->path != sourcePath)
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

bool AssetLibrary::setAssetName(int index, const QString &name)
{
    drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset)
        return false;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || asset->name == trimmed)
        return false;

    asset->name = trimmed;
    emitAssetRowChanged(index, {NameRole});
    snapshotAssets();
    return true;
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

bool AssetLibrary::removeAssetAt(int index)
{
    if (!m_project || index < 0 || index >= m_project->assetOrder().size())
        return false;

    const QString assetId = m_project->assetIdAt(index);
    // The copy is dead weight the moment the row goes: nothing else on the device references it,
    // and a project saved earlier that does gets it back from the URI on load.
    const drift::MediaAsset *asset = assetAtIndex(index);
    const QString materialized = asset ? asset->path : QString();

    beginRemoveRows({}, index, index);
    m_project->assets().remove(assetId);
    m_project->assetOrder().removeAll(assetId);
    endRemoveRows();
    discardMaterializedCopy(materialized);

    // In-flight probe/thumb jobs already no-op when the id is gone; this just
    // keeps the pending sets from retaining ids nothing will ever clear.
    m_importPending.remove(assetId);
    m_thumbPending.remove(assetId);
    m_audioProbePending.remove(assetId);
    return true;
}

void AssetLibrary::clear()
{
    if (!m_project || m_project->assetOrder().isEmpty())
        return;

    QStringList materialized;
    for (const QString &id : m_project->assetOrder()) {
        if (const drift::MediaAsset *asset = m_project->asset(id))
            materialized.append(asset->path);
    }

    beginResetModel();
    m_project->assets().clear();
    m_project->assetOrder().clear();
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();
    endResetModel();

    for (const QString &path : materialized)
        discardMaterializedCopy(path);
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
        if (!asset->sourceUri.isEmpty())
            object.insert(QStringLiteral("sourceUri"), asset->sourceUri);
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
        asset.sourceUri = object.value(QStringLiteral("sourceUri")).toString();
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

    restoreMissingSources();
    for (int i = 0; i < m_project->assetOrder().size(); ++i)
        refreshMediaAt(i);
}

void AssetLibrary::importUrls(const QList<QUrl> &urls)
{
    QList<ImportSource> sources;
    sources.reserve(urls.size());
    for (const QUrl &url : urls) {
        if (url.isEmpty())
            continue;
        const ImportSource source = materializeImportUrl(url);
        if (source.path.isEmpty()) {
            qWarning("import: skipped unreadable URL %s", qPrintable(url.toString()));
            continue;
        }
        sources.append(source);
    }
    importFiles(sources);
}

void AssetLibrary::setImporting(bool importing)
{
    if (m_importing == importing)
        return;
    m_importing = importing;
    emit importingChanged();
}

bool AssetLibrary::importUrlsAsync(const QList<QUrl> &urls)
{
    // Neither case emits importFinished: a caller that never started an import must not
    // be told one finished, or a rejected call reads as "every file failed".
    if (urls.isEmpty())
        return false;
    // One import at a time — two concurrent runs would interleave their progress and both
    // call importFiles, and the UI has a single busy state to show for it.
    if (m_importing)
        return false;
    setImporting(true);

    (void)QtConcurrent::run([this, urls]() {
        QList<ImportSource> sources;
        sources.reserve(urls.size());
        int failed = 0;
        const int total = urls.size();
        for (int i = 0; i < total; ++i) {
            const QUrl &url = urls.at(i);
            ImportSource source;
            if (!url.isEmpty())
                source = materializeImportUrl(url);
            if (source.path.isEmpty()) {
                ++failed;
                qWarning("import: skipped unreadable URL %s", qPrintable(url.toString()));
            } else {
                sources.append(source);
            }
            const QString name =
                QFileInfo(source.path.isEmpty() ? url.fileName() : source.path).fileName();
            const int done = i + 1;
            QMetaObject::invokeMethod(
                this, [this, done, total, name]() { emit importProgress(done, total, name); },
                Qt::QueuedConnection);
        }

        // importFiles mutates the project and the model, so it only runs on the GUI thread.
        QMetaObject::invokeMethod(
            this,
            [this, sources, failed]() {
                importFiles(sources);
                setImporting(false);
                emit importFinished(sources.size(), failed);
            },
            Qt::QueuedConnection);
    });
    return true;
}

QString AssetLibrary::addGeneratedAsset(drift::MediaAsset asset)
{
    if (!m_project || asset.path.isEmpty())
        return {};

    if (asset.id.isEmpty())
        asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset.path = QFileInfo(asset.path).absoluteFilePath();

    const int row = m_project->assetOrder().size();
    beginInsertRows({}, row, row);
    const QString id = m_project->addAsset(asset);
    endInsertRows();
    return id;
}

void AssetLibrary::importFiles(const QList<ImportSource> &sources)
{
    if (!m_project)
        return;

    for (const ImportSource &source : sources) {
        const QFileInfo fileInfo(source.path);
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
        placeholder.sourceUri = source.sourceUri;
        placeholder.kind = provisionalKind(absolutePath);

        const int row = m_project->assetOrder().size();
        beginInsertRows({}, row, row);
        m_project->addAsset(placeholder);
        endInsertRows();

        startImportJob(placeholder.id, absolutePath, isImagePath(absolutePath));
    }
}
