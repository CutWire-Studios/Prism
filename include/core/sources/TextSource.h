#pragma once

#include "core/sources/MediaSource.h"
#include "core/sources/SourceDescriptor.h"
#include "core/scripting/ScriptOutput.h"
#include <QImage>
#include <QJsonObject>
#include <memory>

/// Renders a text template to an RGBA frame. Placeholders like {now} are filled
/// from a ScriptOutput (see core/scripting), enabling live data-driven captions.
class TextSource : public MediaSource {
public:
    explicit TextSource(const SourceDescriptor &desc);

    void setDataSource(std::shared_ptr<ScriptOutput> data);

    /// Renders @p resolvedText with the text styling in @p desc onto a canvas
    /// image. Shared by the live source and the TextEditDialog preview so the
    /// preview is pixel-identical to program output.
    static QImage renderDescriptor(const SourceDescriptor &desc,
                                   const QString &resolvedText);

    /// Replaces {name} tokens with values from @p params. Numbers and booleans
    /// are formatted; missing keys become empty.
    static QString substitutePlaceholders(const QString &tmpl,
                                          const QJsonObject &params);

    Type type() const override { return Type::Text; }
    bool isReady() const override { return !m_image.isNull(); }
    QSize frameSize() const override { return m_image.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_image.constBits());
    }
    bool nextFrame() override;
    QString displayName() const override { return m_displayName; }
    bool hasAlpha() const override { return true; }

private:
    void render();

    SourceDescriptor m_desc;
    QImage m_image;
    QString m_displayName = QStringLiteral("Text");
    std::shared_ptr<ScriptOutput> m_data;
    uint m_lastVersion = 0;
    QString m_resolvedText;
};
