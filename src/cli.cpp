#include "cli.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "compressor.h"
#include "editor.h"
#include "imageio.h"
#include "looks.h"

namespace {

const char *usage = R"(Diapo, a photo viewer for Omarchy.

  diapo [FILE or FOLDER]      open a photo, or a folder's first one
  diapo compress FILE...      write smaller copies beside the originals
      --format webp|avif|jxl|jpeg|png|same   (webp)
      --quality 1-100                        (80)
      --max PIXELS    longest side, 0 keeps the size (2560)
      --keep-metadata keep location and camera data
      --out FOLDER    write there instead
  diapo look FILE... --name NAME [--strength 0-100]
                              write copies with a film look
  diapo looks                 list the film looks

Commands take --json. They exit 0, or 1 with the reason on stderr.
)";

int fail(const QString &message) {
    QTextStream(stderr) << "diapo: " << message << Qt::endl;
    return 1;
}

void print(const QJsonDocument &json) { QTextStream(stdout) << json.toJson(QJsonDocument::Indented); }

} // namespace

bool cli::handles(const QStringList &arguments) {
    static const QStringList commands{"compress", "look", "looks", "--help", "-h", "--version", "-v"};
    return !arguments.isEmpty() && commands.contains(arguments.first());
}

int cli::run(const QStringList &arguments) {
    QTextStream out(stdout);
    const QString command = arguments.value(0);
    if (command == "--help" || command == "-h") {
        out << usage;
        return 0;
    }
    if (command == "--version" || command == "-v") {
        out << "diapo " << QCoreApplication::applicationVersion() << Qt::endl;
        return 0;
    }

    QStringList files;
    QHash<QString, QString> options;
    bool json = false;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument == "--json")
            json = true;
        else if (argument == "--keep-metadata")
            options.insert("keep-metadata", "1");
        else if (argument.startsWith("--")) {
            if (i + 1 >= arguments.size())
                return fail(argument + " needs a value");
            options.insert(argument.mid(2), arguments.at(++i));
        } else
            files << argument;
    }

    if (command == "looks") {
        QJsonArray list;
        for (const Look &look : looks::all()) {
            list << QJsonObject{{"name", look.name}, {"group", look.group}};
            if (!json)
                out << look.group << ": " << look.name << Qt::endl;
        }
        if (json)
            print(QJsonDocument(list));
        return 0;
    }
    if (files.isEmpty())
        return fail("no files given");

    QJsonArray written;
    if (command == "look") {
        EditState state;
        const Look *look = looks::find(options.value("name"));
        if (!look)
            return fail("no look called \"" + options.value("name") + "\"; `diapo looks` lists them");
        state.look = look->name;
        state.strength = qBound(0, options.value("strength", "100").toInt(), 100);
        for (const QString &file : files) {
            QString error;
            const QString copy = Editor::saveCopy(QFileInfo(file).absoluteFilePath(), state, &error);
            if (copy.isEmpty())
                return fail(file + ": " + error);
            written << QJsonObject{{"source", file}, {"path", copy}, {"look", look->name}};
            if (!json)
                out << copy << Qt::endl;
        }
    } else {
        CompressSettings settings;
        settings.format = options.value("format", settings.format).toLower();
        imageio::Format known;
        if (settings.format != "same" && !imageio::parseFormat(settings.format, &known))
            return fail("unknown format \"" + settings.format + "\"");
        settings.quality = qBound(1, options.value("quality", "80").toInt(), 100);
        settings.maxEdge = qMax(0, options.value("max", "2560").toInt());
        settings.strip = !options.contains("keep-metadata");
        for (const QString &file : files) {
            const QString path = QFileInfo(file).absoluteFilePath();
            QString error;
            const QImage image = imageio::load(path, 0, &error);
            if (image.isNull())
                return fail(file + ": " + error);
            const Compressed result = Compressor::compressImage(image, path, settings);
            const QString folder = options.value("out", QFileInfo(path).absolutePath());
            const QString target = Compressor::targetPath(folder, path, result.format);
            if (result.data.isEmpty() || !imageio::write(target, result.data, &error))
                return fail(file + ": " + (result.error.isEmpty() ? error : result.error));
            written << QJsonObject{{"source", file}, {"path", target}, {"bytes", result.data.size()},
                                   {"original_bytes", QFileInfo(path).size()}, {"width", result.size.width()},
                                   {"height", result.size.height()}, {"format", imageio::formatId(result.format)}};
            if (!json)
                out << target << Qt::endl;
        }
    }
    if (json)
        print(QJsonDocument(written));
    return 0;
}
