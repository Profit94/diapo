#include "editor.h"

#include <QEventLoopLocker>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <memory>

#include "background.h"
#include "debug.h"
#include "imageio.h"
#include "images.h"

namespace {
const QStringList sliders{"exposure", "contrast", "saturation", "warmth", "tint", "fade", "grain", "vignette"};

struct Preview {
    QImage plain, styled;
};
} // namespace

bool EditState::operator==(const EditState &o) const {
    return exposure == o.exposure && contrast == o.contrast && saturation == o.saturation && warmth == o.warmth
           && tint == o.tint && fade == o.fade && grain == o.grain && vignette == o.vignette && look == o.look
           && (look.isEmpty() || strength == o.strength) && geometry == o.geometry;
}

int *EditState::slider(const QString &key) {
    int *all[] = {&exposure, &contrast, &saturation, &warmth, &tint, &fade, &grain, &vignette};
    const int at = sliders.indexOf(key);
    return at < 0 ? nullptr : all[at];
}

bool EditState::adjusted() const {
    return exposure || contrast || saturation || warmth || tint || fade || grain || vignette;
}

Look EditState::adjustment() const {
    Look look;
    look.exposure = exposure / 100.0f;          // one stop either way
    look.contrast = 1 + contrast / 200.0f;      // 0.5 to 1.5
    look.saturation = 1 + saturation / 100.0f;  // grey to double
    look.warmth = warmth / 100.0f * 1.5f;
    look.tint = tint / 100.0f * 2.0f;
    look.fade = fade / 100.0f * 0.25f;
    look.grain = grain / 100.0f * 0.12f;
    look.vignette = vignette / 100.0f * 0.6f;
    return look;
}

Editor::Editor(QObject *parent) : QObject(parent) {}

QString Editor::name() const { return QFileInfo(m_path).fileName(); }

QVariantMap Editor::values() const {
    QVariantMap map;
    EditState state = m_state;
    for (const QString &key : sliders)
        map.insert(key, *state.slider(key));
    return map;
}

QVariantList Editor::looks() const {
    QVariantList list;
    int index = 0;
    for (const Look &look : looks::all())
        list << QVariantMap{{"index", index++}, {"name", look.name}, {"group", look.group}};
    return list;
}

QVariantList Editor::groups() const {
    QVariantList list;
    QStringList seen;
    for (const Look &look : looks::all())
        if (!seen.contains(look.group))
            seen << look.group;
    for (const QString &group : seen) {
        int count = 0;
        for (const Look &look : looks::all())
            count += look.group == group;
        list << QVariantMap{{"name", group}, {"count", count}};
    }
    return list;
}

// Width over height of the crop the chosen shape asks for, in pixels; 0 is free.
double Editor::aspectRatio() const {
    if (m_state.aspect == "free" || !m_previewSize.isValid())
        return 0;
    const double shown = double(m_previewSize.width()) / m_previewSize.height();
    if (m_state.aspect == "original")
        return shown;
    const QStringList parts = m_state.aspect.split(':');
    if (parts.size() != 2 || parts[1].toDouble() <= 0)
        return 0;
    const double ratio = parts[0].toDouble() / parts[1].toDouble();
    // 3:2 on an upright picture means 2:3.
    return shown < 1 ? 1 / ratio : ratio;
}

void Editor::begin(const QString &path) {
    end();
    m_path = path;
    m_state = EditState();
    m_history = {m_state};
    m_at = 0;
    emit activeChanged();
    emit changed();
    const int session = ++m_session;
    // The viewer has usually decoded the photo at screen size or whole
    // already; that is enough to edit with, so nothing is decoded again.
    const QImage shown = Images::ready(path);
    const QSize full = imageio::sizeOf(path);
    const bool enough = !shown.isNull()
                        && (qMax(shown.width(), shown.height()) >= 1600
                            || (full.isValid() && shown.size() == full));
    inBackground<QImage>(this, [path, shown, enough] {
        return enough ? looks::fit(shown, 1600) : imageio::load(path, 1600);
    }, [this, session, path](const QImage &image) {
        if (session != m_session)
            return;
        if (image.isNull()) {
            emit failed(tr("%1 could not be opened for editing.").arg(QFileInfo(path).fileName()));
            end();
            return;
        }
        m_base = image.convertToFormat(QImage::Format_RGBA8888);
        mark("editor: photo decoded");
        renderPreview();
        renderThumbs();
    });
}

void Editor::end() {
    if (m_path.isEmpty())
        return;
    ++m_session;
    m_path.clear();
    m_base = {};
    m_previewSize = {};
    m_rendering = m_renderAgain = false;
    Images::drop("edit");
    Images::drop("look/");
    emit activeChanged();
    emit previewChanged();
}

void Editor::touch() {
    emit changed();
    renderPreview();
}

void Editor::set(const QString &key, int value) {
    int *slider = m_state.slider(key);
    if (!slider || *slider == value)
        return;
    *slider = value;
    touch();
}

void Editor::setStrength(int value) {
    if (m_state.strength == value)
        return;
    m_state.strength = qBound(0, value, 100);
    touch();
}

void Editor::setAngle(double degrees) {
    if (m_state.geometry.angle == degrees)
        return;
    m_state.geometry.angle = qBound(-45.0, degrees, 45.0);
    touch();
}

void Editor::setCrop(double x, double y, double width, double height) {
    width = qBound(0.05, width, 1.0);
    height = qBound(0.05, height, 1.0);
    const QRectF crop(qBound(0.0, x, 1 - width), qBound(0.0, y, 1 - height), width, height);
    if (crop == m_state.geometry.crop)
        return;
    m_state.geometry.crop = crop;
    // The preview shows the whole picture with the crop drawn over it, so
    // nothing needs rendering again.
    emit changed();
}

void Editor::commit() {
    if (m_history.value(m_at) == m_state && m_history.value(m_at).aspect == m_state.aspect)
        return;
    m_history = m_history.mid(0, m_at + 1);
    m_history << m_state;
    m_at = m_history.size() - 1;
    emit changed();
}

void Editor::setLook(const QString &name) {
    if (m_state.look == name)
        return;
    m_state.look = name;
    commit();
    touch();
}

void Editor::setAspect(const QString &aspect) {
    m_state.aspect = aspect;
    const double ratio = aspectRatio();
    if (ratio > 0 && m_previewSize.isValid()) {
        // The largest crop of that shape, in the middle.
        const double w = m_previewSize.width(), h = m_previewSize.height();
        const double cw = qMin(w, h * ratio), ch = cw / ratio;
        m_state.geometry.crop = QRectF((1 - cw / w) / 2, (1 - ch / h) / 2, cw / w, ch / h);
    } else if (aspect == "free") {
        m_state.geometry.crop = QRectF(0, 0, 1, 1);
    }
    commit();
    emit changed();
}

void Editor::rotate(int quarters) {
    m_state.geometry.quarter = (((m_state.geometry.quarter + quarters) % 4) + 4) % 4;
    // Turning a mirrored picture a quarter swaps which way it is mirrored.
    if (quarters % 2 != 0)
        std::swap(m_state.geometry.flipH, m_state.geometry.flipV);
    // A crop drawn on the picture lying one way means nothing on the other.
    m_state.geometry.crop = QRectF(0, 0, 1, 1);
    m_state.aspect = "free";
    commit();
    touch();
    renderThumbs();
}

void Editor::flip(bool horizontal) {
    // The flips are applied after the turn, so they are already the screen's.
    if (horizontal)
        m_state.geometry.flipH = !m_state.geometry.flipH;
    else
        m_state.geometry.flipV = !m_state.geometry.flipV;
    QRectF &crop = m_state.geometry.crop;
    if (horizontal)
        crop.moveLeft(1 - crop.right());
    else
        crop.moveTop(1 - crop.bottom());
    commit();
    touch();
    renderThumbs();
}

void Editor::undo() {
    if (!canUndo())
        return;
    const Geometry before = m_state.geometry;
    m_state = m_history.at(--m_at);
    touch();
    if (before.quarter != m_state.geometry.quarter || before.flipH != m_state.geometry.flipH
        || before.flipV != m_state.geometry.flipV)
        renderThumbs();
}

void Editor::redo() {
    if (!canRedo())
        return;
    const Geometry before = m_state.geometry;
    m_state = m_history.at(++m_at);
    touch();
    if (before.quarter != m_state.geometry.quarter || before.flipH != m_state.geometry.flipH
        || before.flipV != m_state.geometry.flipV)
        renderThumbs();
}

void Editor::reset() {
    if (!dirty())
        return;
    const bool turned = m_state.geometry.quarter || m_state.geometry.flipH || m_state.geometry.flipV;
    m_state = EditState();
    commit();
    touch();
    if (turned)
        renderThumbs();
}

QImage Editor::render(const QImage &source, const EditState &state, bool crop) {
    QImage image = looks::applyGeometry(source, state.geometry, crop);
    if (state.adjusted())
        image = looks::apply(image, state.adjustment(), 1);
    if (const Look *look = looks::find(state.look))
        image = looks::apply(image, *look, state.strength / 100.0f);
    return image;
}

void Editor::renderPreview() {
    if (m_base.isNull())
        return;
    // One render at a time; a change that arrives meanwhile is drawn next.
    if (m_rendering) {
        m_renderAgain = true;
        return;
    }
    m_rendering = true;
    const int session = m_session;
    inBackground<Preview>(this, [base = m_base, state = m_state] {
        Preview preview;
        preview.plain = looks::applyGeometry(base, state.geometry, false);
        EditState look = state;
        look.geometry = Geometry();
        preview.styled = render(preview.plain, look, false);
        return preview;
    }, [this, session](const Preview &preview) {
        if (session != m_session)
            return;
        m_rendering = false;
        Images::put("edit", preview.styled);
        Images::put("edit-plain", preview.plain);
        m_previewSize = preview.styled.size();
        ++m_revision;
        mark("editor: preview drawn");
        emit previewChanged();
        emit changed();
        if (m_renderAgain) {
            m_renderAgain = false;
            renderPreview();
        }
    });
}

void Editor::renderThumbs() {
    if (m_base.isNull())
        return;
    const int session = m_session;
    Geometry turn = m_state.geometry;
    turn.angle = 0;
    turn.crop = QRectF(0, 0, 1, 1);
    inBackground<QList<QImage>>(this, [base = m_base, turn] {
        const QImage small = looks::fit(looks::orient(base, turn), 220);
        QList<QImage> thumbs;
        for (const Look &look : looks::all())
            thumbs << looks::apply(small, look, 1);
        return thumbs;
    }, [this, session](const QList<QImage> &thumbs) {
        if (session != m_session)
            return;
        for (int i = 0; i < thumbs.size(); ++i)
            Images::put(QString("look/%1").arg(i), thumbs.at(i));
        ++m_thumbRevision;
        mark("editor: looks drawn");
        emit thumbsChanged();
    });
}

QString Editor::copyPath(const QString &path) {
    const QFileInfo info(path);
    imageio::Format format;
    const QString extension = imageio::formatOfPath(path, &format) ? info.suffix() : "jpg";
    return imageio::uniquePath(info.absolutePath(), info.completeBaseName() + "-edited", extension);
}

QString Editor::saveCopy(const QString &path, const EditState &state, QString *error) {
    const QImage full = imageio::load(path, 0, error);
    if (full.isNull())
        return {};
    // Formats Diapo doesn't write (RAW, HEIC, TIFF) become a JPEG copy.
    imageio::Format format = imageio::Format::Jpeg;
    imageio::formatOfPath(path, &format);
    const QByteArray data = imageio::encode(render(full, state, true), format, 92, imageio::exifOf(path), error);
    const QString target = copyPath(path);
    if (data.isEmpty() || !imageio::write(target, data, error))
        return {};
    return target;
}

void Editor::save() {
    if (m_saving || m_path.isEmpty())
        return;
    commit();
    m_saving = true;
    emit savingChanged();
    struct Result {
        QString path, error;
    };
    inBackground<Result>(this, [path = m_path, state = m_state] {
        Result result;
        result.path = saveCopy(path, state, &result.error);
        return result;
    }, [this](const Result &result) {
        m_saving = false;
        emit savingChanged();
        if (result.path.isEmpty())
            emit failed(tr("The copy could not be saved: %1").arg(result.error));
        else
            emit saved(result.path);
    });
}
