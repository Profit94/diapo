#pragma once

#include <QEventLoopLocker>
#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent>
#include <memory>

// Runs `work` on the thread pool and `done` with its result back on the
// owner's thread. Closing the last window quits the event loop under whatever
// is still running, so each job keeps the app alive until it is done.
template <typename Result, typename Work, typename Done>
void inBackground(QObject *owner, Work work, Done done) {
    auto *watcher = new QFutureWatcher<Result>(owner);
    auto locker = std::make_shared<QEventLoopLocker>();
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, owner, [watcher, locker, done] {
        const Result result = watcher->result();
        watcher->deleteLater();
        done(result);
    });
    watcher->setFuture(QtConcurrent::run(work));
}
