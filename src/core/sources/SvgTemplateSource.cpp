#include "core/sources/SvgTemplateSource.h"
#include "core/render/SkiaRender.h"
#include <QJsonDocument>
#include <QLocale>
#include <QMutexLocker>
#include <QTime>
#include <algorithm>

namespace {

constexpr int kUrgentSeconds = 60;

qint64 remainingSeconds(const QJsonObject &values, qint64 nowSecs, qint64 startSecs) {
    const QJsonValue t = values.value(QStringLiteral("target"));
    const QString text = t.isString() ? t.toString().trimmed() : QString::number(t.toDouble(600.0));
    const QDateTime when = QDateTime::fromString(text, Qt::ISODate);
    if (when.isValid())
        return when.toSecsSinceEpoch() - nowSecs;
    bool ok = false;
    const double duration = text.toDouble(&ok);
    return qRound64(ok ? duration : 600.0) - (nowSecs - startSecs);
}

QString formatCountdown(qint64 remaining) {
    const qint64 s = std::max<qint64>(remaining, 0);
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
            .arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QJsonObject resolveValues(const QJsonObject &base, const QJsonObject &script, qint64 nowSecs,
                          qint64 startSecs) {
    QJsonObject values = base;
    for (auto it = script.begin(); it != script.end(); ++it)
        values.insert(it.key(), it.value());

    const QDateTime now = QDateTime::fromSecsSinceEpoch(nowSecs);
    const QLocale locale;
    values.insert(QStringLiteral("time"), now.time().toString(QStringLiteral("HH:mm:ss")));
    values.insert(QStringLiteral("time12"), locale.toString(now.time(), QStringLiteral("h:mm AP")));
    values.insert(QStringLiteral("date"), locale.toString(now.date(), QStringLiteral("dddd, MMMM d, yyyy")));

    const qint64 remaining = remainingSeconds(values, nowSecs, startSecs);
    values.insert(QStringLiteral("countdown"), formatCountdown(remaining));
    const QJsonValue urgent = values.value(QStringLiteral("urgentColor"));
    const QJsonValue normal = values.value(QStringLiteral("color"));
    values.insert(QStringLiteral("countdownColor"),
                  remaining <= kUrgentSeconds
                      ? (urgent.isString() ? urgent.toString() : QStringLiteral("#f44336"))
                      : (normal.isString() ? normal.toString() : QStringLiteral("#ff6f00")));
    return values;
}

QJsonObject baseValues(const SourceDescriptor &desc, const QByteArray &svg) {
    QJsonObject base = prism::svgTemplateDefaults(prism::svgTemplateInfo(desc.svgTemplateId, svg));
    const QJsonObject overrides = SvgTemplateSource::paramsFromDescriptor(desc);
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        base.insert(it.key(), it.value());
    return base;
}

QImage renderSvgText(const QString &svgText, const QJsonObject &values, double scale) {
    return prism::renderSvg(prism::substituteSvgTokens(svgText, values).toUtf8(), scale)
        .convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

QJsonObject SvgTemplateSource::paramsFromDescriptor(const SourceDescriptor &desc) {
    return QJsonDocument::fromJson(desc.svgParamsJson.toUtf8()).object();
}

QString SvgTemplateSource::paramsToJson(const QJsonObject &params) {
    return QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
}

double SvgTemplateSource::scaleOf(const QJsonObject &params) {
    const QJsonValue v = params.value(QStringLiteral("scale"));
    const double s = v.isString() ? v.toString().toDouble() : v.toDouble(1.0);
    return s > 0.0 ? std::min(s, 8.0) : 1.0;
}

QImage SvgTemplateSource::renderDescriptor(const SourceDescriptor &desc, const QJsonObject &data) {
    const QByteArray svg = prism::svgTemplateBytes(desc.svgTemplateId);
    if (svg.isEmpty())
        return {};
    const QJsonObject base = baseValues(desc, svg);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return renderSvgText(QString::fromUtf8(svg), resolveValues(base, data, now, now), scaleOf(base));
}

SvgTemplateSource::SvgTemplateSource(const SourceDescriptor &desc)
    : m_desc(desc)
    , m_svg(prism::svgTemplateBytes(desc.svgTemplateId))
{
    if (!desc.displayName.isEmpty())
        m_displayName = desc.displayName;
    m_svgText = QString::fromUtf8(m_svg);
    m_base = baseValues(desc, m_svg);
    m_scale = scaleOf(m_base);
    m_usesBuiltins = prism::svgUsesBuiltinTokens(m_svg);
    m_startSecs = m_lastSecs = QDateTime::currentSecsSinceEpoch();
    if (!m_svg.isEmpty())
        render();
}

void SvgTemplateSource::setDataSource(std::shared_ptr<ScriptOutput> data) {
    m_data = std::move(data);
    m_lastVersion = 0;
    if (!m_data)
        m_scriptData = {};
    m_dirty = true;
}

bool SvgTemplateSource::nextFrame() {
    if (m_svg.isEmpty())
        return false;

    if (m_data) {
        const uint ver = m_data->version.load(std::memory_order_acquire);
        if (ver != m_lastVersion) {
            QString json;
            {
                QMutexLocker lock(&m_data->mutex);
                json = m_data->json;
            }
            m_lastVersion = ver;
            m_scriptData = QJsonDocument::fromJson(json.toUtf8()).object();
            m_dirty = true;
        }
    }

    if (m_usesBuiltins && QDateTime::currentSecsSinceEpoch() != m_lastSecs)
        m_dirty = true;
    if (!m_dirty)
        return false;

    render();
    return true;
}

void SvgTemplateSource::render() {
    m_dirty = false;
    m_lastSecs = QDateTime::currentSecsSinceEpoch();
    const QImage img = renderSvgText(m_svgText, resolveValues(m_base, m_scriptData, m_lastSecs, m_startSecs),
                                     m_scale);
    if (!img.isNull())
        m_image = img;
}
