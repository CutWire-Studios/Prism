#include "app/editors/MixerChannels.h"

MixerChannels::MixerChannels(QObject *parent)
    : QObject(parent)
{
}

QVariantList MixerChannels::channels(quint64 nodeId) const
{
    QVariantList out;
    const ClipGraph *graph = m_controller->graph();
    const ClipGraph::Node *node = graph->node(nodeId);
    if (!node)
        return out;

    const QVariantList slotList = node->params.value(QStringLiteral("slots")).toList();
    for (int i = 0; i < slotList.size(); ++i) {
        if (!graph->isPortConnected(nodeId, ClipGraph::PortDirection::In, i))
            continue;
        const QVariantMap slot = slotList.at(i).toMap();
        out.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("name"), slot.value(QStringLiteral("name"))},
            {QStringLiteral("volume"), slot.value(QStringLiteral("volume"), 100)},
            {QStringLiteral("muted"), slot.value(QStringLiteral("muted"), false)},
        });
    }
    return out;
}

bool MixerChannels::apply(quint64 nodeId, const QVariantList &rows)
{
    const ClipGraph::Node *node = m_controller->graph()->node(nodeId);
    if (!node)
        return false;

    QVariantList slotList = node->params.value(QStringLiteral("slots")).toList();
    for (const QVariant &rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        const int index = row.value(QStringLiteral("index")).toInt();
        QVariantMap slot = slotList.at(index).toMap();
        slot[QStringLiteral("volume")] = row.value(QStringLiteral("volume")).toInt();
        slot[QStringLiteral("muted")] = row.value(QStringLiteral("muted")).toBool();
        slot[QStringLiteral("name")] = row.value(QStringLiteral("name")).toString().trimmed();
        slotList[index] = slot;
    }
    return m_controller->setParams(nodeId, QVariantMap{{QStringLiteral("slots"), slotList}});
}
