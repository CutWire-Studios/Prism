#include "ui/canvas/HtmlVisualEditorWidget.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineView>

namespace {

// Encodes a QString as a JS string literal (JSON string escaping).
static QString jsString(const QString &s) {
    QJsonArray a;
    a.append(s);
    const QString out = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
    return out.mid(1, out.size() - 2);
}

} // namespace

HtmlVisualEditorWidget::HtmlVisualEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(480, 300);

    m_view = new QWebEngineView(this);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    m_bridge = new HtmlVisualBridge(this);
    auto *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("bridge"), m_bridge);
    m_view->page()->setWebChannel(channel);

    connect(m_bridge, &HtmlVisualBridge::ready, this, [this]() {
        m_jsReady = true;
        for (const QString &js : std::as_const(m_pendingJs))
            m_view->page()->runJavaScript(js);
        m_pendingJs.clear();
    });

    connect(m_bridge, &HtmlVisualBridge::htmlEdited, this, [this](const QString &html) {
        m_html = html;
        m_dirty = true;
        emit htmlEdited();
    });

    connect(m_bridge, &HtmlVisualBridge::selectionInfo, this, [this](const QString &infoJson) {
        const QJsonDocument doc = QJsonDocument::fromJson(infoJson.toUtf8());
        emit selectionChanged(doc.isObject() ? doc.object() : QJsonObject());
    });

    m_view->setUrl(QUrl(QStringLiteral("qrc:/html/editor/visual_editor.html")));
}

void HtmlVisualEditorWidget::runOrQueue(const QString &js) {
    if (m_jsReady)
        m_view->page()->runJavaScript(js);
    else
        m_pendingJs.append(js);
}

void HtmlVisualEditorWidget::loadHtml(const QString &html, const QUrl &base) {
    m_html = html;
    m_dirty = false;
    runOrQueue(QStringLiteral("editor.setHtml(%1, %2);")
                   .arg(jsString(html), jsString(base.toString())));
}

void HtmlVisualEditorWidget::applyStyle(const QString &property, const QString &value) {
    runOrQueue(QStringLiteral("editor.applyStyle(%1, %2);")
                   .arg(jsString(property), jsString(value)));
}

void HtmlVisualEditorWidget::deleteSelectedElement() {
    runOrQueue(QStringLiteral("editor.deleteSelected();"));
}
