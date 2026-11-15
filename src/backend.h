#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

class FilePicker;

// The viewer: the photo on screen and its folder, stepped through in order.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY photoChanged)
    Q_PROPERTY(QString name READ name NOTIFY photoChanged)
    Q_PROPERTY(QUrl url READ url NOTIFY photoChanged)
    Q_PROPERTY(int token READ token NOTIFY photoChanged)
    Q_PROPERTY(bool animated READ animated NOTIFY photoChanged)
    Q_PROPERTY(bool slow READ slow NOTIFY photoChanged)
    Q_PROPERTY(QSize size READ size NOTIFY photoChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY photoChanged)
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY photoChanged)
    Q_PROPERTY(QVariantList details READ details NOTIFY photoChanged)
    Q_PROPERTY(int index READ index NOTIFY photoChanged)
    Q_PROPERTY(int count READ count NOTIFY photoChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    explicit Backend(FilePicker *picker = nullptr, QObject *parent = nullptr);

    QString path() const { return m_path; }
    QString name() const;
    QUrl url() const { return QUrl::fromLocalFile(m_path); }
    int token() const { return m_token; }
    bool animated() const { return m_animated; }
    // Decoded whole whatever its size (HEIC, AVIF, JPEG XL, RAW).
    bool slow() const { return m_slow; }
    QSize size() const { return m_size; }
    QString kind() const;
    qint64 bytes() const { return m_bytes; }
    QVariantList details() const { return m_details; }
    int index() const { return m_files.indexOf(m_path); }
    int count() const { return m_files.size(); }
    QString notice() const { return m_notice; }
    QStringList files() const { return m_files; }

    // A path or a file: URL; a folder opens its first photo.
    Q_INVOKABLE void open(const QString &pathOrUrl);
    Q_INVOKABLE void next() { step(1); }
    Q_INVOKABLE void previous() { step(-1); }
    Q_INVOKABLE void first() { show(m_files.value(0)); }
    Q_INVOKABLE void last() { show(m_files.value(m_files.size() - 1)); }
    Q_INVOKABLE void trash();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void reveal();
    Q_INVOKABLE void pickPhoto();
    Q_INVOKABLE void say(const QString &notice);
    // Decodes the photos either side of this one at the size it is shown, so
    // stepping to them is instant.
    Q_INVOKABLE void prefetchAround(int maxEdge);
    // With DIAPO_DEBUG set, prints how long after start something happened.
    Q_INVOKABLE void mark(const QString &what) const;
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    // The path with the home folder written as ~.
    Q_INVOKABLE QString tidyPath(const QString &path) const;

signals:
    void photoChanged();
    void noticeChanged();

private:
    void show(const QString &path);
    void step(int by);
    void listFolder();
    void describe();

    FilePicker *m_picker;
    QString m_path;
    QStringList m_files;
    int m_token = 0;
    int m_direction = 1; // the way the last step went
    bool m_animated = false;
    bool m_slow = false;
    QSize m_size;
    qint64 m_bytes = 0;
    QVariantList m_details;
    QString m_notice;
    QFileSystemWatcher m_watcher;
};
