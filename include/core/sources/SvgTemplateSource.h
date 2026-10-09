#pragma once

#include "core/scripting/ScriptOutput.h"
#include "core/sources/MediaSource.h"
#include "core/sources/SourceDescriptor.h"
#include "core/sources/SvgTemplates.h"
#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <memory>

/// Renders an SVG template to an RGBA frame. {name} tokens are filled from, in increasing
/// priority: the template's declared defaults, the descriptor's overrides (svgParamsJson), the
/// latest ScriptOutput, and the built-in tokens. See SvgTemplates.h for the file format.
///
/// Built-ins: {time} HH:mm:ss, {time12} h:mm AP, {date}, {countdown} and {countdownColor}.
/// The countdown runs towards the `target` param: an ISO date-time, or a number of seconds
/// counted down from when the source was created. {countdownColor} is the `urgentColor` param
/// (default #f44336) in the last 60 seconds and the `color` param (default #ff6f00) before.
class SvgTemplateSource : public MediaSource {
public:
    explicit SvgTemplateSource(const SourceDescriptor &desc);

    void setDataSource(std::shared_ptr<ScriptOutput> data);

    /// Renders the descriptor's template once with @p data layered over its parameters, as a
    /// straight-alpha frame. Shared by the live source and the editor's preview and thumbnails.
    static QImage renderDescriptor(const SourceDescriptor &desc, const QJsonObject &data = {});

    /// Param overrides stored in SourceDescriptor::svgParamsJson.
    static QJsonObject paramsFromDescriptor(const SourceDescriptor &desc);
    static QString paramsToJson(const QJsonObject &params);

    /// Render scale (reserved param "scale", default 1).
    static double scaleOf(const QJsonObject &params);

    Type type() const override { return Type::SvgTemplate; }
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
    QByteArray m_svg;
    QString m_svgText;
    QJsonObject m_base;        // defaults + user overrides
    QJsonObject m_scriptData;
    double m_scale = 1.0;
    bool m_usesBuiltins = false;
    qint64 m_startSecs = 0;
    qint64 m_lastSecs = 0;
    bool m_dirty = true;
    QImage m_image;
    QString m_displayName = QStringLiteral("SVG Template");
    std::shared_ptr<ScriptOutput> m_data;
    uint m_lastVersion = 0;
};
