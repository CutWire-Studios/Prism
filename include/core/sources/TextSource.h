#pragma once

#include "core/sources/MediaSource.h"
#include "core/sources/SourceDescriptor.h"
#include "core/scripting/ScriptOutput.h"
#include "core/render/SkiaRender.h"
#include "core/text/TextStyle.h"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <memory>

/// Renders a text template to an RGBA frame. Placeholders like {now} are filled
/// from a ScriptOutput (see core/scripting), enabling live data-driven captions.
class TextSource : public MediaSource {
public:
    explicit TextSource(const SourceDescriptor &desc);

    void setDataSource(std::shared_ptr<ScriptOutput> data);

    /// Renders @p resolvedText with the text style in @p desc onto a canvas
    /// image, at the settled (held) pose. Shared by the live source and the
    /// TextEditDialog preview so the preview is pixel-identical to program output.
    static QImage renderDescriptor(const SourceDescriptor &desc,
                                   const QString &resolvedText);

    /// Parses / serializes SourceDescriptor::textStyleJson (empty or invalid JSON gives the default style).
    static prism::TextStyle styleFromDescriptor(const SourceDescriptor &desc);
    static QString styleToJson(const prism::TextStyle &style);

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

    /// While off air the held pose is shown. The false -> true edge restarts the In phase.
    void setOnAir(bool on) override;
    /// Starts the Out phase and returns its length; 0 when there is no Out animation, when the
    /// source is off air, or when Out is already running.
    double requestOut() override;

    /// Where the animation is right now (testing and tools).
    prism::TextAnimClock::Phase animPhase() const { return clockNow().phase; }
    bool outFinished() const;

private:
    prism::TextAnimClock clockNow() const;
    void render();
    void refreshDurations();

    prism::TextStyle m_style;
    QElapsedTimer m_clock;
    bool m_onAir = false;
    bool m_outActive = false;
    double m_outStartSec = 0.0;
    double m_inSec = 0.0;
    double m_outSec = 0.0;
    bool m_dirty = false;
    prism::TextAnimClock::Phase m_lastPhase = prism::TextAnimClock::Phase::Hold;
    bool m_lastOutDone = false;

    SourceDescriptor m_desc;
    QImage m_image;
    QString m_displayName = QStringLiteral("Text");
    std::shared_ptr<ScriptOutput> m_data;
    uint m_lastVersion = 0;
    QString m_resolvedText;
};
