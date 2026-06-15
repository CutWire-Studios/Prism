#pragma once

#include <QObject>
#include <QPixmap>
#include <QString>
#include "core/SourceDescriptor.h"
#include "core/OverlayItem.h"
#include "ui/ClipCard.h"

using NodeId = quint64;

// Lightweight model that wraps a ClipCard. The card itself is created externally
// (by ClipNodeItem) and passed in via setCard(); this class holds a borrowed pointer.
class ClipNodeModel : public QObject {
    Q_OBJECT

public:
    explicit ClipNodeModel(QObject *parent = nullptr);

    void setCard(ClipCard *card);

    void loadClip(const QString &path, const QPixmap &thumbnail) { m_card->loadClip(path, thumbnail); }
    void loadSource(const SourceDescriptor &desc, const QPixmap &thumbnail) { m_card->loadSource(desc, thumbnail); }
    void clearClip() { m_card->clearClip(); }

    // ── Clip properties (forwarded to the embedded ClipCard) ─────────────────
    QString clipPath() const { return m_card->clipPath(); }
    bool hasSource() const { return m_card->hasSource(); }
    bool isLiveSource() const { return m_card->isLiveSource(); }
    QString sourceName() const { return m_card->sourceName(); }
    const SourceDescriptor &sourceDescriptor() const { return m_card->sourceDescriptor(); }

    bool isMuted() const { return m_card->isMuted(); }
    int volume() const { return m_card->volume(); }
    bool isRepeat() const { return m_card->isRepeat(); }

    const ClipSettings &settings() const { return m_card->settings(); }
    double startTime() const { return m_card->startTime(); }
    double endTime() const { return m_card->endTime(); }
    float cropX() const { return m_card->cropX(); }
    float cropY() const { return m_card->cropY(); }
    float cropW() const { return m_card->cropW(); }
    float cropH() const { return m_card->cropH(); }
    const QList<OverlayItem> &overlays() const { return m_card->overlays(); }

    // ── A/B Deck buttons ─────────────────────────────────────────────────────
    void setASelected(bool selected) { m_card->setASelected(selected); }
    void setBSelected(bool selected) { m_card->setBSelected(selected); }
    bool isASelected() const { return m_card->isASelected(); }
    bool isBSelected() const { return m_card->isBSelected(); }

signals:
    void aButtonClicked();
    void bButtonClicked();
    void removeRequested();
    void sourceDescriptorChanged(const SourceDescriptor &desc);

private:
    ClipCard *m_card = nullptr;  // borrowed — owned by QGraphicsProxyWidget inside ClipNodeItem
};
