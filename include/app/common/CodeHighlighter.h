#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QList>

/// Regex-based syntax highlighter for the code editors (shader, Lua script).
/// Parented to the document it highlights, so no ownership management
/// is needed at the call site.
class CodeHighlighter : public QSyntaxHighlighter {
public:
    enum class Language { Glsl, Lua };

    CodeHighlighter(Language lang, QTextDocument *doc);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void setupGlsl();
    void setupLua();

    QList<Rule> m_rules;
    QRegularExpression m_blockCommentStart;
    QRegularExpression m_blockCommentEnd;
    QTextCharFormat m_commentFormat;
};
