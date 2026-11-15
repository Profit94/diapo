#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

// A film look is a recipe: tone, colour, grain. The built-in ones are named
// after the Leica, Fujifilm and Kodak renderings they follow, and the editor's
// own sliders are one more recipe applied the same way.
struct Look {
    QString name;
    QString group;
    float exposure = 0;   // stops
    float contrast = 1;   // 1 is unchanged
    float saturation = 1; // 1 is unchanged
    float warmth = 0;     // positive is warmer
    float tint = 0;       // positive is greener
    float fade = 0;       // lifts the blacks, 0 to 0.25
    QList<QPointF> curve, curveR, curveG, curveB; // input to output, 0 to 1
    float shadowsTint[3] = {0, 0, 0};
    float highlightsTint[3] = {0, 0, 0};
    bool mono = false;
    float monoMix[3] = {0.30f, 0.59f, 0.11f};
    float grain = 0;
    float vignette = 0;

    static Look fromJson(const QJsonObject &object);
};

// How the picture is turned and cut before any look.
struct Geometry {
    int quarter = 0; // quarter turns clockwise
    bool flipH = false;
    bool flipV = false;
    double angle = 0;            // straighten, in degrees
    QRectF crop{0, 0, 1, 1};     // in 0 to 1 of the straightened picture
    bool operator==(const Geometry &o) const {
        return quarter == o.quarter && flipH == o.flipH && flipV == o.flipV && angle == o.angle && crop == o.crop;
    }
    bool cropped() const { return crop != QRectF(0, 0, 1, 1); }
};

namespace looks {

const QList<Look> &all();
const Look *find(const QString &name);
// `strength` from 0 to 1 blends the look over the original.
QImage apply(const QImage &image, const Look &look, float strength = 1);

QImage orient(const QImage &image, const Geometry &geometry);
// Turned around its middle and zoomed just enough that no empty corner shows.
QImage straighten(const QImage &image, double degrees);
QImage applyGeometry(const QImage &image, const Geometry &geometry, bool crop = true);
// Its longest side at most maxEdge; untouched when 0 or already smaller.
QImage fit(const QImage &image, int maxEdge);

} // namespace looks
