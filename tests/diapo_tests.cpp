#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <QBuffer>
#include <QProcess>
#include <QPainter>
#include <QSignalSpy>

#include "backend.h"
#include "compressor.h"
#include "filepicker.h"
#include "editor.h"
#include "imageio.h"
#include "images.h"
#include "looks.h"
#include "theme.h"
#include "translator.h"

namespace {

bool write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

int lines(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
}

// A hyprctl that answers with response.json beside it and counts its calls.
QString fakeHyprctl(const QString &directory) {
    const QString command = directory + "/hyprctl";
    write(command, "#!/bin/bash\necho >> '" + directory.toUtf8() + "/calls'\ncat '" + directory.toUtf8()
                       + "/response.json'\n");
    QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return command;
}

QJsonObject readCatalog(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// The placeholders in a text, sorted: %1 to %99, and the count (%n, %Ln).
QStringList placeholders(const QString &text) {
    static const QRegularExpression placeholder("%L?(?:[1-9][0-9]?|n)");
    QStringList found;
    for (auto it = placeholder.globalMatch(text); it.hasNext();)
        found << it.next().captured();
    found.sort();
    return found;
}

// Every text the sources hand to qsTr() or tr(), and whether a call passes it
// a count. It walks the code a token at a time, so a string never hides a
// comment or the other way round, C++'s adjacent literals join, and QML's
// single quotes count. A text that isn't a literal can't be in a catalog.
QMap<QString, bool> translatables(const QString &folder, QStringList *problems) {
    QMap<QString, bool> found;
    for (const QFileInfo &info : QDir(folder).entryInfoList({"*.qml", "*.js", "*.cpp", "*.h"}, QDir::Files)) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString code = QString::fromUtf8(file.readAll());
        qsizetype i = 0;
        auto literal = [&](QString *text) {
            const QChar quote = code[i++];
            for (; i < code.size() && code[i] != quote; ++i) {
                if (code[i] == '\\' && i + 1 < code.size()) {
                    const QChar escaped = code[++i];
                    const int digits = escaped == 'u' ? 4 : escaped == 'x' ? 2 : 0;
                    bool hex = false;
                    const ushort unit = code.mid(i + 1, digits).toUShort(&hex, 16);
                    if (digits && hex) {
                        *text += QChar(unit);
                        i += digits;
                    } else {
                        *text += escaped == 'n' ? QChar('\n') : escaped == 't' ? QChar('\t') : escaped;
                    }
                } else {
                    *text += code[i];
                }
            }
            ++i;
        };
        auto skipSpace = [&] {
            while (i < code.size() && code[i].isSpace())
                ++i;
        };
        while (i < code.size()) {
            const QChar c = code[i];
            if (code.mid(i, 2) == "//") {
                i = code.indexOf('\n', i);
                if (i < 0)
                    break;
            } else if (code.mid(i, 2) == "/*") {
                i = code.indexOf("*/", i + 2);
                if (i < 0)
                    break;
                i += 2;
            } else if (c == '"' || c == '\'') {
                QString skipped;
                literal(&skipped);
            } else if (c.isLetter() || c == '_') {
                const qsizetype start = i;
                while (i < code.size() && (code[i].isLetterOrNumber() || code[i] == '_'))
                    ++i;
                const QString word = code.mid(start, i - start);
                skipSpace();
                if ((word != "qsTr" && word != "tr") || i >= code.size() || code[i] != '(')
                    continue;
                ++i;
                skipSpace();
                if (i >= code.size() || (code[i] != '"' && code[i] != '\'')) {
                    *problems << info.fileName() + ": not a literal: " + code.mid(start, 40);
                    continue;
                }
                QString text;
                do {
                    literal(&text);
                    skipSpace();
                } while (i < code.size() && code[i] == '"');
                // The arguments after the text, to the call's closing parenthesis.
                int depth = 0, commas = 0;
                while (i < code.size()) {
                    const QChar d = code[i];
                    if (d == '"' || d == '\'') {
                        QString skipped;
                        literal(&skipped);
                        continue;
                    }
                    if (d == '(' || d == '[' || d == '{')
                        ++depth;
                    else if ((d == ')' || d == ']' || d == '}') && depth-- == 0)
                        break;
                    else if (d == ',' && depth == 0)
                        ++commas;
                    ++i;
                }
                found[text] = found.value(text) || commas >= 2;
            } else {
                ++i;
            }
        }
    }
    return found;
}

// Finds an item by name through what is drawn, since a Repeater's items are
// its visual children and not its QObject ones.
// Stands in for the desktop's file chooser: remembers what was asked.
class FakePicker : public FilePicker {
public:
    QString askedPurpose, askedFolder;
    int asked = 0;
    void pickPhotos(const QString &, bool, const QString &) override {}
    void pickFolder(const QString &purpose, const QString &folder) override {
        askedPurpose = purpose;
        askedFolder = folder;
        ++asked;
    }
};

QQuickItem *drawn(QQuickItem *under, const QString &name) {
    if (under->objectName() == name)
        return under;
    for (QQuickItem *child : under->childItems())
        if (QQuickItem *found = drawn(child, name))
            return found;
    return nullptr;
}

// A made-up photo: a sky over a dark hill, so looks have colour to work on.
QImage sample(int width = 240, int height = 160) {
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, y > height * 0.7 ? QColor(40, 45, 70) : QColor(60 + 150 * x / width, 110, 200 - 100 * y / height));
    return image;
}

bool saveSample(const QString &path, int width = 240, int height = 160) {
    imageio::Format format = imageio::Format::Png;
    imageio::formatOfPath(path, &format);
    return imageio::write(path, imageio::encode(sample(width, height), format, 90));
}

// A TIFF block holding a camera maker and an orientation, as a camera writes.
QByteArray exifBlock(quint16 orientation) {
    QByteArray block("II*\0\x08\0\0\0", 8);
    auto u16 = [&](quint16 v) { block += char(v & 0xff); block += char(v >> 8); };
    auto u32 = [&](quint32 v) { u16(v & 0xffff); u16(v >> 16); };
    u16(2);
    u16(0x010f); u16(2); u32(6); u32(38);
    u16(0x0112); u16(3); u32(1); u16(orientation); u16(0);
    u32(0);
    block += QByteArray("Diapo\0", 6);
    return block;
}

} // namespace

class DiapoTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QQuickStyle::setStyle("Material"); }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QColor("black"));
        QVERIFY(!theme.light());
        QCOMPARE(theme.radius(), 0);
    }

    void themeReadsThePalette() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("theme"));
        QVERIFY(write(dir.path() + "/theme/colors.toml", "mode = \"light\"\n"
                                                         "theme_type = \"dark\"\n"
                                                         "accent = \"#112233\"\n"
                                                         "bg = #fafafa # paper\n"
                                                         "foreground = '#202020'\n"
                                                         "color1 = \"#aa0000\"\n"));

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#112233"));
        QCOMPARE(theme.accentForeground(), QColor("white"));
        QCOMPARE(theme.background(), QColor("#fafafa"));
        QCOMPARE(theme.foreground(), QColor("#202020"));
        QCOMPARE(theme.colors().value("red").value<QColor>(), QColor("#aa0000"));
        QVERIFY(theme.light());
    }

    void themeAsksHyprlandAgainOnlyWhenItReloads() {
        // The fake Hyprland lives outside the theme directory, whose changes
        // the Theme also follows.
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));

        // Once on start, and once more when the event socket connects.
        Theme theme(dir.path(), nullptr, command, events.fullServerName());
        QTRY_VERIFY(events.hasPendingConnections());
        QLocalSocket *hyprland = events.nextPendingConnection();
        QTRY_COMPARE(lines(hypr.filePath("calls")), 2);
        QTRY_COMPARE(theme.radius(), 4);

        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":10}"));
        QTest::qWait(300);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        // Events arrive in pieces; only a whole configreloaded line counts.
        hyprland->write("activewindow>>kitty,~\nconfigrel");
        hyprland->flush();
        QTest::qWait(100);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        hyprland->write("oaded>>\n");
        QTRY_COMPARE(theme.radius(), 10);
        QCOMPARE(lines(hypr.filePath("calls")), 3);
    }

    void themeReachesHyprlandOnceItListens() {
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        Theme theme(dir.path(), nullptr, command, hypr.filePath("events.sock"));
        QTRY_COMPARE(theme.radius(), 4);

        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":8}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));
        QTRY_VERIFY_WITH_TIMEOUT(events.hasPendingConnections(), 5000);
        QTRY_COMPARE(theme.radius(), 8);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "pt-BR\n"));
        QVERIFY(write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})"));
        QVERIFY(write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorSeesAFirstPick() {
        // Before the first pick there is no ~/.config/omarchy to watch.
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/da.json", R"({"Save": "Gem"})"));
        Translator translator(dir.path() + "/omarchy/language", dir.path());
        QVERIFY(QDir(dir.path()).mkpath("omarchy"));
        QVERIFY(write(dir.path() + "/omarchy/language", "da\n"));
        QTRY_COMPARE(translator.translate("Main", "Save"), QString("Gem"));
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "ru\n"));
        QVERIFY(write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"},
                                                   "%n files": {"one": "%n file", "other": "%n files"}})"));
        QVERIFY(write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        // An untranslated counted string falls back to English, with English rules.
        QCOMPARE(translator.translate("Main", "%n files", nullptr, 21), QString("%n files"));
    }

    void englishCatalogListsEveryString() {
        // en.json is what translators work from: every text the sources
        // translate is in it, as itself or, when counted, as its English
        // plural forms, and nothing else is.
        const QString sources = QFileInfo(QFINDTESTDATA("../src/Main.qml")).absolutePath();
        QStringList problems;
        const QMap<QString, bool> used = translatables(sources, &problems);
        const QJsonObject english = readCatalog(sources + "/i18n/en.json");
        for (auto it = used.begin(); it != used.end(); ++it) {
            const QJsonValue value = english.value(it.key());
            if (value.isUndefined())
                problems << "missing from en.json: " + it.key();
            else if (it.value() && (value.toObject().keys() != QStringList{"one", "other"}
                                    || value.toObject().value("other").toString() != it.key()))
                problems << "counted, so needs its one and other forms: " + it.key();
            else if (!it.value() && value.toString() != it.key())
                problems << "should be itself: " + it.key();
        }
        for (const QString &key : english.keys())
            if (!used.contains(key))
                problems << "in en.json but no source translates it: " + key;
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void catalogsMatchTheEnglishOne() {
        // A catalog may lack a string (it shows in English meanwhile), but what
        // it has translates something in en.json, with the same placeholders,
        // and a counted string has every form its language uses.
        const QString folder = QFileInfo(QFINDTESTDATA("../src/i18n/en.json")).absolutePath();
        const QJsonObject english = readCatalog(folder + "/en.json");
        QStringList problems;
        for (const QFileInfo &file : QDir(folder).entryInfoList({"*.json"}, QDir::Files)) {
            const QString code = file.completeBaseName();
            const QJsonObject catalog = readCatalog(file.filePath());
            if (catalog.isEmpty())
                problems << code + ": not a JSON object, or empty";
            // Some languages reach a form only at a million ("many" in French).
            QSet<QString> categories{"other", Translator::pluralCategory(code, 1000000)};
            for (long long n = 0; n <= 1000; ++n)
                categories << Translator::pluralCategory(code, n);
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                const QString where = code + ": " + it.key() + ": ";
                const QJsonValue source = english.value(it.key());
                if (source.isUndefined()) {
                    problems << where + "not in en.json";
                } else if (source.isString()) {
                    if (!it.value().isString() || it.value().toString().isEmpty())
                        problems << where + "needs a translation";
                    else if (placeholders(it.value().toString()) != placeholders(it.key()))
                        problems << where + "has other placeholders than the English";
                } else {
                    const QJsonObject forms = it.value().toObject();
                    const QStringList wanted = placeholders(source.toObject().value("other").toString());
                    for (const QString &category : categories)
                        if (forms.value(category).toString().isEmpty())
                            problems << where + "needs its " + category + " form";
                    // A form other than "other" may say its number in words.
                    for (auto form = forms.begin(); form != forms.end(); ++form) {
                        const QStringList has = placeholders(form.value().toString());
                        QStringList needed = wanted;
                        if (form.key() != "other")
                            needed.removeIf([&](const QString &p) { return p.endsWith('n') && !has.contains(p); });
                        if (has != needed)
                            problems << where + form.key() + " has other placeholders than the English";
                    }
                }
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void looksAreAllThere() {
        QCOMPARE(looks::all().size(), 30);
        QHash<QString, int> groups;
        for (const Look &look : looks::all())
            ++groups[look.group];
        QCOMPARE(groups.value("Leica"), 11);
        QCOMPARE(groups.value("Fujifilm"), 12);
        QCOMPARE(groups.value("Kodak"), 7);
        QVERIFY(looks::find("classic chrome"));
        QVERIFY(!looks::find("No such film"));
    }

    void aLookChangesThePictureByItsStrength() {
        const QImage before = sample();
        const Look *velvia = looks::find("Velvia");
        QVERIFY(velvia);
        QCOMPARE(looks::apply(before, *velvia, 0), before);
        const QImage full = looks::apply(before, *velvia, 1);
        QCOMPARE(full.size(), before.size());
        QVERIFY(full != before);
        // Black and white film leaves no colour behind.
        const QColor grey = looks::apply(before, *looks::find("Acros"), 1).pixelColor(30, 30);
        QCOMPARE(grey.red(), grey.green());
        QCOMPARE(grey.green(), grey.blue());
    }

    void geometryTurnsAndCuts() {
        const QImage image = sample(240, 160);
        Geometry geometry;
        geometry.quarter = 1;
        QCOMPARE(looks::applyGeometry(image, geometry).size(), QSize(160, 240));
        geometry.crop = QRectF(0.25, 0.25, 0.5, 0.5);
        QCOMPARE(looks::applyGeometry(image, geometry).size(), QSize(80, 120));
        geometry = Geometry();
        geometry.angle = 10;
        QCOMPARE(looks::applyGeometry(image, geometry).size(), image.size());
        QCOMPARE(looks::fit(image, 120).size(), QSize(120, 80));
        QCOMPARE(looks::fit(image, 0).size(), image.size());
    }

    void everyFormatWritesAndReadsBack_data() {
        QTest::addColumn<QString>("id");
        for (const char *id : {"jpeg", "png", "webp", "avif", "jxl"})
            QTest::newRow(id) << QString(id);
    }
    void everyFormatWritesAndReadsBack() {
        QFETCH(QString, id);
        imageio::Format format;
        QVERIFY(imageio::parseFormat(id, &format));
        QString error;
        const QByteArray data = imageio::encode(sample(), format, 80, {}, &error);
        QVERIFY2(!data.isEmpty(), qPrintable(error));
        QTemporaryDir dir;
        const QString path = dir.filePath("photo." + imageio::formatExtension(format));
        QVERIFY(imageio::write(path, data));
        QVERIFY(imageio::isPhoto(path));
        QCOMPARE(imageio::sizeOf(path), QSize(240, 160));
        QCOMPARE(imageio::load(path).size(), QSize(240, 160));
        QCOMPARE(imageio::load(path, 120).size(), QSize(120, 80));
        QCOMPARE(imageio::decode(data).size(), QSize(240, 160));
    }

    void cameraDataIsCarriedOrLeftOut_data() {
        QTest::addColumn<QString>("id");
        for (const char *id : {"jpeg", "png", "webp", "avif"})
            QTest::newRow(id) << QString(id);
    }
    void cameraDataIsCarriedOrLeftOut() {
        QFETCH(QString, id);
        imageio::Format format;
        QVERIFY(imageio::parseFormat(id, &format));
        QTemporaryDir dir;
        const QString with = dir.filePath("with." + id), without = dir.filePath("without." + id);
        QVERIFY(imageio::write(with, imageio::encode(sample(), format, 80, exifBlock(1))));
        QVERIFY(imageio::write(without, imageio::encode(sample(), format, 80)));
        QVERIFY(!imageio::exifOf(with).isEmpty());
        QVERIFY(imageio::exifOf(without).isEmpty());
        const auto facts = imageio::cameraFacts(with);
        QCOMPARE(facts.size(), 1);
        QCOMPARE(facts.first().second, QString("Diapo"));
        QCOMPARE(imageio::load(with).size(), QSize(240, 160));
    }

    void aSidewaysPhotoIsReadUpright() {
        // Orientation 6: the camera was held on its side.
        QTemporaryDir dir;
        const QString path = dir.filePath("side.jpg");
        QVERIFY(imageio::write(path, imageio::encode(sample(), imageio::Format::Jpeg, 80, exifBlock(6))));
        QCOMPARE(imageio::sizeOf(path), QSize(160, 240));
        QCOMPARE(imageio::load(path).size(), QSize(160, 240));
        // The block handed on says upright, since the pixels now are.
        const QString copy = dir.filePath("copy.jpg");
        QVERIFY(imageio::write(copy, imageio::encode(imageio::load(path), imageio::Format::Jpeg, 80, imageio::exifOf(path))));
        QCOMPARE(imageio::load(copy).size(), QSize(160, 240));
    }

    void heicIsDecodedOnceAndShared() {
        // heif-enc writes a HEIC with a small preview, as a phone does.
        QTemporaryDir dir;
        const QString png = dir.filePath("source.png"), heic = dir.filePath("photo.heic");
        QVERIFY(saveSample(png, 1200, 800));
        QCOMPARE(QProcess::execute("heif-enc", {"-q", "80", "-t", "240", png, "-o", heic}), 0);
        QVERIFY(imageio::decodesWhole(heic));
        QVERIFY(!imageio::decodesWhole(png));
        const QImage preview = imageio::thumbnail(heic);
        QVERIFY(!preview.isNull());
        QVERIFY(qMax(preview.width(), preview.height()) <= 240);
        QVERIFY(imageio::thumbnail(png).isNull());

        // Asked at two sizes, it is decoded once, whole, and both come from that.
        const QFuture<QImage> first = Images::fetch(heic, 600);
        const QFuture<QImage> second = Images::fetch(heic, 900);
        QCOMPARE(first.result().size(), QSize(1200, 800));
        // The same picture in memory, not a second decode.
        QCOMPARE(second.result().constBits(), first.result().constBits());
        Images images(true);
        QSize size;
        const int token = Images::tokenFor(heic);
        QCOMPARE(images.requestImage(QString("file/600/%1").arg(token), &size, {}).size(), QSize(600, 400));
    }

    void savingNeverReplacesAFile() {
        QTemporaryDir dir;
        QVERIFY(saveSample(dir.filePath("a.png")));
        QCOMPARE(imageio::uniquePath(dir.path(), "a", "png"), dir.filePath("a-2.png"));
        QCOMPARE(imageio::uniquePath(dir.path(), "b", "png"), dir.filePath("b.png"));
        QCOMPARE(Compressor::targetPath(dir.path(), dir.filePath("a.png"), imageio::Format::Webp), dir.filePath("a.webp"));
        QCOMPARE(Compressor::targetPath(dir.path(), dir.filePath("a.png"), imageio::Format::Png), dir.filePath("a-small.png"));
        QCOMPARE(Editor::copyPath(dir.filePath("a.png")), dir.filePath("a-edited.png"));
        QCOMPARE(Editor::copyPath(dir.filePath("a.heic")), dir.filePath("a-edited.jpg"));
    }

    void viewerStepsThroughAFolderInOrder() {
        QTemporaryDir dir;
        for (const char *name : {"p10.png", "p2.png", "p1.png"})
            QVERIFY(saveSample(dir.filePath(name)));
        QVERIFY(write(dir.filePath("notes.txt"), "not a photo"));

        Backend backend;
        backend.open(dir.filePath("p2.png"));
        QCOMPARE(backend.count(), 3);
        QCOMPARE(backend.name(), QString("p2.png"));
        QCOMPARE(backend.index(), 1);
        QCOMPARE(backend.size(), QSize(240, 160));
        backend.next();
        QCOMPARE(backend.name(), QString("p10.png"));
        backend.next();
        QCOMPARE(backend.name(), QString("p1.png"));
        backend.previous();
        QCOMPARE(backend.name(), QString("p10.png"));

        // A photo that appears in the folder joins the sequence.
        QVERIFY(saveSample(dir.filePath("p3.png")));
        QTRY_COMPARE(backend.count(), 4);
        // A folder opens at its first photo.
        backend.open(dir.path());
        QCOMPARE(backend.name(), QString("p1.png"));
    }

    void editingWritesACopyAndLeavesTheOriginal() {
        QTemporaryDir dir;
        const QString path = dir.filePath("photo.jpg");
        QVERIFY(saveSample(path));
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray before = original.readAll();

        EditState state;
        state.look = "Classic Chrome";
        state.exposure = 20;
        state.geometry.quarter = 1;
        state.geometry.crop = QRectF(0, 0, 0.5, 0.5);
        QString error;
        const QString copy = Editor::saveCopy(path, state, &error);
        QVERIFY2(!copy.isEmpty(), qPrintable(error));
        QCOMPARE(copy, dir.filePath("photo-edited.jpg"));
        QCOMPARE(imageio::load(copy).size(), QSize(80, 120));
        original.seek(0);
        QCOMPARE(original.readAll(), before);
    }

    void editorUndoesBackToClean() {
        QTemporaryDir dir;
        const QString path = dir.filePath("photo.png");
        QVERIFY(saveSample(path));
        Editor editor;
        editor.begin(path);
        QTRY_VERIFY(editor.ready());
        QTRY_VERIFY(editor.thumbRevision() > 0);
        QVERIFY(!editor.dirty());
        editor.set("contrast", 30);
        editor.commit();
        editor.setLook("Velvia");
        QVERIFY(editor.dirty());
        QCOMPARE(editor.values().value("contrast").toInt(), 30);
        editor.undo();
        QCOMPARE(editor.look(), QString());
        editor.undo();
        QVERIFY(!editor.dirty());
        QVERIFY(!editor.canUndo());
        editor.redo();
        QCOMPARE(editor.values().value("contrast").toInt(), 30);
        editor.setAspect("1:1");
        QCOMPARE(qRound(editor.crop().width() * 240), 160);
        QCOMPARE(qRound(editor.crop().height() * 160), 160);
        editor.end();
    }

    void compressorMakesAPhotoSmallerInMemory() {
        QTemporaryDir dir;
        const QString path = dir.filePath("big.png");
        QVERIFY(saveSample(path, 1200, 800));
        CompressSettings settings;
        settings.maxEdge = 600;
        const Compressed result = Compressor::compressImage(imageio::load(path), path, settings);
        QCOMPARE(result.format, imageio::Format::Webp);
        QCOMPARE(result.size, QSize(600, 400));
        QVERIFY(!result.data.isEmpty());
        // "Same" keeps a format Diapo writes, and turns the rest into JPEG.
        settings.format = "same";
        QCOMPARE(Compressor::targetFormat(path, settings), imageio::Format::Png);
        QCOMPARE(Compressor::targetFormat(dir.filePath("raw.cr2"), settings), imageio::Format::Jpeg);
        // Nothing was written yet.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }

    void cropDragsWithTheMouse() {
        QTemporaryDir dir, photos;
        QVERIFY(saveSample(photos.filePath("one.png"), 900, 600));
        Translator translator(dir.path() + "/language");
        Theme theme(dir.path());
        Backend backend;
        Editor editor;
        Compressor compressor;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.addImageProvider("photo", new PhotoImages);
        engine.addImageProvider("made", new Images(false));
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.rootContext()->setContextProperty("compressor", &compressor);
        engine.rootContext()->setContextProperty("startIn", QString());
        engine.rootContext()->setContextProperty("startAfter", 0);
        backend.open(photos.filePath("one.png"));
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTRY_VERIFY(window->property("settled").toBool());
        QTest::keyClick(window, Qt::Key_E);
        QTRY_VERIFY(editor.ready());
        QQuickItem *frame = nullptr;
        QTRY_VERIFY((frame = drawn(window->contentItem(), "cropFrame")) && frame->isVisible() && frame->width() > 100);
        const double fw = frame->width(), fh = frame->height();
        const auto at = [&](double x, double y) { return frame->mapToScene(QPointF(x * fw, y * fh)).toPoint(); };
        const auto centre = [&](const char *name) {
            QQuickItem *item = drawn(window->contentItem(), name);
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        // A drag in small steps, the way a hand moves.
        const auto drag = [&](QPoint from, QPoint to) {
            QTest::mousePress(window, Qt::LeftButton, {}, from);
            for (int i = 1; i <= 8; ++i)
                QTest::mouseMove(window, from + (to - from) * i / 8);
            QTest::mouseRelease(window, Qt::LeftButton, {}, to);
        };
        const auto near = [](QRectF got, QRectF want) {
            const double slack = 0.02;
            return qAbs(got.x() - want.x()) < slack && qAbs(got.y() - want.y()) < slack
                   && qAbs(got.width() - want.width()) < slack && qAbs(got.height() - want.height()) < slack;
        };
        const auto show = [](QRectF r) { return QString("%1 %2 %3 %4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); };

        // Grabbing a handle a little off its middle doesn't make the corner jump.
        const QPoint grip = centre("cropHandle00") + QPoint(5, 5);
        QTest::mousePress(window, Qt::LeftButton, {}, grip);
        QTest::mouseMove(window, grip + QPoint(1, 0));
        QTest::mouseMove(window, grip + QPoint(2, 0));
        QVERIFY2(qAbs(editor.crop().x() * fw - 2) < 1 && qAbs(editor.crop().y() * fh) < 1, qPrintable(show(editor.crop())));
        QTest::mouseRelease(window, Qt::LeftButton, {}, grip + QPoint(2, 0));

        // The top-left corner follows the pointer.
        editor.setCrop(0, 0, 1, 1);
        drag(centre("cropHandle00"), at(0.2, 0.1));
        QVERIFY2(near(editor.crop(), QRectF(0.2, 0.1, 0.8, 0.9)), qPrintable(show(editor.crop())));
        // The bottom-right corner, dragged past the photo, stops at its edge.
        drag(centre("cropHandle11"), at(1.05, 1.08));
        QVERIFY2(near(editor.crop(), QRectF(0.2, 0.1, 0.8, 0.9)), qPrintable(show(editor.crop())));
        drag(centre("cropHandle11"), at(0.7, 0.6));
        QVERIFY2(near(editor.crop(), QRectF(0.2, 0.1, 0.5, 0.5)), qPrintable(show(editor.crop())));
        // A corner can't be dragged past the opposite one.
        drag(centre("cropHandle00"), at(0.9, 0.9));
        QVERIFY(editor.crop().width() >= 0.049 && editor.crop().height() >= 0.049);
        QVERIFY2(editor.crop().right() <= 0.701 && editor.crop().bottom() <= 0.601, qPrintable(show(editor.crop())));

        // The box moves whole, and stops at the photo's edges.
        editor.setCrop(0.2, 0.1, 0.5, 0.5);
        drag(centre("cropBox"), centre("cropBox") + QPoint(int(0.1 * fw), int(0.2 * fh)));
        QVERIFY2(near(editor.crop(), QRectF(0.3, 0.3, 0.5, 0.5)), qPrintable(show(editor.crop())));
        drag(centre("cropBox"), centre("cropBox") + QPoint(int(0.4 * fw), int(0.3 * fh)));
        QVERIFY2(near(editor.crop(), QRectF(0.5, 0.5, 0.5, 0.5)), qPrintable(show(editor.crop())));

        // A chosen shape holds while a corner is dragged: 1:1 stays square.
        editor.setCrop(0, 0, 1, 1);
        editor.setAspect("1:1");
        QTRY_VERIFY(drawn(window->contentItem(), "cropHandle11"));
        drag(centre("cropHandle11"), at(0.5, 0.9));
        const QRectF square = editor.crop();
        QVERIFY2(qAbs(square.width() * fw - square.height() * fh) < 3, qPrintable(show(square)));
        QVERIFY(square.bottom() <= 1.0001 && square.right() <= 1.0001);

        // Each drag is one step to undo.
        editor.setAspect("free");
        editor.setCrop(0, 0, 1, 1);
        editor.commit();
        drag(centre("cropHandle00"), at(0.25, 0.25));
        QVERIFY(near(editor.crop(), QRectF(0.25, 0.25, 0.75, 0.75)));
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY2(near(editor.crop(), QRectF(0, 0, 1, 1)), qPrintable(show(editor.crop())));

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void saveFolderCanBeChanged() {
        QTemporaryDir dir, photos, elsewhere;
        QVERIFY(saveSample(photos.filePath("one.png"), 900, 600));
        // The new folder already has a one.webp, which must not be touched.
        QFile taken(elsewhere.filePath("one.webp"));
        QVERIFY(taken.open(QIODevice::WriteOnly));
        taken.write("keep me");
        taken.close();
        Translator translator(dir.path() + "/language");
        Theme theme(dir.path());
        Backend backend;
        Editor editor;
        FakePicker picker;
        Compressor compressor(&picker);
        connect(&compressor, &Compressor::failed, &backend, &Backend::say);
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.addImageProvider("photo", new PhotoImages);
        engine.addImageProvider("made", new Images(false));
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.rootContext()->setContextProperty("compressor", &compressor);
        engine.rootContext()->setContextProperty("startIn", QString());
        engine.rootContext()->setContextProperty("startAfter", 0);
        backend.open(photos.filePath("one.png"));
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTRY_VERIFY(window->property("settled").toBool());

        QTest::keyClick(window, Qt::Key_C);
        QTRY_VERIFY_WITH_TIMEOUT(compressor.estimateBytes() > 0 && !compressor.estimating(), 15000);
        auto *saveDialog = window->findChild<QObject *>("saveDialog");
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(saveDialog->property("opened").toBool(), 15000);
        auto *name = window->findChild<QObject *>("saveName");
        auto *folder = window->findChild<QObject *>("saveFolder");
        QCOMPARE(name->property("text").toString(), QString("one.webp"));
        QCOMPARE(folder->property("text").toString(), backend.tidyPath(photos.path()));

        // Change… asks the chooser, starting in the folder in use.
        QQuickItem *change = drawn(window->contentItem(), "saveChange");
        QVERIFY(change);
        QTest::mouseClick(window, Qt::LeftButton, {}, change->mapToScene(QPointF(change->width() / 2, change->height() / 2)).toPoint());
        QCOMPARE(picker.asked, 1);
        QCOMPARE(picker.askedPurpose, QString("compress"));
        QCOMPARE(picker.askedFolder, photos.path());
        // Cancelling the chooser answers nothing: all stays as it was.
        QVERIFY(saveDialog->property("visible").toBool());
        QCOMPARE(folder->property("text").toString(), backend.tidyPath(photos.path()));

        // Choosing a folder shows it, and the name moves off the one taken there.
        emit picker.folderPicked("compress", elsewhere.path());
        QCOMPARE(folder->property("text").toString(), backend.tidyPath(elsewhere.path()));
        QCOMPARE(name->property("text").toString(), QString("one-small.webp"));
        QVERIFY(saveDialog->property("visible").toBool());
        // An answer meant for another asker is not ours.
        emit picker.folderPicked("open", photos.path());
        QCOMPARE(folder->property("text").toString(), backend.tidyPath(elsewhere.path()));

        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->property("mode").toString(), QString("view"));
        QVERIFY(QFileInfo::exists(elsewhere.filePath("one-small.webp")));
        QVERIFY(!QFileInfo::exists(photos.filePath("one.webp")));
        QVERIFY(taken.open(QIODevice::ReadOnly));
        QCOMPARE(taken.readAll(), QByteArray("keep me"));
        taken.close();

        // A typed name is kept when the folder changes after it.
        QTest::keyClick(window, Qt::Key_C);
        QTRY_VERIFY_WITH_TIMEOUT(compressor.estimateBytes() > 0 && !compressor.estimating(), 15000);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(saveDialog->property("opened").toBool(), 15000);
        name->setProperty("text", "holiday.webp");
        emit picker.folderPicked("compress", photos.path());
        QCOMPARE(name->property("text").toString(), QString("holiday.webp"));

        // A folder that can't be written says so, and keeps the result.
        QTemporaryDir locked;
        QFile::setPermissions(locked.path(), QFile::ReadOwner | QFile::ExeOwner);
        emit picker.folderPicked("compress", locked.path());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(backend.property("notice").toString().startsWith("Could not save to"));
        QVERIFY(compressor.resultReady());
        QVERIFY(QDir(locked.path()).entryList(QDir::Files).isEmpty());
        QFile::setPermissions(locked.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void windowDoesItsThreeJobs() {
        // A temp language and theme, so the test never depends on the desktop's.
        QTemporaryDir dir, photos;
        for (const char *name : {"one.png", "two.png"})
            QVERIFY(saveSample(photos.filePath(name), 900, 600));
        Translator translator(dir.path() + "/language");
        QCoreApplication::installTranslator(&translator);
        Theme theme(dir.path());
        Backend backend;
        Editor editor;
        Compressor compressor;

        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.addImageProvider("photo", new PhotoImages);
        engine.addImageProvider("made", new Images(false));
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.rootContext()->setContextProperty("compressor", &compressor);
        engine.rootContext()->setContextProperty("startIn", QString());
        engine.rootContext()->setContextProperty("startAfter", 0);
        backend.open(photos.filePath("one.png"));
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTRY_VERIFY(window->property("settled").toBool());

        // Viewing: Space steps on, and the bar says where in the folder.
        auto *position = window->findChild<QObject *>("position");
        QCOMPARE(position->property("text").toString(), QString("1 of 2"));
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(backend.name(), QString("two.png"));
        QCOMPARE(position->property("text").toString(), QString("2 of 2"));
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(backend.name(), QString("one.png"));

        auto *help = window->findChild<QObject *>("help");
        QTest::keyClick(window, '?');
        QTRY_VERIFY(help->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!help->property("visible").toBool());

        // Editing: a look is one click, and leaving with edits asks first.
        QTest::keyClick(window, Qt::Key_L);
        QCOMPARE(window->property("mode").toString(), QString("edit"));
        QTRY_VERIFY(editor.ready());
        QTRY_VERIFY(editor.thumbRevision() > 0);
        auto *leica = drawn(window->contentItem(), "look-Classic");
        QVERIFY(leica);
        QTest::mouseClick(window, Qt::LeftButton, {}, leica->mapToScene(QPointF(10, 10)).toPoint());
        QCOMPARE(editor.look(), QString("Classic"));
        auto *ask = window->findChild<QObject *>("ask");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(ask->property("opened").toBool());
        // Escape on the question keeps the edits.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!ask->property("visible").toBool());
        QCOMPARE(window->property("mode").toString(), QString("edit"));
        QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(window->property("mode").toString(), QString("view"), 15000);
        QCOMPARE(backend.name(), QString("one-edited.png"));
        QCOMPARE(backend.count(), 3);

        // Compressing: the popup comes up when it is done, and Save writes.
        QTest::keyClick(window, Qt::Key_C);
        QCOMPARE(window->property("mode").toString(), QString("compress"));
        QTRY_VERIFY_WITH_TIMEOUT(compressor.estimateBytes() > 0 && !compressor.estimating(), 15000);
        auto *saveDialog = window->findChild<QObject *>("saveDialog");
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(saveDialog->property("opened").toBool(), 15000);
        QVERIFY(!QFileInfo::exists(photos.filePath("one-edited.webp")));
        QCOMPARE(window->findChild<QObject *>("saveName")->property("text").toString(), QString("one-edited.webp"));
        // Not now keeps the result; the button offers the save again.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!saveDialog->property("visible").toBool());
        QVERIFY(compressor.resultReady());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(saveDialog->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->property("mode").toString(), QString("view"));
        QVERIFY(QFileInfo::exists(photos.filePath("one-edited.webp")));
        QVERIFY(QFileInfo(photos.filePath("one-edited.webp")).size() < QFileInfo(photos.filePath("one-edited.png")).size());
        QVERIFY(window->findChild<QObject *>("toast")->property("text").toString().startsWith("Saved to "));

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QCoreApplication::removeTranslator(&translator);
    }
};

QTEST_MAIN(DiapoTests)
#include "diapo_tests.moc"
