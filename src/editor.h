#pragma once

#include <QImage>
#include <QObject>
#include <QRectF>
#include <QSize>
#include <QVariantList>
#include <QVariantMap>

#include "looks.h"

// Everything an edit is: the sliders, the look, and how the picture is turned
// and cut. Undo keeps whole copies of it.
struct EditState {
    int exposure = 0, contrast = 0, saturation = 0, warmth = 0, tint = 0; // -100 to 100
    int fade = 0, grain = 0, vignette = 0;                                // 0 to 100
    QString look;
    int strength = 100;
    Geometry geometry;
    QString aspect = "free";

    bool operator==(const EditState &o) const;
    int *slider(const QString &key);
    // The sliders as a recipe, applied before the film look.
    Look adjustment() const;
    bool adjusted() const;
};

// The editor: a preview kept in step with the sliders off the GUI thread, and
// a copy written at full size when asked. The original is never changed.
class Editor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY previewChanged)
    Q_PROPERTY(QString name READ name NOTIFY activeChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QString look READ look NOTIFY changed)
    Q_PROPERTY(int strength READ strength NOTIFY changed)
    Q_PROPERTY(double angle READ angle NOTIFY changed)
    Q_PROPERTY(QRectF crop READ crop NOTIFY changed)
    Q_PROPERTY(QString aspect READ aspect NOTIFY changed)
    Q_PROPERTY(double aspectRatio READ aspectRatio NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(int revision READ revision NOTIFY previewChanged)
    Q_PROPERTY(QSize previewSize READ previewSize NOTIFY previewChanged)
    Q_PROPERTY(int thumbRevision READ thumbRevision NOTIFY thumbsChanged)
    Q_PROPERTY(QVariantList looks READ looks CONSTANT)
    Q_PROPERTY(QVariantList groups READ groups CONSTANT)

public:
    explicit Editor(QObject *parent = nullptr);

    bool active() const { return !m_path.isEmpty(); }
    bool ready() const { return !m_base.isNull(); }
    QString name() const;
    QVariantMap values() const;
    QString look() const { return m_state.look; }
    int strength() const { return m_state.strength; }
    double angle() const { return m_state.geometry.angle; }
    QRectF crop() const { return m_state.geometry.crop; }
    QString aspect() const { return m_state.aspect; }
    double aspectRatio() const;
    bool canUndo() const { return m_at > 0; }
    bool canRedo() const { return m_at + 1 < m_history.size(); }
    bool dirty() const { return !(m_state == EditState()); }
    bool saving() const { return m_saving; }
    int revision() const { return m_revision; }
    QSize previewSize() const { return m_previewSize; }
    int thumbRevision() const { return m_thumbRevision; }
    QVariantList looks() const;
    QVariantList groups() const;
    const EditState &state() const { return m_state; }

    Q_INVOKABLE void begin(const QString &path);
    Q_INVOKABLE void end();
    // A slider moving; commit() when it is let go makes it one undo step.
    Q_INVOKABLE void set(const QString &key, int value);
    Q_INVOKABLE void setStrength(int value);
    Q_INVOKABLE void setAngle(double degrees);
    Q_INVOKABLE void setCrop(double x, double y, double width, double height);
    Q_INVOKABLE void commit();
    Q_INVOKABLE void setLook(const QString &name);
    Q_INVOKABLE void setAspect(const QString &aspect);
    Q_INVOKABLE void rotate(int quarters);
    Q_INVOKABLE void flip(bool horizontal);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void save();

    // The finished picture at any size: geometry, sliders, then the look.
    static QImage render(const QImage &source, const EditState &state, bool crop);
    // Where a copy of `path` is written: beside it, never over anything.
    static QString copyPath(const QString &path);
    // The whole save, for the command line and the tests too.
    static QString saveCopy(const QString &path, const EditState &state, QString *error);

signals:
    void activeChanged();
    void changed();
    void previewChanged();
    void thumbsChanged();
    void savingChanged();
    void saved(const QString &path);
    void failed(const QString &message);

private:
    void touch();
    void renderPreview();
    void renderThumbs();

    QString m_path;
    QImage m_base;
    EditState m_state;
    QList<EditState> m_history;
    int m_at = 0;
    int m_session = 0;
    int m_revision = 0;
    int m_thumbRevision = 0;
    QSize m_previewSize;
    bool m_rendering = false, m_renderAgain = false;
    bool m_saving = false;
};
