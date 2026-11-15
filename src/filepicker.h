#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

// The desktop's own open and folder dialogs. Abstract so tests can stand in a
// fake; `purpose` comes back with the answer, so each asker knows its own.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void pickPhotos(const QString &purpose, bool multiple, const QString &folder) = 0;
    virtual void pickFolder(const QString &purpose, const QString &folder) = 0;

signals:
    void photosPicked(const QString &purpose, const QList<QUrl> &urls);
    void folderPicked(const QString &purpose, const QString &folder);
    void failed(const QString &message);
};
