#include "compressor.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "background.h"
#include "debug.h"
#include "filepicker.h"
#include "images.h"
#include "looks.h"

namespace {
// Small, Balanced, Best: quality and longest side.
const int presets[3][2] = {{60, 1920}, {80, 2560}, {92, 0}};
}

Compressor::Compressor(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker) {
    // A slider being dragged asks for one estimate when it rests, not a
    // hundred on the way.
    m_settle.setSingleShot(true);
    m_settle.setInterval(160);
    connect(&m_settle, &QTimer::timeout, this, &Compressor::estimate);
    if (m_picker) {
        connect(m_picker, &FilePicker::photosPicked, this, [this](const QString &purpose, const QList<QUrl> &urls) {
            if (purpose == "compress")
                addUrls(urls);
        });
        connect(m_picker, &FilePicker::folderPicked, this, [this](const QString &purpose, const QString &folder) {
            if (purpose == "compress" && !folder.isEmpty()) {
                m_folder = folder;
                emit resultChanged();
            }
        });
    }
}

QVariantList Compressor::files() const {
    QVariantList list;
    for (const QString &path : m_paths) {
        qint64 result = -1;
        for (const Compressed &done : m_results)
            if (done.source == path)
                result = done.data.size();
        list << QVariantMap{{"name", QFileInfo(path).fileName()}, {"bytes", QFileInfo(path).size()}, {"result", result}};
    }
    return list;
}

qint64 Compressor::totalBytes() const {
    qint64 total = 0;
    for (const QString &path : m_paths)
        total += QFileInfo(path).size();
    return total;
}

int Compressor::preset() const {
    for (int i = 0; i < 3; ++i)
        if (m_settings.quality == presets[i][0] && m_settings.maxEdge == presets[i][1])
            return i;
    return -1;
}

qint64 Compressor::originalBytes() const { return m_sourcePath.isEmpty() ? 0 : QFileInfo(m_sourcePath).size(); }

qint64 Compressor::resultBytes() const {
    qint64 total = 0;
    for (const Compressed &done : m_results)
        total += done.data.size();
    return total;
}

qint64 Compressor::resultOriginalBytes() const {
    qint64 total = 0;
    for (const Compressed &done : m_results)
        total += QFileInfo(done.source).size();
    return total;
}

QString Compressor::resultFormat() const {
    return m_results.isEmpty() ? QString() : imageio::formatName(m_results.first().format);
}

QSize Compressor::resultSize() const { return m_results.isEmpty() ? QSize() : m_results.first().size; }

QString Compressor::suggestedName() const {
    if (m_results.isEmpty())
        return {};
    return QFileInfo(targetPath(m_folder, m_results.first().source, m_results.first().format)).fileName();
}

void Compressor::begin(const QStringList &paths) {
    end();
    m_active = true;
    emit activeChanged();
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls << QUrl::fromLocalFile(path);
    addUrls(urls);
}

void Compressor::end() {
    if (!m_active)
        return;
    ++m_session;
    m_active = false;
    m_paths.clear();
    m_selected = 0;
    m_source = {};
    m_sourcePath.clear();
    m_loading = m_estimating = m_estimateAgain = m_running = false;
    m_estimateBytes = -1;
    m_results.clear();
    m_settle.stop();
    Images::drop("cmp-");
    emit activeChanged();
    emit filesChanged();
    emit estimateChanged();
    emit runningChanged();
    emit resultChanged();
}

void Compressor::addUrls(const QList<QUrl> &urls) {
    const int before = m_paths.size();
    for (const QUrl &url : urls) {
        const QFileInfo info(url.toLocalFile());
        if (info.isFile() && imageio::isPhoto(info.filePath()) && !m_paths.contains(info.absoluteFilePath()))
            m_paths << info.absoluteFilePath();
    }
    if (m_paths.size() == before)
        return;
    clearResults();
    if (m_folder.isEmpty() || before == 0)
        m_folder = QFileInfo(m_paths.first()).absolutePath();
    emit filesChanged();
    emit resultChanged();
    if (before == 0)
        loadSelected();
}

void Compressor::addPhotos() {
    if (m_picker)
        m_picker->pickPhotos("compress", true, m_paths.isEmpty() ? QDir::homePath() + "/Pictures"
                                                                   : QFileInfo(m_paths.last()).absolutePath());
}

void Compressor::remove(int index) {
    if (index < 0 || index >= m_paths.size() || m_running)
        return;
    const bool shown = index == m_selected;
    m_paths.removeAt(index);
    if (m_selected > index || m_selected >= m_paths.size())
        m_selected = qMax(0, m_selected - 1);
    clearResults();
    emit filesChanged();
    if (shown)
        loadSelected();
}

void Compressor::select(int index) {
    if (index < 0 || index >= m_paths.size() || index == m_selected)
        return;
    m_selected = index;
    emit filesChanged();
    loadSelected();
}

void Compressor::loadSelected() {
    const QString path = m_paths.value(m_selected);
    m_source = {};
    m_sourcePath = path;
    m_estimateBytes = -1;
    m_estimateSize = {};
    m_loading = !path.isEmpty();
    m_estimate = {};
    if (!path.isEmpty()) {
        // What the viewer already shows stands in for the original while the
        // full-size photo decodes.
        const QImage shown = Images::ready(path);
        if (!shown.isNull()) {
            Images::put("cmp-a", looks::fit(shown, 2400));
            ++m_revision;
        }
    }
    emit estimateChanged();
    if (path.isEmpty())
        return;
    const int session = m_session;
    // Through the shared cache, so a HEIC the viewer decoded whole is reused,
    // and the full-size picture stays for the save that may follow.
    const QFuture<QImage> whole = Images::fetch(path, 0);
    inBackground<QImage>(this, [whole] { return whole.result(); }, [this, session, path](const QImage &image) {
        if (session != m_session || path != m_sourcePath)
            return;
        m_loading = false;
        if (image.isNull()) {
            emit failed(tr("%1 could not be opened.").arg(QFileInfo(path).fileName()));
            emit estimateChanged();
            return;
        }
        m_source = image;
        mark("compressor: photo decoded");
        Images::put("cmp-a", looks::fit(image, 2400));
        ++m_revision;
        emit estimateChanged();
        estimate();
    });
}

void Compressor::clearResults() {
    if (m_results.isEmpty())
        return;
    m_results.clear();
    emit resultChanged();
    emit filesChanged();
}

void Compressor::settingsMoved() {
    clearResults();
    m_estimate = {};
    emit settingsChanged();
    m_settle.start();
}

void Compressor::setPreset(int preset) {
    if (preset < 0 || preset > 2)
        return;
    m_settings.quality = presets[preset][0];
    m_settings.maxEdge = presets[preset][1];
    settingsMoved();
}

void Compressor::setFormat(const QString &format) {
    if (m_settings.format == format)
        return;
    m_settings.format = format;
    settingsMoved();
}

void Compressor::setQuality(int quality) {
    if (m_settings.quality == quality)
        return;
    m_settings.quality = qBound(1, quality, 100);
    settingsMoved();
}

void Compressor::setMaxEdge(int edge) {
    if (m_settings.maxEdge == edge)
        return;
    m_settings.maxEdge = qMax(0, edge);
    settingsMoved();
}

void Compressor::setStrip(bool strip) {
    if (m_settings.strip == strip)
        return;
    m_settings.strip = strip;
    settingsMoved();
}

imageio::Format Compressor::targetFormat(const QString &path, const CompressSettings &settings) {
    // "Same" keeps the original's format when Diapo writes it, and falls
    // back to JPEG for the ones it only reads (RAW, HEIC, TIFF).
    imageio::Format format = imageio::Format::Jpeg;
    if (!imageio::parseFormat(settings.format, &format))
        imageio::formatOfPath(path, &format);
    return format;
}

Compressed Compressor::compressImage(const QImage &source, const QString &path, const CompressSettings &settings) {
    Compressed result;
    result.source = path;
    result.format = targetFormat(path, settings);
    const QImage sized = looks::fit(source, settings.maxEdge);
    result.size = sized.size();
    result.data = imageio::encode(sized, result.format, settings.quality,
                                  settings.strip ? QByteArray() : imageio::exifOf(path), &result.error);
    return result;
}

QString Compressor::targetPath(const QString &folder, const QString &source, imageio::Format format) {
    const QFileInfo info(source);
    const QString extension = imageio::formatExtension(format);
    const QString plain = QDir(folder).filePath(info.completeBaseName() + "." + extension);
    if (!QFileInfo::exists(plain))
        return plain;
    return imageio::uniquePath(folder, info.completeBaseName() + "-small", extension);
}

void Compressor::estimate() {
    if (m_source.isNull() || m_running)
        return;
    if (m_estimating) {
        m_estimateAgain = true;
        return;
    }
    m_estimating = true;
    emit estimateChanged();
    const int session = m_session;
    struct Estimate {
        Compressed result;
        QImage preview;
    };
    inBackground<Estimate>(this, [source = m_source, path = m_sourcePath, settings = m_settings] {
        Estimate estimate;
        estimate.result = compressImage(source, path, settings);
        // What the saved file will look like, read back from its own bytes.
        estimate.preview = looks::fit(imageio::decode(estimate.result.data), 2400);
        return estimate;
    }, [this, session, settings = m_settings](const Estimate &made) {
        if (session != m_session)
            return;
        m_estimating = false;
        if (made.result.source == m_sourcePath) {
            if (made.result.data.isEmpty()) {
                m_estimateBytes = -1;
                emit failed(made.result.error);
            } else {
                m_estimateBytes = made.result.data.size();
                m_estimateSize = made.result.size;
                m_estimate = made.result;
                m_estimateSettings = settings;
                Images::put("cmp-b", made.preview);
                ++m_revision;
                mark("compressor: size estimated");
            }
        }
        emit estimateChanged();
        if (m_estimateAgain) {
            m_estimateAgain = false;
            this->estimate();
        }
    });
}

void Compressor::compress() {
    if (m_running || m_paths.isEmpty())
        return;
    // One photo whose size was just estimated with these very settings is
    // already compressed: the estimate is the result.
    if (m_paths.size() == 1 && !m_settle.isActive() && !m_estimating && m_estimate.source == m_paths.first()
        && !m_estimate.data.isEmpty() && m_estimateSettings == m_settings) {
        m_results = {m_estimate};
        emit resultChanged();
        emit filesChanged();
        return;
    }
    m_settle.stop();
    m_running = true;
    m_progress = 0;
    m_results.clear();
    emit runningChanged();
    emit resultChanged();
    const int session = m_session;
    inBackground<QList<Compressed>>(this, [this, session, paths = m_paths, settings = m_settings, shown = m_sourcePath,
                                           source = m_source] {
        QList<Compressed> results;
        for (int i = 0; i < paths.size(); ++i) {
            const QString &path = paths.at(i);
            QString error;
            const QImage image = path == shown && !source.isNull() ? source : imageio::load(path, 0, &error);
            Compressed result;
            if (image.isNull()) {
                result.source = path;
                result.error = error;
            } else {
                result = compressImage(image, path, settings);
            }
            results << result;
            const double progress = double(i + 1) / paths.size();
            QMetaObject::invokeMethod(this, [this, session, progress] {
                if (session == m_session) {
                    m_progress = progress;
                    emit runningChanged();
                }
            }, Qt::QueuedConnection);
        }
        return results;
    }, [this, session](const QList<Compressed> &results) {
        if (session != m_session)
            return;
        m_running = false;
        QStringList problems;
        for (const Compressed &result : results) {
            if (result.data.isEmpty())
                problems << QFileInfo(result.source).fileName();
            else
                m_results << result;
        }
        emit runningChanged();
        emit resultChanged();
        emit filesChanged();
        if (!problems.isEmpty())
            emit failed(tr("Could not compress %1.").arg(problems.join(", ")));
    });
}

void Compressor::chooseFolder() {
    if (m_picker)
        m_picker->pickFolder("compress", m_folder);
}

void Compressor::save(const QString &name) {
    if (m_results.isEmpty())
        return;
    QString first, error;
    int written = 0;
    for (const Compressed &result : m_results) {
        QString target = targetPath(m_folder, result.source, result.format);
        // A typed name is used as given, but never over a file that is there.
        if (m_results.size() == 1 && !name.trimmed().isEmpty() && name.trimmed() != QFileInfo(target).fileName()) {
            const QFileInfo typed(QFileInfo(name.trimmed()).fileName());
            const QString extension = imageio::formatExtension(result.format);
            const QString stem = typed.suffix().compare(extension, Qt::CaseInsensitive) == 0 ? typed.completeBaseName()
                                                                                            : typed.fileName();
            target = imageio::uniquePath(m_folder, stem, extension);
        }
        if (!imageio::write(target, result.data, &error))
            break;
        if (first.isEmpty())
            first = target;
        ++written;
    }
    if (written < m_results.size()) {
        emit failed(tr("Could not save to %1: %2").arg(m_folder, error));
        return;
    }
    m_results.clear();
    emit resultChanged();
    emit filesChanged();
    emit saved(first, written);
}
