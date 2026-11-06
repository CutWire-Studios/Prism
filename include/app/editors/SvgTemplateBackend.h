#pragma once

#include "core/sources/SourceDescriptor.h"
#include "core/sources/SvgTemplates.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class ClipGraphController;

// Backend of the SVG template source editor: the built-in and opened custom templates, their
// parameter table (the Scale row first, then the template's own params), and the live preview.
class SvgTemplateBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList templates READ templates NOTIFY templatesChanged)
    Q_PROPERTY(QString templateId READ templateId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList params READ params NOTIFY paramsChanged)
    Q_PROPERTY(QImage preview READ preview NOTIFY previewChanged)

public:
    explicit SvgTemplateBackend(QObject *parent = nullptr);

    QVariantList templates() const { return m_templates; }
    QString templateId() const { return m_info.id; }
    QVariantList params() const { return m_params; }
    QImage preview() const { return m_preview; }

    Q_INVOKABLE void open(ClipGraphController *graph, quint64 nodeId);
    Q_INVOKABLE void selectTemplate(const QString &id);
    Q_INVOKABLE void addCustomFile(const QUrl &url);
    Q_INVOKABLE void setParamValue(const QString &name, const QString &value);
    Q_INVOKABLE void setParamColor(const QString &name, const QColor &color);
    Q_INVOKABLE bool commit();

signals:
    void templatesChanged();
    void selectionChanged();
    void paramsChanged();
    void previewChanged();

private:
    void addCustomItem(const QString &path);
    void rebuildParams();
    QVariantMap paramRow(const QString &name, const QString &label, const QString &type,
                         const QString &defaultValue) const;
    void updatePreview();
    SourceDescriptor currentDescriptor() const;

    ClipGraphController *m_graph = nullptr;
    quint64 m_nodeId = 0;
    prism::SvgTemplateInfo m_info;
    QJsonObject m_overrides;
    QVariantList m_templates;
    QVariantList m_params;
    QImage m_preview;
};
