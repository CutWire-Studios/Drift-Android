#pragma once

#include "core/MediaAsset.h"

#include <QAbstractListModel>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <QUrl>

namespace drift {
class Project;
}

// Media bin model backed by the project's asset table.
class AssetLibrary : public QAbstractListModel
{
    Q_OBJECT
    // Read-only row count, so QML can tell an empty bin from a populated one
    // (drives the empty state) and can detect imports that produced no asset.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        DurationRole,
        DurationSecondsRole,
        PathRole,
        ThumbnailPathRole,
        FilmstripPathRole,
    };
    Q_ENUM(Role)

    explicit AssetLibrary(QObject *parent = nullptr);

    void setProject(drift::Project *project);
    drift::Project *project() const { return m_project; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }

    Q_INVOKABLE void importUrls(const QList<QUrl> &urls);
    // Same work off the GUI thread. On Android every picked URL is a content:// URI and
    // materializeImportUrl copies the whole file out of the SAF stream before anything
    // is probed — seconds to minutes for a handful of 4K clips, which importUrls spends
    // frozen and well past the ANR watchdog. Progress and completion arrive as signals;
    // the model is only touched back on the GUI thread. Returns false when nothing was
    // started (no URLs, or an import is already running), so a caller can tell that apart
    // from an import that ran and produced nothing.
    Q_INVOKABLE bool importUrlsAsync(const QList<QUrl> &urls);
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    bool importing() const { return m_importing; }
    // Registers media the app rendered itself (freeze frames and the like). The asset is already
    // complete, so this skips the probe and thumbnail jobs the import path runs. Returns its id.
    QString addGeneratedAsset(drift::MediaAsset asset);
    Q_INVOKABLE QVariantMap assetAt(int index) const;
    Q_INVOKABLE QString assetIdAt(int index) const;
    Q_INVOKABLE int indexOfId(const QString &id) const;
    Q_INVOKABLE QString thumbnailAt(int index) const;
    Q_INVOKABLE QString filmstripAt(int index) const;
    Q_INVOKABLE void ensureMedia(int index);
    Q_INVOKABLE void ensureAllMedia();
    Q_INVOKABLE void sortByName();
    Q_INVOKABLE void sortByKind();
    // Display name in the media bin. Does not rename the file on disk.
    Q_INVOKABLE bool setAssetName(int index, const QString &name);
    int indexOfPath(const QString &path) const;
    // Drops the row from the project's asset table. Callers own the undo
    // snapshot and the in-use check; this only touches the bin.
    bool removeAssetAt(int index);
    // Probes `absolutePath` off-thread and reports it back through assetSourceProbed without
    // touching the project, so the caller can apply the swap, the clip fixups and the undo
    // snapshot as one transaction. Returns false when nothing was started.
    bool startReplaceProbe(int index, const QString &absolutePath);
    // Writes a probed source over the asset at `assetId`, keeping the id. Keeping it is the
    // whole point: clips address their media through it, so they stay bound across the swap.
    // Callers own the undo snapshot and the clip fixups.
    bool applyProbedSource(const QString &assetId, const drift::MediaAsset &filled);
    // Re-reads the project after undo/redo has swapped it wholesale.
    void syncToProject();

    QJsonArray toJsonArray() const;
    void loadFromJsonArray(const QJsonArray &assets);
    void clear();

    // Fills hasAudio from MediaProbe off-thread when hasAudioKnown is false.
    void ensureAudioPresence(const QString &assetId);

signals:
    void countChanged();
    void importingChanged();
    // `done` files of `total` have been materialized; `name` is the one just finished.
    void importProgress(int done, int total, const QString &name);
    void importFinished(int materialized, int failed);
    // Fired when probe/thumb/audio metadata lands so unlink affordances can refresh.
    void assetMetadataChanged(const QString &assetId);
    // Result of startReplaceProbe. Nothing has been applied yet; the caller decides whether the
    // probed media is an acceptable stand-in and calls applyProbedSource if so.
    void assetSourceProbed(const QString &assetId, const drift::MediaAsset &filled, bool ok);

private:
    void importFiles(const QStringList &paths);
    void setImporting(bool importing);
    bool m_importing = false;

    bool containsPath(const QString &path) const;
    void refreshMediaAt(int index);
    void startImportJob(const QString &assetId, const QString &absolutePath, bool imageOnly);
    void startThumbJob(const QString &assetId);
    void applyImportResult(const QString &assetId, const drift::MediaAsset &filled, bool ok);
    // `sourcePath` is the file the job actually read. It is compared against the asset's current
    // path on landing so a result for media that has since been replaced is dropped.
    void applyThumbResult(const QString &assetId, const QString &sourcePath, const QString &thumb,
                          const QString &strip);
    void applyAudioPresence(const QString &assetId, const QString &sourcePath, bool hasAudio,
                            int sampleRate, int channels);
    void emitAssetRowChanged(int index, const QList<int> &roles);
    void snapshotAssets();
    QList<QString> currentPaths() const;
    const drift::MediaAsset *assetAtIndex(int index) const;
    drift::MediaAsset *assetAtIndex(int index);

    drift::Project *m_project = nullptr;
    // Asset order and per-row source paths as of the last change this model itself made, so
    // syncToProject() can tell an undone asset edit from every other undo. A replaced source
    // leaves the order alone and only moves a path, which is why both are tracked.
    QList<QString> m_syncedOrder;
    QList<QString> m_syncedPaths;
    QSet<QString> m_importPending;
    QSet<QString> m_thumbPending;
    QSet<QString> m_audioProbePending;
};
