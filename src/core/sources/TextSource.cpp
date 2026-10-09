#include "core/sources/TextSource.h"
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <algorithm>

namespace {

QString jsonValueToText(const QJsonValue &v) {
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true")
                                               : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        const qint64 i = static_cast<qint64>(d);
        if (static_cast<double>(i) == d)
            return QString::number(i);
        return QString::number(d);
    }
    default: return QString();
    }
}

QSize canvasFor(const SourceDescriptor &desc) {
    return QSize(desc.canvasWidth > 0 ? desc.canvasWidth : 1280,
                 desc.canvasHeight > 0 ? desc.canvasHeight : 720);
}

} // namespace

QString TextSource::substitutePlaceholders(const QString &tmpl, const QJsonObject &params) {
    static const QRegularExpression tokenRx(QStringLiteral("\\{([a-zA-Z_][a-zA-Z0-9_]*)\\}"));
    QString out = tmpl;
    QRegularExpressionMatchIterator it = tokenRx.globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString key = match.captured(1);
        out.replace(match.captured(0), jsonValueToText(params.value(key)));
    }
    return out;
}

prism::TextStyle TextSource::styleFromDescriptor(const SourceDescriptor &desc) {
    return prism::textStyleFromJson(QJsonDocument::fromJson(desc.textStyleJson.toUtf8()).object());
}

QString TextSource::styleToJson(const prism::TextStyle &style) {
    return QString::fromUtf8(QJsonDocument(prism::textStyleToJson(style)).toJson(QJsonDocument::Compact));
}

QImage TextSource::renderDescriptor(const SourceDescriptor &desc, const QString &resolvedText) {
    const prism::TextStyle style = styleFromDescriptor(desc);
    const QSize canvas = canvasFor(desc);
    prism::TextAnimClock clock;
    clock.phase = prism::TextAnimClock::Phase::Hold;
    clock.totalSec = prism::textAnimationInSeconds(style, resolvedText, canvas);
    return prism::renderText(style, resolvedText, canvas, clock)
        .convertToFormat(QImage::Format_RGBA8888);
}

TextSource::TextSource(const SourceDescriptor &desc)
    : m_desc(desc)
    , m_style(styleFromDescriptor(desc))
{
    if (!desc.displayName.isEmpty())
        m_displayName = desc.displayName;
    refreshDurations();
    render();
    m_dirty = true;
}

void TextSource::setDataSource(std::shared_ptr<ScriptOutput> data) {
    m_data = std::move(data);
    m_lastVersion = 0;
}

void TextSource::setOnAir(bool on) {
    if (on == m_onAir)
        return;
    m_onAir = on;
    m_outActive = false;
    if (on)
        m_clock.start();
    m_dirty = true;
}

double TextSource::requestOut() {
    if (!m_onAir || m_outActive || m_outSec <= 0.0)
        return 0.0;
    m_outStartSec = m_clock.elapsed() / 1000.0;
    m_outActive = true;
    m_dirty = true;
    return m_outSec;
}

bool TextSource::outFinished() const {
    return m_onAir && m_outActive && m_clock.elapsed() / 1000.0 - m_outStartSec >= m_outSec;
}

prism::TextAnimClock TextSource::clockNow() const {
    using Phase = prism::TextAnimClock::Phase;
    prism::TextAnimClock c;
    if (!m_onAir) {
        c.totalSec = m_inSec;
        return c;
    }
    const double total = m_clock.elapsed() / 1000.0;
    if (m_outActive) {
        const double elapsed = std::min(total - m_outStartSec, m_outSec);
        c.phase = Phase::Out;
        c.phaseElapsedSec = elapsed;
        c.totalSec = m_outStartSec + elapsed;
        c.outWindowSec = m_outSec;
    } else if (total < m_inSec) {
        c.phase = Phase::In;
        c.phaseElapsedSec = total;
        c.totalSec = total;
    } else {
        c.totalSec = total;
    }
    return c;
}

bool TextSource::nextFrame() {
    bool textChanged = false;
    if (m_data) {
        const uint ver = m_data->version.load(std::memory_order_acquire);
        if (ver != m_lastVersion) {
            QString json;
            {
                QMutexLocker lock(&m_data->mutex);
                json = m_data->json;
            }
            m_lastVersion = ver;

            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
            if (doc.isObject())
                m_resolvedText = substitutePlaceholders(m_desc.textTemplate, doc.object());
            else
                m_resolvedText = m_desc.textTemplate;
            refreshDurations();
            textChanged = true;
        }
    }

    const prism::TextAnimClock clk = clockNow();
    const bool outDone = outFinished();
    const bool continuous = m_onAir && (m_style.animation.loop.isActive() || m_style.isAnimated() ||
                                        prism::textTimeDrivenPaint(m_style));
    const bool moving = m_onAir && (clk.phase == prism::TextAnimClock::Phase::In ||
                                    (clk.phase == prism::TextAnimClock::Phase::Out && !outDone));
    if (!(textChanged || m_dirty || continuous || moving ||
          clk.phase != m_lastPhase || outDone != m_lastOutDone))
        return false;

    m_dirty = false;
    render();
    return true;
}

void TextSource::refreshDurations() {
    const QString text = m_resolvedText.isEmpty() ? m_desc.textTemplate : m_resolvedText;
    const QSize canvas = canvasFor(m_desc);
    m_inSec = prism::textAnimationInSeconds(m_style, text, canvas);
    m_outSec = prism::textAnimationOutSeconds(m_style, text, canvas);
}

void TextSource::render() {
    const prism::TextAnimClock clk = clockNow();
    m_lastPhase = clk.phase;
    m_lastOutDone = outFinished();
    m_image = prism::renderText(m_style,
                                m_resolvedText.isEmpty() ? m_desc.textTemplate : m_resolvedText,
                                canvasFor(m_desc), clk)
                  .convertToFormat(QImage::Format_RGBA8888);
}
