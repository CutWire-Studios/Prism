#pragma once

#include "core/MediaSource.h"
#include "core/ScriptOutput.h"
#include <QColor>
#include <QFont>
#include <QImage>
#include <memory>

class TextSource : public MediaSource {
public:
    explicit TextSource(const QString &textTemplate,
                        const QFont &font = QFont(),
                        const QColor &color = Qt::white,
                        Qt::Alignment align = Qt::AlignCenter,
                        const QSize &canvas = QSize(1280, 720),
                        bool bgTransparent = true,
                        const QColor &bgColor = Qt::black);

    void setTemplate(const QString &textTemplate);
    void setFont(const QFont &font);
    void setColor(const QColor &color);
    void setAlignment(Qt::Alignment align);
    void setCanvasSize(const QSize &size);
    void setBackgroundTransparent(bool transparent);
    void setBackgroundColor(const QColor &color);
    void setDataSource(std::shared_ptr<ScriptOutput> data);

    Type type() const override { return Type::Text; }
    bool isReady() const override { return !m_image.isNull(); }
    QSize frameSize() const override { return m_canvas; }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_image.constBits());
    }
    bool nextFrame() override;
    QString displayName() const override { return m_displayName; }
    bool hasAlpha() const override { return true; }

private:
    void render();

    QString m_template;
    QFont m_font;
    QColor m_color;
    Qt::Alignment m_align;
    QSize m_canvas;
    bool m_bgTransparent = true;
    QColor m_bgColor;
    QImage m_image;
    QString m_displayName = QStringLiteral("Text");
    std::shared_ptr<ScriptOutput> m_data;
    uint m_lastVersion = 0;
    QString m_resolvedText;
};
