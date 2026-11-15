#include "place.h"

#include "imageio.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

namespace {

const QStringList fileManagers{"folio", "nautilus", "org.gnome.files", "thunar", "dolphin", "nemo", "pcmanfm", "caja",
                               "cosmic-files", "io.elementary.files"};

QString quoted(const QString &text) { return "'" + QString(text).replace("'", "'\\''") + "'"; }

// Runs before there is an application object, so through the shell.
bool hyprctl(const QStringList &arguments, QByteArray *output) {
    QString command = "hyprctl";
    for (const QString &argument : arguments)
        command += " " + quoted(argument);
    FILE *pipe = popen((command + " 2>/dev/null").toLocal8Bit().constData(), "r");
    if (!pipe)
        return false;
    char buffer[4096];
    size_t got;
    while ((got = fread(buffer, 1, sizeof buffer, pipe)) > 0)
        output->append(buffer, qsizetype(got));
    return pclose(pipe) == 0;
}

struct Spot {
    int x, y, width, height; // on its monitor
    int globalX, globalY;    // on the whole desktop
    int workspace;
    QString running;         // a Diapo window that is already open
};

// The bar over the photo, which the window needs on top of the picture.
constexpr int barHeight = 60;

bool spotOverFileManager(const QString &appId, const QSize &photo, Spot *spot) {
    QByteArray out;
    if (!hyprctl({"-j", "clients"}, &out))
        return false;
    const QJsonArray clients = QJsonDocument::fromJson(out).array();
    QJsonObject active;
    // For tests: DIAPO_PLACE_OVER=<window address> stands in for the focused window.
    const QString forced = qEnvironmentVariable("DIAPO_PLACE_OVER");
    if (qEnvironmentVariableIsSet("DIAPO_PLACE_OVER") && forced.isEmpty())
        return false;
    spot->running.clear();
    for (const QJsonValue &value : clients) {
        const QJsonObject client = value.toObject();
        if (client.value("class").toString() == appId)
            spot->running = client.value("address").toString();
        if (forced.isEmpty() ? client.value("focusHistoryID").toInt(-1) == 0 : client.value("address").toString() == forced)
            active = client;
    }
    const QString windowClass = active.value("class").toString().toLower();
    bool fileManager = !forced.isEmpty();
    for (const QString &name : fileManagers)
        fileManager = fileManager || windowClass.contains(name);
    if (active.isEmpty() || !fileManager || active.value("fullscreen").toInt() != 0)
        return false;

    out.clear();
    if (!hyprctl({"-j", "monitors"}, &out))
        return false;
    for (const QJsonValue &value : QJsonDocument::fromJson(out).array()) {
        const QJsonObject monitor = value.toObject();
        if (monitor.value("id") != active.value("monitor"))
            continue;
        const double scale = monitor.value("scale").toDouble(1) > 0 ? monitor.value("scale").toDouble(1) : 1;
        const int monitorX = monitor.value("x").toInt(), monitorY = monitor.value("y").toInt();
        const int monitorWidth = int(monitor.value("width").toDouble() / scale);
        const int monitorHeight = int(monitor.value("height").toDouble() / scale);
        // Most of the file manager, in its middle, with Files still in view
        // around it: 86% of its width and 88% of its height. On a 1920×1080
        // laptop at 1.25 that is 1292×716 over a 1502×814 Files window, and
        // a 3:2 photo shows at ~984×656 with thin bands beside it. A small
        // file manager window still gets a usable viewer, kept on screen.
        const int filesX = active.value("at").toArray().at(0).toInt() - monitorX;
        const int filesY = active.value("at").toArray().at(1).toInt() - monitorY;
        const int filesWidth = active.value("size").toArray().at(0).toInt();
        const int filesHeight = active.value("size").toArray().at(1).toInt();
        const int boxWidth = qMin(qMax(filesWidth, 640), qMin(monitorWidth, qMax(640, qRound(filesWidth * 0.86))));
        const int boxHeight = qMin(qMax(filesHeight, 440), qMin(monitorHeight, qMax(440, qRound(filesHeight * 0.88))));
        spot->width = boxWidth;
        spot->height = boxHeight;
        // Within that, a portrait photo gets a window of its own shape, so it
        // doesn't sit between two wide empty bands, and a very wide one a
        // lower window. It never gets narrower than the bar's buttons need, and a photo smaller than the
        // box keeps the box, as it is shown at its own size in the middle.
        if (photo.isValid() && photo.width() > 0 && photo.height() > 0) {
            const double room = double(boxWidth) / qMax(1, boxHeight - barHeight);
            const double shape = double(photo.width()) / photo.height();
            // A landscape photo keeps the box's whole width, even with thin
            // bands beside it; a portrait or square one is narrowed to it.
            if (shape < room && shape < 1.0)
                spot->width = qMax(640, qRound((boxHeight - barHeight) * shape));
            else if (shape > room)
                spot->height = qMax(440, qRound(boxWidth / shape) + barHeight);
        }
        spot->x = qBound(0, filesX + (filesWidth - spot->width) / 2, qMax(0, monitorWidth - spot->width));
        spot->y = qBound(0, filesY + (filesHeight - spot->height) / 2, qMax(0, monitorHeight - spot->height));
        spot->globalX = monitorX + spot->x;
        spot->globalY = monitorY + spot->y;
        spot->workspace = active.value("workspace").toObject().value("id").toInt();
        return true;
    }
    return false;
}

bool dispatch(const QString &lua) {
    QByteArray out;
    return hyprctl({"dispatch", lua}, &out) && out.trimmed() == "ok";
}

// Lays the Diapo window that is already open in the middle of the file manager.
void moveOver(const Spot &spot) {
    const QString window = "window = \"address:" + spot.running + "\"";
    if (spot.workspace > 0)
        dispatch(QString("hl.dsp.window.move({ workspace = \"%1\", follow = false, %2 })").arg(spot.workspace).arg(window));
    dispatch("hl.dsp.window.float({ action = \"on\", " + window + " })");
    dispatch(QString("hl.dsp.window.resize({ x = %1, y = %2, %3 })").arg(spot.width).arg(spot.height).arg(window));
    dispatch(QString("hl.dsp.window.move({ x = %1, y = %2, %3 })").arg(spot.globalX).arg(spot.globalY).arg(window));
    if (!qEnvironmentVariableIsSet("DIAPO_PLACE_OVER"))
        dispatch("hl.dsp.focus({ " + window + " })");
}

} // namespace

void place::moveRunningOverFileManager(const QString &appId, const QString &photo) {
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        return;
    Spot spot;
    if (spotOverFileManager(appId, imageio::sizeOf(photo), &spot) && !spot.running.isEmpty())
        moveOver(spot);
}

bool place::overFileManager(const QString &appId, const QStringList &files) {
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") || qEnvironmentVariableIsSet("DIAPO_PLACED")
        || files.isEmpty())
        return false;
    Spot spot;
    if (!spotOverFileManager(appId, imageio::sizeOf(files.first()), &spot) || !spot.running.isEmpty())
        return false;
    const QString self = QFileInfo("/proc/self/exe").canonicalFilePath();
    if (self.isEmpty())
        return false;
    // Hyprland starts the new copy with its own environment, so Diapo's own
    // settings (a test instance, timings, a screen to open on) are passed on.
    QString command = "env DIAPO_PLACED=1";
    for (const char *name : {"DIAPO_INSTANCE", "DIAPO_DEBUG", "DIAPO_OPEN", "DIAPO_OPEN_AFTER"})
        if (qEnvironmentVariableIsSet(name))
            command += QString(" %1=%2").arg(name, quoted(qEnvironmentVariable(name)));
    command += " " + quoted(self);
    for (const QString &file : files)
        command += " " + quoted(QFileInfo(file).absoluteFilePath());

    // Named and special workspaces (id below 1) are left to Hyprland, which
    // opens on the active one. The test hook must not pull the view along.
    const QString silent = qEnvironmentVariableIsSet("DIAPO_PLACE_OVER") ? " silent" : "";
    const QString luaWorkspace = spot.workspace > 0 ? QString(", workspace = \"%1%2\"").arg(spot.workspace).arg(silent) : "";
    const QString classicWorkspace = spot.workspace > 0 ? QString("; workspace %1%2").arg(spot.workspace).arg(silent) : "";
    // Hyprland with a Lua config first, then the classic dispatcher.
    const QString lua = QString("hl.dsp.exec_cmd(\"%1\", { float = true, move = \"%2 %3\", size = \"%4 %5\"%6 })")
                            .arg(QString(command).replace("\\", "\\\\").replace("\"", "\\\""))
                            .arg(spot.x).arg(spot.y).arg(spot.width).arg(spot.height).arg(luaWorkspace);
    const QString classic = QString("[float; move %1 %2; size %3 %4%5] %6")
                                .arg(spot.x).arg(spot.y).arg(spot.width).arg(spot.height).arg(classicWorkspace, command);
    if (dispatch(lua))
        return true;
    QByteArray out;
    return hyprctl({"dispatch", "exec", classic}, &out) && out.trimmed() == "ok";
}
