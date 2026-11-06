#include "app/editors/SvgTemplateBackend.h"

#include "core/graph/ClipGraphModels.h"
#include "core/sources/SvgTemplateSource.h"

#include <QFileInfo>

#include <utility>

using namespace prism;

namespace {

const QString kScaleName = QStringLiteral("scale");
const QSize kThumbSize(176, 100);

QImage thumbFor(const SourceDescriptor &desc)
{
    return SvgTemplateSource::renderDescriptor(desc).scaled(kThumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

SvgTemplateBackend::SvgTemplateBackend(QObject *parent)
    : QObject(parent)
{
    for (const SvgTemplateInfo &info : builtinSvgTemplates()) {
        SourceDescriptor d;
        d.kind = SourceDescriptor::Kind::SvgTemplate;
        d.svgTemplateId = info.id;
        m_templates.append(QVariantMap{{QStringLiteral("id"), info.id},
                                       {QStringLiteral("label"), info.name},
                                       {QStringLiteral("image"), thumbFor(d)}});
    }
}

void SvgTemplateBackend::open(ClipGraphController *graph, quint64 nodeId)
{
    if (!graph || !graph->graph())
        return;
    m_graph = graph;
    m_nodeId = nodeId;

    const SourceDescriptor initial = graph->graph()->sourceDescriptor(nodeId);
    if (initial.kind == SourceDescriptor::Kind::SvgTemplate && !initial.svgTemplateId.isEmpty()) {
        m_overrides = SvgTemplateSource::paramsFromDescriptor(initial);
        bool found = false;
        for (const QVariant &v : std::as_const(m_templates)) {
            if (v.toMap().value(QStringLiteral("id")).toString() == initial.svgTemplateId)
                found = true;
        }
        if (found)
            selectTemplate(initial.svgTemplateId);
        else
            addCustomItem(initial.svgTemplateId);
    } else if (!m_templates.isEmpty()) {
        selectTemplate(m_templates.first().toMap().value(QStringLiteral("id")).toString());
    }
}

void SvgTemplateBackend::selectTemplate(const QString &id)
{
    const QByteArray svg = svgTemplateBytes(id);
    if (!m_info.id.isEmpty() && m_info.id != id)
        m_overrides = {};
    m_info = svgTemplateInfo(id, svg);
    if (m_info.name.isEmpty())
        m_info.name = QFileInfo(id).completeBaseName();
    emit selectionChanged();
    rebuildParams();
    updatePreview();
}

void SvgTemplateBackend::addCustomFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!path.isEmpty())
        addCustomItem(path);
}

void SvgTemplateBackend::addCustomItem(const QString &path)
{
    if (svgTemplateBytes(path).isEmpty())
        return;
    SourceDescriptor d;
    d.kind = SourceDescriptor::Kind::SvgTemplate;
    d.svgTemplateId = path;
    m_templates.append(QVariantMap{{QStringLiteral("id"), path},
                                   {QStringLiteral("label"), QFileInfo(path).fileName()},
                                   {QStringLiteral("image"), thumbFor(d)}});
    emit templatesChanged();
    selectTemplate(path);
}

void SvgTemplateBackend::rebuildParams()
{
    QVariantList rows;
    rows.append(paramRow(kScaleName, tr("Scale"), QStringLiteral("number"), QStringLiteral("1")));
    for (const SvgTemplateParam &p : std::as_const(m_info.params))
        rows.append(paramRow(p.name, p.label, p.type, p.defaultValue));
    m_params = rows;
    emit paramsChanged();
}

QVariantMap SvgTemplateBackend::paramRow(const QString &name, const QString &label, const QString &type,
                                        const QString &defaultValue) const
{
    const QJsonValue ov = m_overrides.value(name);
    const QString value = ov.isUndefined() ? defaultValue
                        : (ov.isString() ? ov.toString() : QString::number(ov.toDouble()));
    QVariantMap row{{QStringLiteral("name"), name},
                    {QStringLiteral("label"), label},
                    {QStringLiteral("type"), type},
                    {QStringLiteral("value"), value}};
    if (type == QLatin1String("color")) {
        const QColor c(value);
        row.insert(QStringLiteral("swatch"), c.isValid() ? c : QColor(Qt::transparent));
        row.insert(QStringLiteral("swatchValid"), c.isValid());
        row.insert(QStringLiteral("display"), !c.isValid() ? (value.isEmpty() ? tr("Pick…") : value)
                                                           : c.name(QColor::HexArgb).toUpper());
    }
    return row;
}

void SvgTemplateBackend::setParamValue(const QString &name, const QString &value)
{
    QString def;
    if (name == kScaleName) {
        def = QStringLiteral("1");
    } else {
        for (const SvgTemplateParam &p : std::as_const(m_info.params)) {
            if (p.name == name)
                def = p.defaultValue;
        }
    }
    if (value == def)
        m_overrides.remove(name);
    else
        m_overrides.insert(name, value);
    updatePreview();
}

void SvgTemplateBackend::setParamColor(const QString &name, const QColor &color)
{
    if (!color.isValid())
        return;
    setParamValue(name, color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb));
    rebuildParams();
}

bool SvgTemplateBackend::commit()
{
    if (!m_graph || !m_graph->graph() || m_info.id.isEmpty())
        return false;
    m_graph->graph()->setSourceDescriptor(m_nodeId, currentDescriptor());
    return true;
}

SourceDescriptor SvgTemplateBackend::currentDescriptor() const
{
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::SvgTemplate;
    desc.svgTemplateId = m_info.id;
    desc.svgParamsJson = SvgTemplateSource::paramsToJson(m_overrides);
    desc.displayName = m_info.name;
    return desc;
}

void SvgTemplateBackend::updatePreview()
{
    if (m_info.id.isEmpty())
        return;
    const QImage frame = SvgTemplateSource::renderDescriptor(currentDescriptor());
    if (frame.isNull())
        return;
    m_preview = frame;
    emit previewChanged();
}
