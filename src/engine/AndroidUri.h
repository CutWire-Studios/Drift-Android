#pragma once

#include <QFile>
#include <QString>
#include <QUrl>

#include <memory>

// Everything the user picks on Android arrives as a content:// URI from the Storage Access
// Framework, and QUrl::toLocalFile() returns an empty string for those — the pattern
// `url.toLocalFile()`, bail if empty` fails unconditionally on device. Route file-dialog URLs
// through here instead. On desktop every function collapses to plain local-file behaviour.
namespace AndroidUri {

bool isContentUri(const QUrl &url);

// A string QFile accepts: the local path for file:// URLs, and the fully encoded URI for
// content:// ones, which Qt's AndroidContentFileEngine knows how to open. Empty for anything
// else.
QString filePath(const QUrl &url);

// The real filesystem path, or empty when there is none — which is every content:// URI. This
// is the one callers need before handing a path to FFmpeg or any other non-Qt library.
QString localPath(const QUrl &url);

// Open and ready, or null when the URL cannot be opened. Writing truncates, and the document
// behind a content:// URI has to exist already: ACTION_CREATE_DOCUMENT has created it by the
// time its URI comes back from the dialog.
std::unique_ptr<QFile> openForRead(const QUrl &url);
std::unique_ptr<QFile> openForWrite(const QUrl &url);

// The provider's DISPLAY_NAME — the name the user knows the file by, as opposed to the opaque
// document id that is all the URI carries. An extension is appended from the document's MIME
// type when the provider's name has none, because kind detection downstream is suffix-driven.
// Falls back to the URL's own file name.
QString displayName(const QUrl &url);

// Upgrades the one-shot grant that came with the picker result into one that survives a
// restart, so a stored content:// URI is still readable tomorrow. False when the provider
// offered no persistable grant; the URI then simply expires with the process.
bool takePersistableReadPermission(const QUrl &url);

} // namespace AndroidUri
