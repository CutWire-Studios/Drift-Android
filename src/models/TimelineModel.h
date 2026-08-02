#pragma once

#include "core/Project.h"

#include <QAbstractListModel>

// One row per timeline track (video, audio, text).
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        MutedRole,
        HiddenRole,
        LockedRole,
        ClipCountRole,
    };
    Q_ENUM(Role)

    explicit TimelineModel(QObject *parent = nullptr);

    void setProject(drift::Project *project);
    void refresh();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    drift::Project *m_project = nullptr;
};
