#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QSyntaxHighlighter>
#include <QtQml/qqmlregistration.h>

#include <QQuickTextDocument>

// Attaches a CodeHighlighter to the document of a QML TextArea.
class CodeHighlighterAttacher : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument)
    Q_PROPERTY(QString language READ language WRITE setLanguage)

public:
    explicit CodeHighlighterAttacher(QObject *parent = nullptr);

    QQuickTextDocument *textDocument() const { return m_document; }
    void setTextDocument(QQuickTextDocument *document);

    QString language() const { return m_language; }
    void setLanguage(const QString &language);

private:
    void attach();

    QQuickTextDocument *m_document = nullptr;
    QString m_language;
    QPointer<QSyntaxHighlighter> m_highlighter;
};
