#pragma once

#include "core/sources/SourceDescriptor.h"
#include "core/text/ShapeStyle.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QTimer;
class ClipGraphController;

// Backend of the Shape source editor: the shape gallery, the geometry knobs of the selected kind,
// the shading layer stack and the live preview, which is rendered by the same code as program
// output. Time-driven styles redraw on a clock.
class ShapeSourceBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList kinds READ kinds CONSTANT)
    Q_PROPERTY(QString kind READ kind NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap geometry READ geometry NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap sections READ sections NOTIFY styleChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY styleChanged)
    Q_PROPERTY(QImage preview READ preview NOTIFY previewChanged)

public:
    explicit ShapeSourceBackend(QObject *parent = nullptr);

    QVariantList kinds() const { return m_kinds; }
    QString kind() const;
    QVariantMap geometry() const;
    QVariantMap sections() const;
    QVariantList layers() const;
    QImage preview() const { return m_preview; }

    Q_INVOKABLE void open(ClipGraphController *graph, quint64 nodeId);
    Q_INVOKABLE void setKind(const QString &kind);
    Q_INVOKABLE void setGeometry(const QString &name, double value);
    Q_INVOKABLE void setLayers(const QVariantList &layers);
    Q_INVOKABLE bool commit();

signals:
    void styleChanged();
    void previewChanged();

private:
    SourceDescriptor resultDescriptor() const;
    void updateClock();
    void scheduleRender();
    void updatePreview();

    ClipGraphController *m_graph = nullptr;
    quint64 m_nodeId = 0;
    SourceDescriptor m_initial;
    prism::ShapeStyle m_style;
    QVariantList m_kinds;
    QImage m_preview;
    QTimer *m_renderTimer = nullptr;
    QTimer *m_clockTimer = nullptr;
    QElapsedTimer m_clock;
};
