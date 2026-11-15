// Diapo — a photo viewer for Omarchy, with film looks, an editor and a compressor.

#include <QTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "backend.h"
#include "debug.h"
#include "cli.h"
#include "compressor.h"
#include "editor.h"
#include "imageio.h"
#include "images.h"
#include "place.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "translator.h"

namespace {

const char *version = "0.1.0";

QString socketPath() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath());
    // DIAPO_INSTANCE keeps a test run apart from the Diapo in use.
    const QString instance = qEnvironmentVariable("DIAPO_INSTANCE");
    return runtime + "/diapo" + (instance.isEmpty() ? QString() : "-" + instance) + ".sock";
}

// Diapo is one window: a photo opened while it runs goes to that window.
// Asked before there is an application object, so with a plain socket.
bool sendToRunning(const QStringList &files) {
    const QByteArray path = QFile::encodeName(socketPath());
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (size_t(path.size()) >= sizeof address.sun_path)
        return false;
    memcpy(address.sun_path, path.constData(), size_t(path.size()));
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    bool sent = false;
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0) {
        const QByteArray message = files.join('\n').toUtf8() + '\n';
        sent = write(fd, message.constData(), size_t(message.size())) == message.size();
    }
    close(fd);
    return sent;
}

} // namespace

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    // The commands for scripts and agents need no display.
    if (cli::handles(arguments)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("diapo");
        app.setApplicationVersion(version);
        return cli::run(arguments);
    }

    if (arguments.value(0) == "open")
        arguments.removeFirst();
    QStringList files;
    for (const QString &argument : arguments) {
        const QUrl url(argument);
        files << QFileInfo(url.isLocalFile() ? url.toLocalFile() : argument).absoluteFilePath();
    }
    if (sendToRunning(files)) {
        // Asked after the photo is on its way, while Files still has the focus.
        if (!files.isEmpty())
            place::moveRunningOverFileManager("diapo", files.first());
        return 0;
    }
    if (place::overFileManager("diapo", files))
        return 0;

    QGuiApplication app(argc, argv);
    mark("started");
    app.setApplicationName("diapo");
    app.setApplicationVersion(version);

    // Matches the window to diapo.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon. The compiled-in copy covers
    // a run from build/, and needs qt6-svg's icon engine.
    app.setDesktopFileName("diapo");
    app.setWindowIcon(QIcon::fromTheme("diapo", QIcon(":/diapo.svg")));

    // Omarchy points fontconfig's monospace at the font picked in its menu,
    // so asking for "monospace" follows the user's choice, as Files does.
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(13);
    app.setFont(font);
    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    // Testing: DIAPO_THEME_DIR=<dir with theme/colors.toml> reads that theme
    // instead of the current Omarchy one, so a light theme can be checked
    // without switching the desktop.
    Theme theme(qEnvironmentVariable("DIAPO_THEME_DIR"));
    PortalFilePicker picker;
    Backend backend(&picker);
    Editor editor;
    Compressor compressor(&picker);
    QObject::connect(&picker, &FilePicker::failed, &backend, &Backend::say);

    QQmlApplicationEngine engine;
    engine.addImageProvider("photo", new PhotoImages);
    engine.addImageProvider("made", new Images(false));
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.rootContext()->setContextProperty("compressor", &compressor);
    // For looking at one screen without the keyboard: DIAPO_OPEN=edit, looks or
    // compress, and DIAPO_OPEN_AFTER=<ms> to go there once the photo is up.
    engine.rootContext()->setContextProperty("startIn", qEnvironmentVariable("DIAPO_OPEN"));
    engine.rootContext()->setContextProperty("startAfter", qEnvironmentVariableIntValue("DIAPO_OPEN_AFTER"));
    if (!files.isEmpty()) {
        backend.open(files.first());
        // Photos that are always decoded whole start now, while the window
        // is still being built, rather than once it asks for them.
        if (!backend.path().isEmpty() && imageio::decodesWhole(backend.path()))
            Images::fetch(backend.path(), 0);
    }
    backend.mark("");
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    mark("window built");
    // With DIAPO_DEBUG, says whenever the interface thread was held up long
    // enough to drop frames.
    if (qEnvironmentVariableIsSet("DIAPO_DEBUG")) {
        auto *beat = new QTimer(&app);
        auto *since = new QElapsedTimer;
        since->start();
        QObject::connect(beat, &QTimer::timeout, &app, [since] {
            if (const qint64 gap = since->restart(); gap > 50)
                mark(QString("interface held up %1 ms").arg(gap));
        });
        beat->start(10);
    }
    // Testing: DIAPO_SIZE=<w>x<h> sizes the window, and DIAPO_SHOT=<file.png>
    // saves what it shows after DIAPO_SHOT_AFTER ms (default 2500), then quits.
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        const QStringList size = qEnvironmentVariable("DIAPO_SIZE").split('x');
        if (size.size() == 2)
            window->resize(size[0].toInt(), size[1].toInt());
        if (const QString shot = qEnvironmentVariable("DIAPO_SHOT"); !shot.isEmpty()) {
            const int after = qEnvironmentVariableIsSet("DIAPO_SHOT_AFTER") ? qEnvironmentVariableIntValue("DIAPO_SHOT_AFTER") : 2500;
            QTimer::singleShot(after, window, [window, shot] {
                window->grabWindow().save(shot);
                QCoreApplication::quit();
            });
        }
    }
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [] {
            static bool first = true;
            if (first)
                mark("first frame");
            first = false;
        });

    // Later launches hand their photo to this window and leave.
    QLocalServer server;
    QLocalServer::removeServer(socketPath());
    server.listen(socketPath());
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::disconnected, socket, [&, socket] {
                const QStringList sent = QString::fromUtf8(socket->readAll()).split('\n', Qt::SkipEmptyParts);
                socket->deleteLater();
                if (!sent.isEmpty()) {
                    if (imageio::decodesWhole(sent.first()))
                        Images::fetch(sent.first(), 0);
                    backend.open(sent.first());
                }
                if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
                    window->show();
                    window->requestActivate();
                }
            });
        }
    });

    return app.exec();
}
