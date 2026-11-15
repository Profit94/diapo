#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Reading and writing photos. Qt's own plugins read the common formats; HEIC
// and AVIF (libheif), JPEG XL (libjxl) and camera RAW (libraw) are read here.
namespace imageio {

enum class Format { Jpeg, Png, Webp, Avif, Jxl };

// Every extension Diapo opens, lower case, without the dot.
const QStringList &extensions();
bool isPhoto(const QString &path);
bool isRaw(const QString &path);
// A GIF, WebP or APNG with more than one frame.
bool isAnimated(const QString &path);

// The picture, upright. With maxEdge its longest side is at most that, which
// lets JPEG decode at a fraction of the cost. A null image comes with `error`.
QImage load(const QString &path, int maxEdge = 0, QString *error = nullptr);
// A small picture of the photo to show while it decodes: the preview a HEIC
// carries, else the one Files (or any file manager) saved in the desktop's
// thumbnail cache, if it is of the file as it is now. Null when neither is.
QImage thumbnail(const QString &path);
// True when the whole picture is always decoded, whatever size is asked for
// (HEIC, AVIF, JPEG XL, RAW): worth starting early and keeping.
bool decodesWhole(const QString &path);
// Its upright size without decoding it, or an invalid size.
QSize sizeOf(const QString &path);

QString formatId(Format format);        // "jpeg", "png", "webp", "avif", "jxl"
QString formatName(Format format);      // "JPEG", "PNG", "WebP", "AVIF", "JPEG XL"
QString formatExtension(Format format); // "jpg", "png", ...
bool parseFormat(const QString &id, Format *format);
// The format a file of this name would be written in, when Diapo writes it.
bool formatOfPath(const QString &path, Format *format);

// `exif` is a TIFF block from exifOf(), carried into the file when given.
QByteArray encode(const QImage &image, Format format, int quality, const QByteArray &exif = {},
                  QString *error = nullptr);
QImage decode(const QByteArray &data);

// The photo's EXIF as a bare TIFF block, its orientation set upright since
// load() has already turned the pixels. Empty when there is none.
QByteArray exifOf(const QString &path);
// What the camera recorded, as (id, value): camera, lens, taken, exposure,
// aperture, iso, focal.
QList<QPair<QString, QString>> cameraFacts(const QString &path);

// Written whole or not at all, never over another file's contents half way.
bool write(const QString &path, const QByteArray &data, QString *error = nullptr);
// folder/stem.ext, or stem-2.ext and so on when that is taken.
QString uniquePath(const QString &folder, const QString &stem, const QString &extension);

} // namespace imageio
