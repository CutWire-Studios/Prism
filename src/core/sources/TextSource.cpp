#include "core/sources/TextSource.h"
#include <QJsonDocument>
#include <QJsonValue>
#include <QLinearGradient>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>

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

void applyFormat(QTextDocument &doc, const QTextCharFormat &cf, int lineHeightPercent) {
    QTextCursor cursor(&doc);
    cursor.select(QTextCursor::Document);
    cursor.mergeCharFormat(cf);
    QTextBlockFormat bf;
    bf.setLineHeight(lineHeightPercent, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(bf);
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

QImage TextSource::renderDescriptor(const SourceDescriptor &desc, const QString &resolvedText) {
    const QSize canvas(desc.canvasWidth > 0 ? desc.canvasWidth : 1280,
                       desc.canvasHeight > 0 ? desc.canvasHeight : 720);

    QImage img(canvas, QImage::Format_RGBA8888);
    img.fill(desc.textBgTransparent ? QColor(Qt::transparent) : desc.textBgColor);
    if (resolvedText.isEmpty())
        return img;

    QFont font(desc.fontFamily.isEmpty() ? QStringLiteral("Sans Serif") : desc.fontFamily,
               desc.fontSize > 0 ? desc.fontSize : 48);
    font.setBold(desc.textBold);
    font.setItalic(desc.textItalic);
    font.setUnderline(desc.textUnderline);
    if (desc.textLetterSpacing != 0)
        font.setLetterSpacing(QFont::PercentageSpacing, 100.0 + desc.textLetterSpacing);

    const Qt::Alignment align(desc.textAlign);

    QTextDocument doc;
    doc.setDefaultFont(font);
    QTextOption opt;
    opt.setAlignment(align & Qt::AlignHorizontal_Mask);
    opt.setWrapMode(QTextOption::WordWrap);
    doc.setDefaultTextOption(opt);
    doc.setDocumentMargin(16);
    doc.setTextWidth(canvas.width());
    doc.setPlainText(resolvedText);

    const int lineHeight = desc.textLineHeight > 0 ? desc.textLineHeight : 100;
    const qreal docH = doc.size().height();
    qreal y = 0;
    if (align & Qt::AlignVCenter)
        y = (canvas.height() - docH) / 2.0;
    else if (align & Qt::AlignBottom)
        y = canvas.height() - docH;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.translate(0, y);

    const QPen noOutline(Qt::NoPen);
    const bool hasOutline = desc.textOutlineWidth > 0;

    if (desc.textShadowDx != 0 || desc.textShadowDy != 0) {
        QTextCharFormat shadowFmt;
        shadowFmt.setForeground(desc.textShadowColor);
        shadowFmt.setTextOutline(hasOutline
            ? QPen(desc.textShadowColor, desc.textOutlineWidth,
                   Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
            : noOutline);
        applyFormat(doc, shadowFmt, lineHeight);
        p.save();
        p.translate(desc.textShadowDx, desc.textShadowDy);
        doc.drawContents(&p);
        p.restore();
    }

    QBrush fill(desc.color.isValid() ? desc.color : QColor(Qt::white));
    if (desc.textGradient) {
        QLinearGradient g;
        const qreal w = canvas.width();
        switch (desc.textGradientDir) {
        case 1:  g = QLinearGradient(0, 0, w, 0);       break; // horizontal
        case 2:  g = QLinearGradient(0, 0, w, docH);    break; // diagonal ↘
        case 3:  g = QLinearGradient(0, docH, w, 0);    break; // diagonal ↗
        default: g = QLinearGradient(0, 0, 0, docH);    break; // vertical
        }
        g.setColorAt(0.0, desc.color.isValid() ? desc.color : QColor(Qt::white));
        g.setColorAt(1.0, desc.textColor2.isValid() ? desc.textColor2 : QColor(Qt::white));
        fill = QBrush(g);
    }

    QTextCharFormat mainFmt;
    mainFmt.setForeground(fill);
    mainFmt.setTextOutline(hasOutline
        ? QPen(desc.textOutlineColor, desc.textOutlineWidth,
               Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
        : noOutline);
    applyFormat(doc, mainFmt, lineHeight);
    doc.drawContents(&p);

    return img;
}

TextSource::TextSource(const SourceDescriptor &desc)
    : m_desc(desc)
{
    if (!desc.displayName.isEmpty())
        m_displayName = desc.displayName;
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
        m_resolvedText = substitutePlaceholders(m_desc.textTemplate, doc.object());
    else
        m_resolvedText = m_desc.textTemplate;

    render();
    return true;
}

void TextSource::render() {
    m_image = renderDescriptor(
        m_desc, m_resolvedText.isEmpty() ? m_desc.textTemplate : m_resolvedText);
}
