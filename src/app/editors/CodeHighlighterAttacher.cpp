#include "app/editors/CodeHighlighterAttacher.h"

#include "ui/common/CodeHighlighter.h"

#include <QQuickTextDocument>
#include <QTextDocument>

CodeHighlighterAttacher::CodeHighlighterAttacher(QObject *parent)
    : QObject(parent)
{
}

void CodeHighlighterAttacher::setTextDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    if (m_highlighter)
        delete m_highlighter.data();
    attach();
}

void CodeHighlighterAttacher::setLanguage(const QString &language)
{
    if (m_language == language)
        return;
    m_language = language;
    if (m_highlighter)
        delete m_highlighter.data();
    attach();
}

void CodeHighlighterAttacher::attach()
{
    if (!m_document || !m_document->textDocument() || m_language.isEmpty())
        return;
    const auto lang = m_language == QLatin1String("lua") ? CodeHighlighter::Language::Lua
                                                         : CodeHighlighter::Language::Glsl;
    m_highlighter = new CodeHighlighter(lang, m_document->textDocument());
}
