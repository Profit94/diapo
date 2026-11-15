#include "looks.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

QList<QPointF> points(const QJsonValue &value) {
    QList<QPointF> list;
    for (const QJsonValue &pair : value.toArray())
        list << QPointF(pair.toArray().at(0).toDouble(), pair.toArray().at(1).toDouble());
    return list;
}

void triple(const QJsonValue &value, float *out) {
    const QJsonArray array = value.toArray();
    if (array.size() == 3)
        for (int i = 0; i < 3; ++i)
            out[i] = float(array.at(i).toDouble());
}

using Table = std::array<float, 256>;

// Smoothstep between the points gives a softer, film-like shoulder.
bool curveTable(QList<QPointF> pts, Table *table) {
    if (pts.size() < 2)
        return false;
    std::sort(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    for (int i = 0; i < 256; ++i) {
        const double x = i / 255.0;
        int j = pts.size() - 1;
        for (int k = 0; k < pts.size(); ++k)
            if (pts[k].x() >= x) {
                j = k;
                break;
            }
        if (j == 0) {
            (*table)[i] = float(pts[0].y());
        } else {
            const QPointF a = pts[j - 1], b = pts[j];
            double t = b.x() > a.x() ? (x - a.x()) / (b.x() - a.x()) : 0;
            t = t * t * (3 - 2 * t);
            (*table)[i] = float(a.y() + (b.y() - a.y()) * t);
        }
    }
    return true;
}

float noise(quint32 x, quint32 y) {
    quint32 h = x * 374761393u ^ y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0f - 0.5f;
}

// Runs rows(from, to) over the picture's height on every core.
template <typename Rows> void eachBand(int height, Rows rows) {
    const int bands = qBound(1, QThread::idealThreadCount(), qMax(1, height / 64));
    QList<int> starts;
    for (int i = 0; i < bands; ++i)
        starts << i;
    QtConcurrent::blockingMap(starts, [&](int band) {
        rows(qint64(height) * band / bands, qint64(height) * (band + 1) / bands);
    });
}

} // namespace

Look Look::fromJson(const QJsonObject &o) {
    Look look;
    look.name = o.value("name").toString();
    look.group = o.value("group").toString();
    look.exposure = float(o.value("exposure").toDouble(0));
    look.contrast = float(o.value("contrast").toDouble(1));
    look.saturation = float(o.value("saturation").toDouble(1));
    look.warmth = float(o.value("warmth").toDouble(0));
    look.tint = float(o.value("tint").toDouble(0));
    look.fade = float(o.value("fade").toDouble(0));
    look.curve = points(o.value("curve"));
    look.curveR = points(o.value("curve_r"));
    look.curveG = points(o.value("curve_g"));
    look.curveB = points(o.value("curve_b"));
    triple(o.value("shadows_tint"), look.shadowsTint);
    triple(o.value("highlights_tint"), look.highlightsTint);
    look.mono = o.value("mono").toBool();
    triple(o.value("mono_mix"), look.monoMix);
    look.grain = float(o.value("grain").toDouble(0));
    look.vignette = float(o.value("vignette").toDouble(0));
    return look;
}

namespace looks {

const QList<Look> &all() {
    static const QList<Look> list = [] {
        QList<Look> loaded;
        QFile file(":/looks.json");
        if (file.open(QIODevice::ReadOnly))
            for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array())
                loaded << Look::fromJson(value.toObject());
        return loaded;
    }();
    return list;
}

const Look *find(const QString &name) {
    for (const Look &look : all())
        if (look.name.compare(name, Qt::CaseInsensitive) == 0)
            return &look;
    return nullptr;
}

QImage apply(const QImage &source, const Look &look, float strength) {
    strength = qBound(0.0f, strength, 1.0f);
    const QImage in = source.convertToFormat(QImage::Format_RGBA8888);
    if (in.isNull() || strength == 0)
        return in;
    const int w = in.width(), h = in.height();

    Table master, per[3];
    const bool hasMaster = curveTable(look.curve, &master);
    const bool hasPer[3] = {curveTable(look.curveR, &per[0]), curveTable(look.curveG, &per[1]),
                            curveTable(look.curveB, &per[2])};
    const float gain = std::pow(2.0f, look.exposure);
    const float balance[3] = {1 + look.warmth * 0.12f, 1 + look.tint * 0.06f, 1 - look.warmth * 0.12f};
    Table lut[3];
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) {
            float x = i / 255.0f * gain * balance[c];
            x = qBound(0.0f, (x - 0.5f) * look.contrast + 0.5f, 1.0f);
            if (hasMaster)
                x = master[size_t(std::lround(x * 255))];
            if (hasPer[c])
                x = per[c][size_t(std::lround(qBound(0.0f, x, 1.0f) * 255))];
            lut[c][size_t(i)] = x;
        }
    const float mixSum = qMax(0.001f, look.monoMix[0] + look.monoMix[1] + look.monoMix[2]);
    const float cx = w / 2.0f, cy = h / 2.0f;
    const float farthest = qMax(1.0f, cx * cx + cy * cy);
    const float grainScale = qMax(0.5f, qMax(w, h) / 2000.0f);

    QImage out(w, h, QImage::Format_RGBA8888);
    eachBand(h, [&](qint64 from, qint64 to) {
        for (int y = int(from); y < int(to); ++y) {
            const uchar *src = in.constScanLine(y);
            uchar *dst = out.scanLine(y);
            for (int x = 0; x < w; ++x, src += 4, dst += 4) {
                float c[3] = {lut[0][src[0]], lut[1][src[1]], lut[2][src[2]]};
                const float l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                if (look.mono) {
                    const float v = (c[0] * look.monoMix[0] + c[1] * look.monoMix[1] + c[2] * look.monoMix[2]) / mixSum;
                    c[0] = c[1] = c[2] = v;
                } else if (look.saturation != 1) {
                    for (float &channel : c)
                        channel = l + (channel - l) * look.saturation;
                }
                const float shadows = (1 - l) * (1 - l), highlights = l * l;
                for (int k = 0; k < 3; ++k) {
                    c[k] += look.shadowsTint[k] * shadows + look.highlightsTint[k] * highlights;
                    c[k] = look.fade + c[k] * (1 - look.fade);
                }
                if (look.vignette != 0) {
                    const float dx = x - cx, dy = y - cy;
                    const float v = 1 - look.vignette * ((dx * dx + dy * dy) / farthest);
                    for (float &channel : c)
                        channel *= v;
                }
                if (look.grain > 0) {
                    const float n = noise(quint32(x / grainScale), quint32(y / grainScale)) * look.grain
                                    * (0.4f + 2.4f * l * (1 - l));
                    for (float &channel : c)
                        channel += n;
                }
                for (int k = 0; k < 3; ++k) {
                    const float f = qBound(0.0f, c[k], 1.0f) * 255;
                    dst[k] = uchar(qBound(0L, std::lround(src[k] + (f - src[k]) * strength), 255L));
                }
                dst[3] = src[3];
            }
        }
    });
    return out;
}

QImage orient(const QImage &image, const Geometry &g) {
    QImage out = image;
    const int quarter = ((g.quarter % 4) + 4) % 4;
    if (quarter != 0)
        out = out.transformed(QTransform().rotate(90 * quarter));
    if (g.flipH || g.flipV)
        out = out.flipped((g.flipH ? Qt::Horizontal : Qt::Orientations()) | (g.flipV ? Qt::Vertical : Qt::Orientations()));
    return out;
}

QImage straighten(const QImage &image, double degrees) {
    if (std::abs(degrees) < 0.01 || image.isNull())
        return image;
    const double w = image.width(), h = image.height();
    const double t = qDegreesToRadians(degrees);
    const double s = std::abs(std::sin(t)), c = std::abs(std::cos(t));
    const double scale = qMax((w * c + h * s) / w, (w * s + h * c) / h);
    QImage out(image.size(), image.hasAlphaChannel() ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGB32);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(w / 2, h / 2);
    painter.rotate(degrees);
    painter.scale(scale, scale);
    painter.translate(-w / 2, -h / 2);
    painter.drawImage(0, 0, image);
    painter.end();
    return out;
}

QImage applyGeometry(const QImage &image, const Geometry &g, bool crop) {
    QImage out = straighten(orient(image, g), g.angle);
    if (crop && g.cropped() && !out.isNull()) {
        const int w = out.width(), h = out.height();
        const int x = qBound(0, qRound(g.crop.x() * w), w - 1);
        const int y = qBound(0, qRound(g.crop.y() * h), h - 1);
        out = out.copy(x, y, qBound(1, qRound(g.crop.width() * w), w - x), qBound(1, qRound(g.crop.height() * h), h - y));
    }
    return out;
}

QImage fit(const QImage &image, int maxEdge) {
    if (maxEdge <= 0 || image.isNull() || qMax(image.width(), image.height()) <= maxEdge)
        return image;
    return image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace looks
