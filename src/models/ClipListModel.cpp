#include "ClipListModel.h"

#include "core/Clip.h"

ClipListModel::ClipListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ClipListModel::setProject(drift::Project *project)
{
    m_project = project;
    refresh();
}

void ClipListModel::setTrackIndex(int trackIndex)
{
    if (m_trackIndex == trackIndex)
        return;
    m_trackIndex = trackIndex;
    refresh();
}

void ClipListModel::refresh()
{
    beginResetModel();
    endResetModel();
}

int ClipListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_project)
        return 0;
    if (m_trackIndex < 0 || m_trackIndex >= m_project->tracks().size())
        return 0;
    return m_project->tracks().at(m_trackIndex).clips.size();
}

QVariant ClipListModel::data(const QModelIndex &index, int role) const
{
    if (!m_project || !index.isValid())
        return {};
    if (m_trackIndex < 0 || m_trackIndex >= m_project->tracks().size())
        return {};
    const drift::Track &track = m_project->tracks().at(m_trackIndex);
    if (index.row() < 0 || index.row() >= track.clips.size())
        return {};

    const drift::Clip &clip = track.clips.at(index.row());
    switch (role) {
    case IdRole:
        return clip.id;
    case NameRole:
        return clip.name;
    case KindRole:
        return drift::clipTypeToString(clip.type);
    case PathRole:
        return clip.path;
    case StartRole:
        return drift::usToSeconds(clip.timelineStart);
    case DurationRole:
        return drift::usToSeconds(clip.timelineDuration);
    case InPointRole:
        return drift::usToSeconds(clip.srcIn);
    case OutPointRole:
        return drift::usToSeconds(clip.srcOut);
    case TextContentRole:
        return clip.textContent;
    case ThumbnailPathRole:
        return clip.thumbnailPath;
    case FilmstripPathRole:
        return clip.filmstripPath;
    case AssetIdRole:
        return clip.assetId;
    default:
        return {};
    }
}

QHash<int, QByteArray> ClipListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {KindRole, "kind"},
        {PathRole, "path"},
        {StartRole, "start"},
        {DurationRole, "duration"},
        {InPointRole, "inPoint"},
        {OutPointRole, "outPoint"},
        {TextContentRole, "textContent"},
        {ThumbnailPathRole, "thumbnailPath"},
        {FilmstripPathRole, "filmstripPath"},
        {AssetIdRole, "assetId"},
    };
}
