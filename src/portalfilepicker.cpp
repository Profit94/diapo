// Follows Omacut's portal file picker (MIT, David Heinemeier Hansson).
#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QRandomGenerator>

#include "imageio.h"

namespace {
struct PortalFilterRule {
    uint type;
    QString pattern;
};
using PortalFilterRules = QList<PortalFilterRule>;

struct PortalFileFilter {
    QString name;
    PortalFilterRules rules;
};
using PortalFileFilters = QList<PortalFileFilter>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule) {
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule) {
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFileFilter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFileFilter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

void registerPortalFilterTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalFilterRule>();
        qDBusRegisterMetaType<PortalFilterRules>();
        qDBusRegisterMetaType<PortalFileFilter>();
        qDBusRegisterMetaType<PortalFileFilters>();
        return true;
    }();
    Q_UNUSED(registered);
}

QByteArray portalPathBytes(const QString &path) {
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}
} // namespace

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) { registerPortalFilterTypes(); }

void PortalFilePicker::pickPhotos(const QString &purpose, bool multiple, const QString &folder) {
    PortalFileFilter photos{tr("Photos"), {}};
    for (const QString &extension : imageio::extensions()) {
        photos.rules.append({0, "*." + extension});
        photos.rules.append({0, "*." + extension.toUpper()});
    }
    QVariantMap options;
    options.insert("accept_label", tr("Open"));
    options.insert("modal", true);
    options.insert("multiple", multiple);
    options.insert("current_folder", portalPathBytes(QDir(folder).exists() ? folder : QDir::homePath()));
    options.insert("filters", QVariant::fromValue(PortalFileFilters{photos, {tr("All files"), {{0, "*"}}}}));
    options.insert("current_filter", QVariant::fromValue(photos));
    requestFile(tr("Open photos"), options, Action::Photos, purpose);
}

void PortalFilePicker::pickFolder(const QString &purpose, const QString &folder) {
    QVariantMap options;
    options.insert("accept_label", tr("Choose"));
    options.insert("modal", true);
    options.insert("directory", true);
    options.insert("current_folder", portalPathBytes(QDir(folder).exists() ? folder : QDir::homePath()));
    requestFile(tr("Choose a folder"), options, Action::Folder, purpose);
}

bool PortalFilePicker::connectToRequestPath(const QString &path) {
    m_pendingPath = path;
    return QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", m_pendingPath,
                                                 "org.freedesktop.portal.Request", "Response", this,
                                                 SLOT(handleResponse(uint,QVariantMap)));
}

void PortalFilePicker::closePendingRequest() {
    if (m_pendingPath.isEmpty())
        return;
    QDBusInterface request("org.freedesktop.portal.Desktop", m_pendingPath, "org.freedesktop.portal.Request",
                           QDBusConnection::sessionBus());
    request.asyncCall("Close");
}

void PortalFilePicker::requestFile(const QString &title, QVariantMap options, Action action, const QString &purpose) {
    // A request whose Response never arrives (the portal crashed or was
    // killed with the dialog up) must not wedge every later dialog behind
    // it: close the stuck one best-effort and replace it.
    if (m_pendingAction != Action::None) {
        closePendingRequest();
        clearPending();
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.FileChooser", bus);
    if (!portal.isValid()) {
        emit failed(tr("The desktop's file chooser is not available."));
        return;
    }

    // Subscribe to the Response signal at the request path the portal will
    // derive from our handle_token *before* making the call, so a response
    // can't slip past while our match rule is still being installed.
    const QString token = QString("diapo_%1").arg(QRandomGenerator::global()->generate());
    options.insert("handle_token", token);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString predictedPath = QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    m_pendingAction = action;
    m_pendingPurpose = purpose;
    if (!connectToRequestPath(predictedPath)) {
        clearPending();
        emit failed(tr("Could not listen for the file chooser's answer."));
        return;
    }

    const int generation = ++m_pendingGeneration;
    auto *watcher = new QDBusPendingCallWatcher(portal.asyncCall("OpenFile", QString(), title, options), this);
    m_pendingWatcher = watcher;

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_pendingWatcher == watcher)
            m_pendingWatcher = nullptr;

        // Superseded by a newer request, or a response may already have
        // been handled while the reply was in flight.
        if (generation != m_pendingGeneration || m_pendingAction == Action::None)
            return;

        if (reply.isError()) {
            clearPending();
            emit failed(tr("The file chooser failed: %1").arg(reply.error().message()));
            return;
        }

        // Old portal versions can hand back a different request path than the
        // handle_token predicts; move our subscription over if so.
        const QString actualPath = reply.value().path();
        if (actualPath != m_pendingPath) {
            QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_pendingPath,
                                                     "org.freedesktop.portal.Request", "Response", this,
                                                     SLOT(handleResponse(uint,QVariantMap)));
            if (!connectToRequestPath(actualPath)) {
                clearPending();
                emit failed(tr("Could not listen for the file chooser's answer."));
            }
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    const Action action = m_pendingAction;
    const QString purpose = m_pendingPurpose;
    clearPending();

    if (response != 0)
        return;
    QList<QUrl> urls;
    for (const QString &uri : results.value("uris").toStringList())
        urls << QUrl(uri);
    if (urls.isEmpty())
        return;
    if (action == Action::Photos)
        emit photosPicked(purpose, urls);
    else if (action == Action::Folder)
        emit folderPicked(purpose, urls.first().toLocalFile());
}

void PortalFilePicker::clearPending() {
    // A caller that never gets its reply must not outlive the request it
    // belonged to (repeated stuck requests would otherwise pile them up).
    if (m_pendingWatcher) {
        m_pendingWatcher->deleteLater();
        m_pendingWatcher = nullptr;
    }
    if (!m_pendingPath.isEmpty())
        QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_pendingPath,
                                                 "org.freedesktop.portal.Request", "Response", this,
                                                 SLOT(handleResponse(uint,QVariantMap)));
    m_pendingPath.clear();
    m_pendingPurpose.clear();
    m_pendingAction = Action::None;
}
