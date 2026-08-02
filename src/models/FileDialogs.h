#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>

// QML-facing wrapper around QFileDialog so file pickers use the native
// platform dialog (xdg-desktop-portal on Linux/Flatpak; Android Storage Access
// Framework / ACTION_OPEN_DOCUMENT on Android) instead of the QtQuick.Dialogs
// QML fallback. On Android, selected URLs are content:// URIs — callers must
// materialize them to a real path before handing them to FFmpeg.
class FileDialogs : public QObject
{
    Q_OBJECT

public:
    explicit FileDialogs(QObject *parent = nullptr);

    Q_INVOKABLE QUrl openFile(const QString &title, const QStringList &nameFilters) const;
    Q_INVOKABLE QList<QUrl> openFiles(const QString &title, const QStringList &nameFilters) const;
    // `suffix` is appended to `suggestedName` for the picker's initial file name; the path the
    // dialog returns is used exactly as given.
    Q_INVOKABLE QUrl saveFile(const QString &title, const QStringList &nameFilters,
                              const QString &suggestedName = QString(),
                              const QString &suffix = QString()) const;
};
