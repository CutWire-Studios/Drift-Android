#include "ProjectCommands.h"

namespace drift {

ProjectSnapshotCommand::ProjectSnapshotCommand(Project *project, Project before, Project after,
                                               const QString &text)
    : QUndoCommand(text)
    , m_project(project)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void ProjectSnapshotCommand::undo()
{
    if (m_project)
        *m_project = m_before;
}

void ProjectSnapshotCommand::redo()
{
    if (m_project)
        *m_project = m_after;
}

} // namespace drift
