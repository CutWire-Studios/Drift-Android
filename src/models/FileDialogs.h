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
    // dialog returns is used exactly as given. `initialDirectory` opens the picker in that folder
    // when it exists (e.g. the last export location).
    Q_INVOKABLE QUrl saveFile(const QString &title, const QStringList &nameFilters,
                              const QString &suggestedName = QString(),
                              const QString &suffix = QString(),
                              const QString &initialDirectory = QString()) const;

    // Android share sheet (ACTION_SEND) for a content:// URI — a finished export as
    // Exporter::publishToGallery left it in the media library. `mimeType` is read from the
    // provider when empty. False on desktop, and for anything that is not a content:// URI:
    // the sheet can only hand another app a URI it is allowed to read.
    Q_INVOKABLE bool shareFile(const QUrl &url, const QString &mimeType = QString()) const;

    // The file the app was launched with (ACTION_VIEW on a .drift project from a file manager),
    // or an empty URL. Consumed by the first call: the activity keeps its launch intent for the
    // life of the process, so an unconsumed one would reopen the project on every check.
    Q_INVOKABLE QUrl takeLaunchUrl();
};
