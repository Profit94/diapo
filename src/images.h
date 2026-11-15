#pragma once

#include <QFuture>
#include <QImage>
#include <QQuickImageProvider>
#include <QQuickImageResponse>

// Hands pictures to QML: a photo on disk, decoded at the size asked for
// (image://photo/file/<longest side>/<token>), or one the editor or
// compressor made in memory (image://made/mem/<revision>/<key>).
//
// Photos decoded from disk are kept for a moment, keyed by the file as it is
// now, so the one on show can start decoding before the window exists and
// its neighbours can be ready before they are stepped to.
class Images : public QQuickImageProvider {
public:
    // Photos on disk load off the GUI thread; pictures already in memory are
    // handed over at once, so a preview never blinks between two renders.
    explicit Images(bool fromDisk);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    static void put(const QString &key, const QImage &image);
    static QImage get(const QString &key);
    static void drop(const QString &prefix);
    // A number standing for this path in a URL, so no file name has to
    // survive being one. A new one each call, which also reloads a changed file.
    static int tokenFor(const QString &path);

    // Starts decoding a photo, at most maxEdge on its longest side (0 is
    // full size), unless that is already done or under way.
    static QFuture<QImage> fetch(const QString &path, int maxEdge);
    // The same for a photo that is not on show yet: one decoded whole is kept
    // only at the size asked for, so it never pushes the photo on show, kept
    // whole, out of memory.
    static QFuture<QImage> prefetch(const QString &path, int maxEdge);
    // The largest picture of this photo already decoded, or a null image;
    // never starts a decode. What the viewer shows is reused this way.
    static QImage ready(const QString &path);
};

// Photos from disk for QML (image://photo/...). Asynchronous, so each request
// waits for its own decode rather than in line: Qt reads pictures on a
// single thread, and a plain provider blocked it on one large photo while
// the next photo and its stand-in queued behind.
class PhotoImages : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
