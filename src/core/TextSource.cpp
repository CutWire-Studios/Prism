#include "core/TextSource.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QRegularExpression>

namespace {

QString substituteParams(const QString &tmpl, const QJsonObject &params) {
    static const QRegularExpression tokenRx(QStringLiteral("\\{([a-zA-Z_][a-zA-Z0-9_]*)\\}"));
    QString out = tmpl;
    QRegularExpressionMatchIterator it = tokenRx.globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString key = match.captured(1);
        const QString replacement = params.value(key).toString();
        out.replace(match.captured(0), replacement);
    }
    return out;
}

} // namespace

TextSource::TextSource(const QString &textTemplate,
                       const QFont &font,
                       const QColor &color,
                       Qt::Alignment align,
                       const QSize &canvas,
                       bool bgTransparent,
                       const QColor &bgColor)
    : m_template(textTemplate)
    , m_font(font)
    , m_color(color)
    , m_align(align)
    , m_canvas(canvas)
    , m_bgTransparent(bgTransparent)
    , m_bgColor(bgColor)
{
    if (m_font.family().isEmpty())
        m_font.setFamily(QStringLiteral("Sans Serif"));
    if (m_font.pointSize() <= 0)
        m_font.setPointSize(48);
    render();
}

void TextSource::setTemplate(const QString &textTemplate) {
    m_template = textTemplate;
    render();
}

void TextSource::setFont(const QFont &font) {
    m_font = font;
    render();
}

void TextSource::setColor(const QColor &color) {
    m_color = color;
    render();
}

void TextSource::setAlignment(Qt::Alignment align) {
    m_align = align;
    render();
}

void TextSource::setCanvasSize(const QSize &size) {
    m_canvas = size;
    render();
}

void TextSource::setBackgroundTransparent(bool transparent) {
    m_bgTransparent = transparent;
    render();
}

void TextSource::setBackgroundColor(const QColor &color) {
    m_bgColor = color;
    render();
}

void TextSource::setDataSource(std::shared_ptr<ScriptOutput> data) {
    m_data = std::move(data);
    m_lastVersion = 0;
}

bool TextSource::nextFrame() {
    if (!m_data)
        return false;

    const uint ver = m_data->version.load(std::memory_order_acquire);
    if (ver == m_lastVersion)
        return false;

    QString json;
    {
        QMutexLocker lock(&m_data->mutex);
        json = m_data->json;
    }

    m_lastVersion = ver;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (doc.isObject())
        m_resolvedText = substituteParams(m_template, doc.object());
    else
        m_resolvedText = m_template;

    render();
    return true;
}

void TextSource::render() {
    if (m_canvas.isEmpty()) {
        m_image = QImage();
        return;
    }

    m_image = QImage(m_canvas, QImage::Format_RGBA8888);
    if (m_bgTransparent)
        m_image.fill(Qt::transparent);
    else
        m_image.fill(m_bgColor);

    const QString text = m_resolvedText.isEmpty() ? m_template : m_resolvedText;
    if (text.isEmpty())
        return;

    QPainter p(&m_image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(m_font);
    p.setPen(m_color);
    p.drawText(QRect(QPoint(0, 0), m_canvas), static_cast<int>(m_align), text);
}
