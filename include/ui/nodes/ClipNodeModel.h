#pragma once

#include <QObject>
#include <QPixmap>
#include <QString>
#include "core/sources/SourceDescriptor.h"
#include "core/project/OverlayItem.h"
#include "ui/nodes/ClipCard.h"

using NodeId = quint64;

// Lightweight model that wraps a ClipCard. The card itself is created externally
// (by ClipNodeItem) and passed in via setCard(); this class holds a borrowed pointer.
class ClipNodeModel : public QObject {
    Q_OBJECT

public:
    explicit ClipNodeModel(QObject *parent = nullptr);

    void setCard(ClipCard *card);

    // Auto-assigned slot identifier (set by ClipNodeEditor on creation).
    void    setNodeId(NodeId id) { m_nodeId = id; }
    NodeId  nodeId()       const { return m_nodeId; }

    // Show / clear the hotkey badge on the embedded card.
    void setHotkeyLabel(const QString &key);

    void loadClip(const QString &path, const QPixmap &thumbnail) {
        m_thumbnail = thumbnail;
        m_card->loadClip(path, thumbnail);
    }
    void loadSource(const SourceDescriptor &desc, const QPixmap &thumbnail) {
        m_thumbnail = thumbnail;
        m_card->loadSource(desc, thumbnail);
    }
    void clearClip() { m_card->clearClip(); }
    QPixmap thumbnail() const { return m_thumbnail; }

    // ── Clip properties (forwarded to the embedded ClipCard) ─────────────────
    QString clipPath() const { return m_card->clipPath(); }
    bool hasSource() const { return m_card->hasSource(); }
    bool isLiveSource() const { return m_card->isLiveSource(); }
    QString sourceName() const { return m_card->displayName(); }
    void setDisplayName(const QString &name) {
        if (m_card) {
            m_card->setDisplayName(name);
        }
    }
    const SourceDescriptor &sourceDescriptor() const { return m_card->sourceDescriptor(); }
    void setObsSceneName(const QString &sceneName) {
        if (m_card) {
            m_card->setObsSceneName(sceneName);
        }
    }

    bool isRepeat() const { return m_card->isRepeat(); }
    void applySettings(const ClipSettings &s) { m_card->applySettings(s); }
    void setRepeat(bool r) { m_card->setRepeat(r); }

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

    void setCardMode(ClipCard::CardMode mode) { m_card->setCardMode(mode); }
    bool isGroupMember() const {
        return m_card && m_card->cardMode() == ClipCard::CardMode::GroupMember;
    }
    void setOutputSelected(bool selected) { m_card->setOutputSelected(selected); }

    void setTransform(float x, float y, float w, float h) { m_card->setTransform(x, y, w, h); }
    void transform(float &x, float &y, float &w, float &h) const { m_card->transform(x, y, w, h); }

signals:
    void aButtonClicked();
    void bButtonClicked();
    void removeRequested();
    void transformChanged(float x, float y, float w, float h);
    void setOutputClicked();
    void sourceDescriptorChanged(const SourceDescriptor &desc);

private:
    ClipCard *m_card   = nullptr;  // borrowed — owned by QGraphicsProxyWidget inside ClipNodeItem
    NodeId    m_nodeId = 0;
    QPixmap   m_thumbnail;
};
