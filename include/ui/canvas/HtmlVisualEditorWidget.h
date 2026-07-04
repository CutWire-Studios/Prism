#pragma once

#include <QWidget>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>

class QWebEngineView;

/// QWebChannel endpoint the visual editor page talks to.
class HtmlVisualBridge : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void jsReady() { emit ready(); }
    void htmlChanged(const QString &html) { emit htmlEdited(html); }
    void selectionChanged(const QString &infoJson) { emit selectionInfo(infoJson); }

signals:
    void ready();
    void htmlEdited(const QString &html);
    void selectionInfo(const QString &infoJson);
};

/// WYSIWYG editor for arbitrary overlay HTML: hosts the bundled visual editor
/// page, which renders the document live in a 1280×720 iframe and lets the
/// user select, move, resize, restyle, text-edit and delete elements directly
/// on the canvas. Edits are serialized back to HTML.
class HtmlVisualEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HtmlVisualEditorWidget(QWidget *parent = nullptr);

    /// Loads a document for editing; base resolves relative asset URLs.
    void loadHtml(const QString &html, const QUrl &base = {});

    QString html() const { return m_html; }
    bool isDirty() const { return m_dirty; }

    void applyStyle(const QString &property, const QString &value);
    void deleteSelectedElement();

signals:
    void htmlEdited();
    void selectionChanged(const QJsonObject &info); // empty object = no selection

private:
    void runOrQueue(const QString &js);

    QWebEngineView   *m_view   = nullptr;
    HtmlVisualBridge *m_bridge = nullptr;
    QString           m_html;
    bool              m_dirty   = false;
    bool              m_jsReady = false;
    QStringList       m_pendingJs;
};
