#include "app/editors/AudioGraphParams.h"

AudioGraphParams::AudioGraphParams(QObject *parent)
    : QObject(parent)
{
}

QVariantMap AudioGraphParams::params(quint64 nodeId) const
{
    const ClipGraph::Node *node = m_controller->graph()->node(nodeId);
    return node ? node->params : QVariantMap{};
}
