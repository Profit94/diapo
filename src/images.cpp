#include "images.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QtConcurrent>
#include <functional>
#include <atomic>
#include <memory>

#include "debug.h"
#include "imageio.h"
#include "looks.h"

namespace {
QMutex lock;
QHash<QString, QImage> &memory() {
    static QHash<QString, QImage> images;
    return images;
}
QHash<int, QString> &paths() {
    static QHash<int, QString> tokens;
    return tokens;
}
int lastToken = 0;

struct Decoded {
    QString key; // path, size and time, so a rewritten file is decoded again
    int edge;    // 0 for full size
    QFuture<QImage> image;
    // Cleared when the entry is let go: a decode that has not started by
    // then is skipped, so stepping quickly past photos doesn't queue up
    // seconds of work for pictures nobody will see.
    std::shared_ptr<std::atomic_bool> wanted;
    std::shared_ptr<std::atomic_bool> running;
};

// Large decodes, two at a time: each can use every core on its own (a HEIC's
// tiles), and more at once only slows the one on screen.
QThreadPool &decodePool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(2);
        return p;
    }();
    return *pool;
}

// Stand-ins are small and quick; on their own threads they never wait
// behind a large decode.
QThreadPool &thumbPool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(2);
        return p;
    }();
    return *pool;
}
QList<Decoded> &decoded() {
    static QList<Decoded> list; // most recent first
    return list;
}

QString fileKey(const QString &path) {
    const QFileInfo info(path);
    return path + '|' + QString::number(info.size()) + '|' + QString::number(info.lastModified().toMSecsSinceEpoch());
}

// Five screen-size pictures and one at full size is plenty: the one on show,
// two ahead and one behind, and at most ~100 MB of a large photo kept whole.
void trim() {
    int screen = 0, whole = 0;
    for (auto it = decoded().begin(); it != decoded().end();) {
        const bool full = it->edge == 0;
        if ((full ? ++whole : ++screen) > (full ? 1 : 5)) {
            *it->wanted = false;
            it = decoded().erase(it);
        } else {
            ++it;
        }
    }
}
} // namespace

Images::Images(bool fromDisk)
    : QQuickImageProvider(QQuickImageProvider::Image,
                          fromDisk ? QQmlImageProviderBase::ForceAsynchronousImageLoading : QQmlImageProviderBase::Flags()) {}

QImage Images::requestImage(const QString &id, QSize *size, const QSize &) {
    QImage image;
    if (id.startsWith("file/")) {
        QString path;
        {
            QMutexLocker locker(&lock);
            path = paths().value(id.section('/', 2, 2).toInt());
        }
        if (!path.isEmpty()) {
            const int edge = id.section('/', 1, 1).toInt();
            image = fetch(path, edge).result();
            // Skipped as unwanted just before it was asked for again.
            if (image.isNull())
                image = fetch(path, edge).result();
            // A picture kept whole serves any smaller size at once.
            if (edge > 0 && qMax(image.width(), image.height()) > edge)
                image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    } else if (id.startsWith("thumb/")) {
        QString path;
        {
            QMutexLocker locker(&lock);
            path = paths().value(id.section('/', 1, 1).toInt());
        }
        if (!path.isEmpty())
            image = imageio::thumbnail(path);
        // No stand-in: a single clear pixel, which the view knows to ignore,
        // rather than nothing, which Qt reports as an error.
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_RGBA8888);
            image.fill(Qt::transparent);
        }
    } else if (id.startsWith("mem/")) {
        image = get(id.section('/', 2));
    }
    if (size)
        *size = image.size();
    return image;
}

void Images::put(const QString &key, const QImage &image) {
    QMutexLocker locker(&lock);
    memory().insert(key, image);
}

QImage Images::get(const QString &key) {
    QMutexLocker locker(&lock);
    return memory().value(key);
}

void Images::drop(const QString &prefix) {
    QMutexLocker locker(&lock);
    for (auto it = memory().begin(); it != memory().end();)
        it = it.key().startsWith(prefix) ? memory().erase(it) : std::next(it);
}

int Images::tokenFor(const QString &path) {
    QMutexLocker locker(&lock);
    // Only the photo on screen and its neighbours are ever asked for.
    if (paths().size() > 64)
        paths().clear();
    paths().insert(++lastToken, path);
    return lastToken;
}

namespace {
QFuture<QImage> start(const QString &path, int maxEdge, bool keepWhole) {
    const QString key = fileKey(path);
    QMutexLocker locker(&lock);
    // The photo on show comes first: anything still waiting to decode for
    // another photo is dropped, so it starts at once rather than in line.
    if (keepWhole)
        for (auto it = decoded().begin(); it != decoded().end();) {
            if (it->key != key && !*it->running && !it->image.isFinished()) {
                *it->wanted = false;
                it = decoded().erase(it);
            } else {
                ++it;
            }
        }
    for (int i = 0; i < decoded().size(); ++i) {
        const Decoded &entry = decoded().at(i);
        // The same size or larger, or the whole picture: any smaller size
        // is cut from it rather than decoded again.
        if (entry.key == key && (entry.edge == 0 || (maxEdge > 0 && entry.edge >= maxEdge))) {
            const Decoded found = entry;
            decoded().move(i, 0);
            return found.image;
        }
    }
    // Formats that are always decoded whole are kept whole for the photo on
    // show, so zooming in and a window of another size cost nothing more.
    const bool whole = imageio::decodesWhole(path);
    const int edge = whole && keepWhole ? 0 : maxEdge;
    auto wanted = std::make_shared<std::atomic_bool>(true);
    auto running = std::make_shared<std::atomic_bool>(false);
    const QFuture<QImage> image = QtConcurrent::run(&decodePool(), [path, edge, whole, wanted, running] {
        if (!*wanted)
            return QImage();
        *running = true;
        QElapsedTimer took;
        took.start();
        // "Whole" stops at 8192 px: a 113 MP photo in full is ~450 MB and
        // seconds to decode, far past what zooming in can show.
        const int most = edge > 0 ? edge : 8192;
        QImage image = whole ? looks::fit(imageio::load(path), most) : imageio::load(path, most);
        mark(QString("decoded %1 at %2 px in %3 ms").arg(QFileInfo(path).fileName()).arg(edge).arg(took.elapsed()));
        return image;
    });
    decoded().prepend({key, edge, image, wanted, running});
    trim();
    return image;
}
} // namespace

QFuture<QImage> Images::fetch(const QString &path, int maxEdge) { return start(path, maxEdge, true); }

QFuture<QImage> Images::prefetch(const QString &path, int maxEdge) { return start(path, maxEdge, false); }

QImage Images::ready(const QString &path) {
    const QString key = fileKey(path);
    QMutexLocker locker(&lock);
    QImage best;
    for (const Decoded &entry : decoded())
        if (entry.key == key && entry.image.isFinished()) {
            const QImage image = entry.image.result();
            if (image.width() * qint64(image.height()) > best.width() * qint64(best.height()))
                best = image;
        }
    return best;
}

namespace {

// One answer to QML, finished from whichever thread its picture comes from.
class PhotoResponse : public QQuickImageResponse {
public:
    struct State {
        QMutex lock;
        PhotoResponse *owner = nullptr;
    };

    PhotoResponse() : m_state(std::make_shared<State>()) { m_state->owner = this; }
    ~PhotoResponse() override {
        QMutexLocker locker(&m_state->lock);
        m_state->owner = nullptr;
    }

    QQuickTextureFactory *textureFactory() const override {
        QMutexLocker locker(&m_imageLock);
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    // Finishes with the picture once `future` has it, unless QML has let the
    // request go by then. Nothing waits: the rest runs in the thread that
    // completes the future.
    void finishWhen(QFuture<QImage> future, std::function<QImage(const QImage &)> then) {
        std::shared_ptr<State> state = m_state;
        future.then(QtFuture::Launch::Sync, [state, then](const QImage &got) {
            const QImage image = then(got);
            QMutexLocker locker(&state->lock);
            if (PhotoResponse *owner = state->owner) {
                {
                    QMutexLocker imageLocker(&owner->m_imageLock);
                    owner->m_image = image;
                }
                emit owner->finished();
            }
        });
    }

private:
    std::shared_ptr<State> m_state;
    mutable QMutex m_imageLock;
    QImage m_image;
};

QString pathForToken(int token) {
    QMutexLocker locker(&lock);
    return paths().value(token);
}

} // namespace

QQuickImageResponse *PhotoImages::requestImageResponse(const QString &id, const QSize &) {
    auto *response = new PhotoResponse;
    if (id.startsWith("thumb/")) {
        const QString path = pathForToken(id.section('/', 1, 1).toInt());
        response->finishWhen(QtConcurrent::run(&thumbPool(), [path] { return path.isEmpty() ? QImage() : imageio::thumbnail(path); }),
                             [](const QImage &found) {
            // No stand-in: a single clear pixel, which the view knows to
            // ignore, rather than nothing, which Qt reports as an error.
            if (!found.isNull())
                return found;
            QImage clear(1, 1, QImage::Format_RGBA8888);
            clear.fill(Qt::transparent);
            return clear;
        });
        return response;
    }
    const QString path = pathForToken(id.section('/', 2, 2).toInt());
    const int edge = id.section('/', 1, 1).toInt();
    if (path.isEmpty()) {
        response->finishWhen(QtFuture::makeReadyValueFuture(QImage()), [](const QImage &) {
            QImage clear(1, 1, QImage::Format_RGBA8888);
            clear.fill(Qt::transparent);
            return clear;
        });
        return response;
    }
    // A picture kept whole serves any smaller size at once.
    const auto sized = [edge](const QImage &image) {
        return edge > 0 && qMax(image.width(), image.height()) > edge
                   ? image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   : image;
    };
    response->finishWhen(Images::fetch(path, edge), sized);
    return response;
}
