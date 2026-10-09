#include "ui/common/CodeHighlighter.h"

namespace {

// Palette tuned for the dark theme's #1E1E1E editor background
const QColor kKeyword(0xC5, 0x86, 0xC0);
const QColor kType(0x4E, 0xC9, 0xB0);
const QColor kBuiltin(0x9C, 0xDC, 0xFE);
const QColor kFunction(0xDC, 0xDC, 0xAA);
const QColor kNumber(0xB5, 0xCE, 0xA8);
const QColor kString(0xCE, 0x91, 0x78);
const QColor kComment(0x6A, 0x99, 0x55);
const QColor kPreprocessor(0x56, 0x9C, 0xD6);

QTextCharFormat fmt(const QColor &color, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(color);
    if (bold) f.setFontWeight(QFont::DemiBold);
    f.setFontItalic(italic);
    return f;
}

QRegularExpression keywordRx(const char *words) {
    return QRegularExpression(QStringLiteral("\\b(?:%1)\\b")
                                  .arg(QLatin1String(words)));
}

} // namespace

CodeHighlighter::CodeHighlighter(Language lang, QTextDocument *doc)
    : QSyntaxHighlighter(doc)
{
    m_commentFormat = fmt(kComment, false, true);
    switch (lang) {
    case Language::Glsl: setupGlsl(); break;
    case Language::Lua:  setupLua();  break;
    }
}

void CodeHighlighter::setupGlsl()
{
    m_rules.append({ QRegularExpression(QStringLiteral(
                         "\\b[A-Za-z_]\\w*(?=\\s*\\()")),
                     fmt(kFunction) });
    m_rules.append({ keywordRx(
                         "if|else|for|while|do|return|break|continue|discard"
                         "|struct|const|uniform|varying|attribute|in|out|inout"
                         "|layout|precision|highp|mediump|lowp|true|false"),
                     fmt(kKeyword) });
    m_rules.append({ keywordRx(
                         "void|float|double|int|uint|bool"
                         "|[biud]?vec[234]|mat[234](?:x[234])?"
                         "|sampler[123]D|samplerCube|sampler2DArray"),
                     fmt(kType) });
    m_rules.append({ QRegularExpression(QStringLiteral("\\bgl_\\w+\\b")),
                     fmt(kBuiltin) });
    m_rules.append({ QRegularExpression(QStringLiteral(
                         "\\b\\d+\\.?\\d*(?:[eE][+-]?\\d+)?[fFuU]?\\b")),
                     fmt(kNumber) });
    m_rules.append({ QRegularExpression(QStringLiteral("^\\s*#\\s*\\w+.*")),
                     fmt(kPreprocessor) });
    m_rules.append({ QRegularExpression(QStringLiteral("//[^\n]*")),
                     m_commentFormat });
    m_blockCommentStart = QRegularExpression(QStringLiteral("/\\*"));
    m_blockCommentEnd   = QRegularExpression(QStringLiteral("\\*/"));
}

void CodeHighlighter::setupLua()
{
    m_rules.append({ QRegularExpression(QStringLiteral(
                         "\\b[A-Za-z_]\\w*(?=\\s*\\()")),
                     fmt(kFunction) });
    m_rules.append({ keywordRx(
                         "and|break|do|else|elseif|end|false|for|function|goto"
                         "|if|in|local|nil|not|or|repeat|return|then|true"
                         "|until|while"),
                     fmt(kKeyword) });
    m_rules.append({ keywordRx(
                         "print|pairs|ipairs|next|type|tostring|tonumber"
                         "|select|pcall|xpcall|error|assert|require|rawget"
                         "|rawset|setmetatable|getmetatable"
                         "|math|string|table|os|io|coroutine"),
                     fmt(kBuiltin) });
    m_rules.append({ QRegularExpression(QStringLiteral(
                         "\\b(?:0[xX][0-9a-fA-F]+|\\d+\\.?\\d*(?:[eE][+-]?\\d+)?)\\b")),
                     fmt(kNumber) });
    m_rules.append({ QRegularExpression(QStringLiteral(
                         "\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"|'[^'\\\\]*(?:\\\\.[^'\\\\]*)*'")),
                     fmt(kString) });
    m_rules.append({ QRegularExpression(QStringLiteral("--(?!\\[\\[)[^\n]*")),
                     m_commentFormat });
    m_blockCommentStart = QRegularExpression(QStringLiteral("--\\[\\["));
    m_blockCommentEnd   = QRegularExpression(QStringLiteral("\\]\\]"));
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }

    // Multi-line comments carry state across blocks (1 = inside comment)
    setCurrentBlockState(0);
    int start = 0;
    if (previousBlockState() != 1) {
        const QRegularExpressionMatch m = m_blockCommentStart.match(text);
        start = m.hasMatch() ? m.capturedStart() : -1;
    }
    while (start >= 0) {
        const QRegularExpressionMatch end = m_blockCommentEnd.match(text, start);
        int len;
        if (end.hasMatch()) {
            len = end.capturedEnd() - start;
        } else {
            setCurrentBlockState(1);
            len = text.length() - start;
        }
        setFormat(start, len, m_commentFormat);
        const QRegularExpressionMatch next =
            m_blockCommentStart.match(text, start + len);
        start = next.hasMatch() ? next.capturedStart() : -1;
    }
}
