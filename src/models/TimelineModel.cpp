#include "TimelineModel.h"

#include "core/Track.h"

TimelineModel::TimelineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void TimelineModel::setProject(drift::Project *project)
{
    m_project = project;
    refresh();
}

void TimelineModel::refresh()
{
    beginResetModel();
    endResetModel();
}

int TimelineModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_project)
        return 0;
    return m_project->tracks().size();
}

QVariant TimelineModel::data(const QModelIndex &index, int role) const
{
    if (!m_project || !index.isValid() || index.row() < 0 || index.row() >= m_project->tracks().size())
        return {};

    const drift::Track &track = m_project->tracks().at(index.row());
    switch (role) {
    case TypeRole:
        return drift::trackTypeToString(track.type);
    case MutedRole:
        return track.muted;
    case HiddenRole:
        return track.hidden;
    case LockedRole:
        return track.locked;
    case ClipCountRole:
        return track.clips.size();
    default:
        return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {
        {TypeRole, "type"},
        {MutedRole, "muted"},
        {HiddenRole, "hidden"},
        {LockedRole, "locked"},
        {ClipCountRole, "clipCount"},
    };
}
