#include "ReverseProxyCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace drift {

namespace {

QString indexPath()
{
    const QString dir = reverseCacheDir();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("index.json"));
}

} // namespace

ReverseProxyCache &ReverseProxyCache::instance()
{
    static ReverseProxyCache cache;
    return cache;
}

QString ReverseProxyCache::lookup(const QString &sourcePath, TimeUs srcIn, TimeUs srcOut,
                                  TimeUs *coverEndUs) const
{
    if (sourcePath.isEmpty())
        return {};

    const QFileInfo info(sourcePath);
    const QString key = info.absoluteFilePath();

    QMutexLocker lock(&m_mutex);
    const auto it = m_entries.constFind(key);
    if (it == m_entries.constEnd())
        return {};

    const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();
    const qint64 size = info.size();
    for (const Entry &entry : *it) {
        if (entry.sourceMtimeMs != mtimeMs || entry.sourceSize != size)
            continue;
        // Containment, not equality: trimming a reversed clip inward, splitting it, or pasting a
        // copy all stay inside the range that was rendered and keep hitting the proxy for free.
        if (entry.coverInUs > srcIn || entry.coverOutUs < srcOut)
            continue;
        if (coverEndUs)
            *coverEndUs = entry.coverOutUs;
        return entry.proxyPath;
    }
    return {};
}

void ReverseProxyCache::insert(const QString &sourcePath, TimeUs coverInUs, TimeUs coverOutUs,
                               const QString &proxyPath)
{
    if (sourcePath.isEmpty() || proxyPath.isEmpty() || coverOutUs <= coverInUs)
        return;

    const QFileInfo info(sourcePath);
    Entry entry;
    entry.proxyPath = proxyPath;
    entry.coverInUs = coverInUs;
    entry.coverOutUs = coverOutUs;
    entry.sourceMtimeMs = info.lastModified().toMSecsSinceEpoch();
    entry.sourceSize = info.size();

    {
        QMutexLocker lock(&m_mutex);
        QList<Entry> &list = m_entries[info.absoluteFilePath()];
        // A new render supersedes any older one it fully covers — keeping both would leave a large
        // file on disk that lookup can never prefer.
        for (int i = list.size() - 1; i >= 0; --i) {
            const Entry &old = list.at(i);
            if (old.sourceMtimeMs != entry.sourceMtimeMs || old.sourceSize != entry.sourceSize
                || (entry.coverInUs <= old.coverInUs && entry.coverOutUs >= old.coverOutUs)) {
                QFile::remove(old.proxyPath);
                list.removeAt(i);
            }
        }
        list.prepend(entry);
        saveLocked();
    }

    // Re-check the budget at the one moment the directory's total actually changes. sweep() takes
    // the mutex itself, and prunes oldest-first; the proxy just written is passed as protected
    // because a single render can exceed the whole budget on its own.
    sweep(kDefaultMaxBytes, proxyPath);
}

void ReverseProxyCache::saveLocked() const
{
    const QString path = indexPath();
    if (path.isEmpty())
        return;

    QJsonArray array;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        for (const Entry &entry : it.value()) {
            QJsonObject object;
            object[QStringLiteral("source")] = it.key();
            object[QStringLiteral("proxy")] = entry.proxyPath;
            object[QStringLiteral("coverInUs")] = double(entry.coverInUs);
            object[QStringLiteral("coverOutUs")] = double(entry.coverOutUs);
            object[QStringLiteral("sourceMtimeMs")] = double(entry.sourceMtimeMs);
            object[QStringLiteral("sourceSize")] = double(entry.sourceSize);
            array.append(object);
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void ReverseProxyCache::load()
{
    const QString path = indexPath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    file.close();

    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        const QString source = object[QStringLiteral("source")].toString();
        Entry entry;
        entry.proxyPath = object[QStringLiteral("proxy")].toString();
        entry.coverInUs = TimeUs(object[QStringLiteral("coverInUs")].toDouble());
        entry.coverOutUs = TimeUs(object[QStringLiteral("coverOutUs")].toDouble());
        entry.sourceMtimeMs = qint64(object[QStringLiteral("sourceMtimeMs")].toDouble());
        entry.sourceSize = qint64(object[QStringLiteral("sourceSize")].toDouble());
        if (source.isEmpty() || entry.proxyPath.isEmpty())
            continue;
        if (!QFile::exists(entry.proxyPath))
            continue;

        // The source may have been edited or replaced while the app was closed. Drop the proxy
        // now rather than letting sweep() carry dead bytes until the budget forces them out.
        const QFileInfo info(source);
        if (!info.exists() || info.lastModified().toMSecsSinceEpoch() != entry.sourceMtimeMs
            || info.size() != entry.sourceSize) {
            QFile::remove(entry.proxyPath);
            continue;
        }
        m_entries[source].append(entry);
    }
    saveLocked();
}

void ReverseProxyCache::sweep(qint64 maxBytes, const QString &protectPath)
{
    const QString dirPath = reverseCacheDir();
    if (dirPath.isEmpty())
        return;

    const QString protect =
        protectPath.isEmpty() ? QString() : QFileInfo(protectPath).absoluteFilePath();

    QDir dir(dirPath);
    for (const QFileInfo &partial :
         dir.entryInfoList({QStringLiteral("*.part")}, QDir::Files))
        QFile::remove(partial.absoluteFilePath());

    // Oldest first, so the prune below drops least-recently-written proxies.
    const QFileInfoList proxies =
        dir.entryInfoList({QStringLiteral("*.mp4")}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const QFileInfo &proxy : proxies)
        total += proxy.size();
    if (total <= maxBytes)
        return;

    QSet<QString> removed;
    for (const QFileInfo &proxy : proxies) {
        if (total <= maxBytes)
            break;
        // The proxy that triggered this sweep survives even when it alone busts the budget:
        // deleting it would drop the render the caller is about to hand back, and every later
        // toggle of reverse on that clip would re-run the same doomed encode.
        if (!protect.isEmpty() && proxy.absoluteFilePath() == protect)
            continue;
        const qint64 size = proxy.size();
        if (QFile::remove(proxy.absoluteFilePath())) {
            removed.insert(proxy.absoluteFilePath());
            total -= size;
        }
    }
    if (removed.isEmpty())
        return;

    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        QList<Entry> &list = it.value();
        for (int i = list.size() - 1; i >= 0; --i) {
            if (removed.contains(QFileInfo(list.at(i).proxyPath).absoluteFilePath()))
                list.removeAt(i);
        }
        if (list.isEmpty())
            it = m_entries.erase(it);
        else
            ++it;
    }
    saveLocked();
}

VideoRead resolveVideoRead(const Clip &clip, TimeUs timelineUs)
{
    const TimeUs sourceUs = clip.timelineToSourceUs(timelineUs);
    if (!clip.reverse || clip.type != ClipType::Video || clip.path.isEmpty())
        return {clip.path, sourceUs};

    TimeUs coverEndUs = 0;
    const QString proxy =
        ReverseProxyCache::instance().lookup(clip.path, clip.srcIn, clip.srcOut, &coverEndUs);
    if (proxy.isEmpty())
        return {clip.path, sourceUs};

    // The proxy holds [coverIn, coverOut] flipped end-for-end, so a source time maps to its
    // mirror. timelineToSourceUs already walked down from srcOut, and this undoes that walk —
    // the composite reads the proxy strictly forwards.
    return {proxy, coverEndUs - sourceUs};
}

QString videoReadPath(const Clip &clip)
{
    if (!clip.reverse || clip.type != ClipType::Video || clip.path.isEmpty())
        return clip.path;

    const QString proxy =
        ReverseProxyCache::instance().lookup(clip.path, clip.srcIn, clip.srcOut, nullptr);
    return proxy.isEmpty() ? clip.path : proxy;
}

QString reverseCacheDir()
{
#ifdef Q_OS_ANDROID
    // CacheLocation, not AppDataLocation: on Android AppDataLocation is the app's files dir, which
    // Settings reports as app data and which the platform's storage reclaim never touches — so the
    // user had no way to get these bytes back short of clearing the whole app. Proxies are exactly
    // what CacheLocation is for: a miss is not an error, lookup() just falls back to live decode.
    //
    // Memoized so the one-time legacy cleanup below runs once per process rather than per call.
    static const QString dir = [] {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (base.isEmpty())
            return QString();
        const QString path = QDir(base).filePath(QStringLiteral("reversed"));
        QDir().mkpath(path);
        // Proxies written by an older build sit in the files dir along with the index that names
        // them by absolute path. Moving the files would leave every one of those paths dangling —
        // orphans that only the byte budget would ever reclaim — so drop the tree instead. The
        // cost is re-rendering an already-reversed clip; nothing here is project content.
        const QString legacy = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!legacy.isEmpty())
            QDir(QDir(legacy).filePath(QStringLiteral("reversed"))).removeRecursively();
        return path;
    }();
    return dir;
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("reversed"));
    QDir().mkpath(dir);
    return dir;
#endif
}

QString newReversePath()
{
    const QString dir = reverseCacheDir();
    if (dir.isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir(dir).filePath(id + QStringLiteral(".mp4"));
}

} // namespace drift
