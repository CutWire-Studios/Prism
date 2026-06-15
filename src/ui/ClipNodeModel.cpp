#include "ui/ClipNodeModel.h"

ClipNodeModel::ClipNodeModel(QObject *parent)
    : QObject(parent) {
}

void ClipNodeModel::setCard(ClipCard *card) {
    m_card = card;

    // Re-emit the card's signals as model signals. The card index is irrelevant
    // in the node editor — identity is tracked via NodeId.
    connect(m_card, &ClipCard::aButtonClicked,    this, [this](int) { emit aButtonClicked(); });
    connect(m_card, &ClipCard::bButtonClicked,    this, [this](int) { emit bButtonClicked(); });
    connect(m_card, &ClipCard::removeRequested,   this, [this](int) { emit removeRequested(); });
    connect(m_card, &ClipCard::sourceDescriptorChanged, this,
            [this](int, const SourceDescriptor &desc) { emit sourceDescriptorChanged(desc); });
}
