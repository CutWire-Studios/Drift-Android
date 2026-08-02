#pragma once

#include "core/Project.h"

#include <QAbstractListModel>

// Clips on a single timeline track.
class ClipListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        PathRole,
        StartRole,
        DurationRole,
        InPointRole,
        OutPointRole,
        TextContentRole,
        ThumbnailPathRole,
        FilmstripPathRole,
        AssetIdRole,
    };
    Q_ENUM(Role)

    explicit ClipListModel(QObject *parent = nullptr);

    void setProject(drift::Project *project);
    void setTrackIndex(int trackIndex);
    int trackIndex() const { return m_trackIndex; }
    void refresh();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    drift::Project *m_project = nullptr;
    int m_trackIndex = 0;
};
