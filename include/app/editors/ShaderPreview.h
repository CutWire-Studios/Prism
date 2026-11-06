#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <memory>

class ShaderSource;

// Shader presets, and an offscreen render of the GLSL under edit drawn into the item.
class ShaderPreview : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit ShaderPreview(QQuickItem *parent = nullptr);
    ~ShaderPreview() override;

    void paint(QPainter *painter) override;

    QStringList presetNames() const;
    Q_INVOKABLE QString presetCode(int row) const;
    // -1 when the code matches no preset.
    Q_INVOKABLE int presetIndexFor(const QString &code) const;

    QString error() const { return m_error; }

    Q_INVOKABLE void refresh(const QString &code);

signals:
    void errorChanged();

private:
    void setError(const QString &error);

    std::unique_ptr<ShaderSource> m_source;
    QImage m_frame;
    QString m_error;
};
