#include "backend.h"

#include <QClipboard>
#include <QCollator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoopLocker>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLocale>
#include <QtConcurrent>
#include <memory>

#include "debug.h"
#include "filepicker.h"
#include "imageio.h"
#include "images.h"

Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker) {
    // The folder and the photo are followed as they change on disk: a new
    // file joins the sequence, a rewritten one is shown again.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        const QString before = m_path;
        const int at = index();
        listFolder();
        if (!m_files.contains(before))
            show(m_files.value(qMin(at, m_files.size() - 1)));
        else
            emit photoChanged();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &changed) {
        if (changed == m_path && QFileInfo::exists(m_path))
            show(m_path);
    });
    if (m_picker)
        connect(m_picker, &FilePicker::photosPicked, this, [this](const QString &purpose, const QList<QUrl> &urls) {
            if (purpose == "open" && !urls.isEmpty())
                open(urls.first().toLocalFile());
        });
}

QString Backend::name() const { return QFileInfo(m_path).fileName(); }

QString Backend::kind() const {
    const QString suffix = QFileInfo(m_path).suffix().toUpper();
    return suffix == "JPG" || suffix == "JPE" || suffix == "JFIF" ? "JPEG" : suffix == "TIF" ? "TIFF" : suffix;
}

void Backend::open(const QString &pathOrUrl) {
    const QUrl url(pathOrUrl);
    QFileInfo info(url.isLocalFile() ? url.toLocalFile() : pathOrUrl);
    if (!info.exists()) {
        say(tr("%1 is not there.").arg(info.fileName()));
        return;
    }
    if (info.isDir()) {
        m_path = info.absoluteFilePath() + "/";
        listFolder();
        if (m_files.isEmpty()) {
            m_path.clear();
            say(tr("No photos in %1.").arg(info.fileName()));
            emit photoChanged();
            return;
        }
        show(m_files.first());
        return;
    }
    show(info.absoluteFilePath());
}

void Backend::listFolder() {
    const QDir folder = QFileInfo(m_path).absoluteDir();
    QStringList names;
    for (const QString &entry : folder.entryList(QDir::Files))
        if (imageio::isPhoto(entry))
            names << entry;
    QCollator order;
    order.setNumericMode(true);
    order.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), order);
    m_files.clear();
    for (const QString &entry : names)
        m_files << folder.absoluteFilePath(entry);
    // A file opened by name stays in its sequence even with an unknown ending.
    if (!m_path.endsWith('/') && QFileInfo::exists(m_path) && !m_files.contains(m_path))
        m_files << m_path;
}

void Backend::show(const QString &path) {
    const QString folderBefore = QFileInfo(m_path).absolutePath();
    m_path = path;
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (path.isEmpty()) {
        m_files.clear();
        m_size = {};
        m_bytes = 0;
        m_details.clear();
        emit photoChanged();
        return;
    }
    const QString folder = QFileInfo(path).absolutePath();
    if (folder != folderBefore || m_files.isEmpty() || !m_files.contains(path)) {
        if (!m_watcher.directories().isEmpty())
            m_watcher.removePaths(m_watcher.directories());
        m_watcher.addPath(folder);
        listFolder();
    }
    m_watcher.addPath(path);
    m_token = Images::tokenFor(path);
    describe();
    ::mark("described " + QFileInfo(path).fileName());
    emit photoChanged();
}

void Backend::describe() {
    const QFileInfo info(m_path);
    m_bytes = info.size();
    m_size = imageio::sizeOf(m_path);
    m_animated = imageio::isAnimated(m_path);
    m_slow = imageio::decodesWhole(m_path);
    auto fact = [](const QString &id, const QString &value) { return QVariantMap{{"id", id}, {"value", value}}; };
    m_details.clear();
    m_details << fact("name", info.fileName()) << fact("folder", tidyPath(info.absolutePath()))
              << fact("kind", kind()) << fact("bytes", formatBytes(m_bytes));
    if (m_size.isValid())
        m_details << fact("pixels", QString("%1 × %2").arg(m_size.width()).arg(m_size.height()));
    m_details << fact("changed", QLocale().toString(info.lastModified(), QLocale::ShortFormat));
    for (const auto &entry : imageio::cameraFacts(m_path)) {
        QString value = entry.second;
        if (entry.first == "taken")
            value = QLocale().toString(QDateTime::fromString(value, Qt::ISODate), QLocale::ShortFormat);
        m_details << fact(entry.first, value);
    }
}

void Backend::step(int by) {
    if (m_files.size() < 2)
        return;
    ::mark("step");
    m_direction = by < 0 ? -1 : 1;
    const int at = index();
    show(m_files.at(((at < 0 ? 0 : at + by) + m_files.size()) % m_files.size()));
}

void Backend::trash() {
    if (m_path.isEmpty())
        return;
    const QString gone = m_path, shown = name();
    const int at = index();
    if (!QFile::moveToTrash(gone)) {
        say(tr("%1 could not be moved to the trash.").arg(shown));
        return;
    }
    m_files.removeAll(gone);
    show(m_files.value(qMin(at, m_files.size() - 1)));
    say(tr("%1 moved to the trash.").arg(shown));
}

void Backend::copy() {
    if (m_path.isEmpty())
        return;
    // Decoded off the GUI thread; the app stays up until it is on the clipboard.
    auto *watcher = new QFutureWatcher<QImage>(this);
    auto locker = std::make_shared<QEventLoopLocker>();
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, locker] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        if (image.isNull()) {
            say(tr("This photo could not be copied."));
            return;
        }
        QGuiApplication::clipboard()->setImage(image);
        say(tr("Copied."));
    });
    watcher->setFuture(QtConcurrent::run([path = m_path] { return imageio::load(path); }));
}

void Backend::reveal() {
    if (m_path.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                                       "org.freedesktop.FileManager1", "ShowItems");
    call << QStringList{url().toString()} << QString();
    QDBusConnection::sessionBus().asyncCall(call);
}

void Backend::pickPhoto() {
    if (m_picker)
        m_picker->pickPhotos("open", false, m_path.isEmpty() ? QDir::homePath() + "/Pictures"
                                                               : QFileInfo(m_path).absolutePath());
}

void Backend::prefetchAround(int maxEdge) {
    if (m_files.size() < 2 || maxEdge <= 0)
        return;
    // Two ahead in the direction of travel, then one behind, nearest first.
    const int at = index();
    const int count = m_files.size();
    QList<int> order{m_direction, 2 * m_direction, -m_direction};
    QStringList done{m_path};
    for (const int offset : order) {
        const QString &next = m_files.at(((at + offset) % count + count) % count);
        if (!done.contains(next)) {
            Images::prefetch(next, maxEdge);
            done << next;
        }
    }
}

void Backend::mark(const QString &what) const { ::mark(what); }

void Backend::say(const QString &notice) {
    m_notice = notice;
    emit noticeChanged();
}

QString Backend::formatBytes(qint64 bytes) const {
    return QLocale().formattedDataSize(bytes, bytes < 1000 * 1000 ? 0 : 1, QLocale::DataSizeSIFormat);
}

QString Backend::tidyPath(const QString &path) const {
    const QString home = QDir::homePath();
    return path == home ? "~" : path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}
