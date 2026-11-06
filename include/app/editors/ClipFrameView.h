#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// A still frame for the editors: one video frame at `time` of `path`, an image file, or the
// reference frame of an Input node. Decoding runs on the thread pool; the latest request wins.
class ClipFrameView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY sourceChanged)
    Q_PROPERTY(double time READ time WRITE setTime NOTIFY sourceChanged)
    /// Input node whose frame to show (file sources are decoded at their trim start, any other
    /// kind falls back to the node thumbnail). Ignored while `path` is set.
    Q_PROPERTY(quint64 nodeId READ nodeId WRITE setNodeId NOTIFY sourceChanged)
    Q_PROPERTY(int maxSize READ maxSize WRITE setMaxSize NOTIFY sourceChanged)
    Q_PROPERTY(qreal imageAspect READ imageAspect NOTIFY imageChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY imageChanged)

public:
    explicit ClipFrameView(QQuickItem *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    double time() const { return m_time; }
    void setTime(double time);
    quint64 nodeId() const { return m_nodeId; }
    void setNodeId(quint64 id);
    int maxSize() const { return m_maxSize; }
    void setMaxSize(int size);
    qreal imageAspect() const;
    bool ready() const { return !m_image.isNull(); }

    void paint(QPainter *painter) override;

signals:
    void sourceChanged();
    void imageChanged();

private:
    void reload();
    void request();
    void apply(const QImage &image, int generation);

    QString m_path;
    double m_time = 0.0;
    quint64 m_nodeId = 0;
    int m_maxSize = 1280;
    QImage m_image;
    int m_generation = 0;
    QTimer m_debounce;
};
