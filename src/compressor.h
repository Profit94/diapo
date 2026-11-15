#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "imageio.h"

class FilePicker;

struct CompressSettings {
    QString format = "webp"; // a format id, or "same" for the original's
    int quality = 80;
    int maxEdge = 2560;      // 0 keeps the size
    bool strip = true;       // leave location and camera data out
    bool operator==(const CompressSettings &o) const {
        return format == o.format && quality == o.quality && maxEdge == o.maxEdge && strip == o.strip;
    }
};

struct Compressed {
    QString source;
    QByteArray data;
    imageio::Format format = imageio::Format::Jpeg;
    QSize size;
    QString error;
};

// The compressor: a live estimate for the photo on show while the settings
// move, then every photo in the list made smaller in memory, and written only
// once the save popup is answered.
class Compressor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(int count READ count NOTIFY filesChanged)
    Q_PROPERTY(int selected READ selected NOTIFY filesChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY filesChanged)
    Q_PROPERTY(int preset READ preset NOTIFY settingsChanged)
    Q_PROPERTY(QString format READ format NOTIFY settingsChanged)
    Q_PROPERTY(int quality READ quality NOTIFY settingsChanged)
    Q_PROPERTY(int maxEdge READ maxEdge NOTIFY settingsChanged)
    Q_PROPERTY(bool strip READ strip NOTIFY settingsChanged)
    Q_PROPERTY(bool estimating READ estimating NOTIFY estimateChanged)
    Q_PROPERTY(qint64 estimateBytes READ estimateBytes NOTIFY estimateChanged)
    Q_PROPERTY(QSize estimateSize READ estimateSize NOTIFY estimateChanged)
    Q_PROPERTY(qint64 originalBytes READ originalBytes NOTIFY estimateChanged)
    Q_PROPERTY(QSize originalSize READ originalSize NOTIFY estimateChanged)
    Q_PROPERTY(int revision READ revision NOTIFY estimateChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY runningChanged)
    Q_PROPERTY(bool resultReady READ resultReady NOTIFY resultChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultChanged)
    Q_PROPERTY(qint64 resultBytes READ resultBytes NOTIFY resultChanged)
    Q_PROPERTY(qint64 resultOriginalBytes READ resultOriginalBytes NOTIFY resultChanged)
    Q_PROPERTY(QString resultFormat READ resultFormat NOTIFY resultChanged)
    Q_PROPERTY(QSize resultSize READ resultSize NOTIFY resultChanged)
    Q_PROPERTY(QString suggestedName READ suggestedName NOTIFY resultChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY resultChanged)

public:
    explicit Compressor(FilePicker *picker = nullptr, QObject *parent = nullptr);

    bool active() const { return m_active; }
    QVariantList files() const;
    int count() const { return m_paths.size(); }
    int selected() const { return m_selected; }
    qint64 totalBytes() const;
    int preset() const;
    QString format() const { return m_settings.format; }
    int quality() const { return m_settings.quality; }
    int maxEdge() const { return m_settings.maxEdge; }
    bool strip() const { return m_settings.strip; }
    bool estimating() const { return m_estimating || m_loading; }
    qint64 estimateBytes() const { return m_estimateBytes; }
    QSize estimateSize() const { return m_estimateSize; }
    qint64 originalBytes() const;
    QSize originalSize() const { return m_source.size(); }
    int revision() const { return m_revision; }
    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    bool resultReady() const { return !m_results.isEmpty(); }
    int resultCount() const { return m_results.size(); }
    qint64 resultBytes() const;
    qint64 resultOriginalBytes() const;
    QString resultFormat() const;
    QSize resultSize() const;
    QString suggestedName() const;
    QString folder() const { return m_folder; }

    Q_INVOKABLE void begin(const QStringList &paths);
    Q_INVOKABLE void end();
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void addPhotos();
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void setPreset(int preset);
    Q_INVOKABLE void setFormat(const QString &format);
    Q_INVOKABLE void setQuality(int quality);
    Q_INVOKABLE void setMaxEdge(int edge);
    Q_INVOKABLE void setStrip(bool strip);
    Q_INVOKABLE void compress();
    Q_INVOKABLE void chooseFolder();
    // Writes the results into the folder; `name` names a single one.
    Q_INVOKABLE void save(const QString &name);

    // One photo made smaller, for the app, the command line and the tests.
    static Compressed compressImage(const QImage &source, const QString &path, const CompressSettings &settings);
    static imageio::Format targetFormat(const QString &path, const CompressSettings &settings);
    // Where a result lands in `folder`: the photo's name with the new ending,
    // or with -small when that would be the original itself.
    static QString targetPath(const QString &folder, const QString &source, imageio::Format format);

signals:
    void activeChanged();
    void filesChanged();
    void settingsChanged();
    void estimateChanged();
    void runningChanged();
    void resultChanged();
    void saved(const QString &firstPath, int count);
    void failed(const QString &message);

private:
    void loadSelected();
    void settingsMoved();
    void estimate();
    void clearResults();

    FilePicker *m_picker;
    bool m_active = false;
    QStringList m_paths;
    int m_selected = 0;
    CompressSettings m_settings;
    QImage m_source;
    QString m_sourcePath;
    bool m_loading = false;
    bool m_estimating = false, m_estimateAgain = false;
    qint64 m_estimateBytes = -1;
    QSize m_estimateSize;
    int m_revision = 0;
    int m_session = 0;
    bool m_running = false;
    double m_progress = 0;
    QList<Compressed> m_results;
    // The last estimate for the photo on show, and what it was made with.
    Compressed m_estimate;
    CompressSettings m_estimateSettings;
    QString m_folder;
    QTimer m_settle;
};
