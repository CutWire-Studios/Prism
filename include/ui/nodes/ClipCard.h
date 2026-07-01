#pragma once

#include <QFrame>
#include <QLabel>
#include "core/project/OverlayItem.h"
#include "core/sources/SourceDescriptor.h"

namespace Ui { class ClipCard; }

/// A deck slot (or group-member tile) showing one clip or live source with its
/// thumbnail, label and active/selected state.
class ClipCard : public QFrame {
    Q_OBJECT
public:
    enum class CardMode { Deck, GroupMember, InputNode };

    explicit ClipCard(int index, QWidget *parent = nullptr);
    ~ClipCard();

    // Load a file-based clip (video or image).
    void loadClip(const QString &path, const QPixmap &thumbnail);

    // Load a live/non-file source (slideshow, camera, screen, color).
    void loadSource(const SourceDescriptor &desc, const QPixmap &thumbnail);

    void clearClip();
    void setActive(bool active);
    void setASelected(bool selected);
    void setBSelected(bool selected);
    bool isASelected() const { return m_aSelected; }
    bool isBSelected() const { return m_bSelected; }

    void setCardMode(CardMode mode);
    CardMode cardMode() const { return m_cardMode; }
    void setOutputSelected(bool selected);
    bool isOutputSelected() const { return m_outputSelected; }

    void setTransform(float x, float y, float w, float h);
    void transform(float &x, float &y, float &w, float &h) const;

    // Update this card's grid index (called after rebuildGrid re-flows cards).
    void setIndex(int idx) { m_index = idx; }

    QString clipPath()   const { return m_clipPath; }
    bool    hasSource()  const;
    bool    isLiveSource() const { return m_sourceDesc.isLiveSource(); }
    void setDisplayName(const QString &name);
    QString displayName() const { return m_sourceDesc.displayName; }
    const SourceDescriptor &sourceDescriptor() const { return m_sourceDesc; }
    void setObsSceneName(const QString &sceneName);

    // Hotkey badge — shows the assigned key letter over the thumbnail.
    void setHotkeyLabel(const QString &key);
    QString hotkeyLabel() const { return m_hotkeyBadge->text(); }

    bool isRepeat()  const { return m_repeat; }

    // Expose full settings (trim + crop + overlays) — valid for file sources only.
    const ClipSettings &settings() const { return m_settings; }
    double startTime() const { return m_settings.startTime; }
    double endTime()   const { return m_settings.endTime; }
    float  cropX()     const { return m_settings.cropX; }
    float  cropY()     const { return m_settings.cropY; }
    float  cropW()     const { return m_settings.cropW; }
    float  cropH()     const { return m_settings.cropH; }
    const QList<OverlayItem> &overlays() const { return m_settings.overlays; }

    // Session restore: programmatically apply settings and toggle states.
    void applySettings(const ClipSettings &s) { m_settings = s; }
    void setRepeat(bool r);

signals:
    void triggered(int index);
    void aButtonClicked(int index);
    void bButtonClicked(int index);
    void removeRequested(int index);
    void transformChanged(int index, float x, float y, float w, float h);
    void setOutputClicked(int index);
    // Emitted when the user changes a live source's settings via Edit.
    void sourceDescriptorChanged(int index, const SourceDescriptor &desc);
    void preferredHeightChanged(int height);

private slots:
    void onRepeatClicked();
    void onEditClicked();
    void onAButtonClicked();
    void onBButtonClicked();
    void onRemoveClicked();
    void onSetOutputClicked();
    void onTransformToggle(bool expanded);
    void onTransformSpinChanged();

private:
    void updateTransformToggleLabel();
    void emitTransformChanged();

    Ui::ClipCard     *ui;
    int               m_index;
    QString           m_clipPath;
    SourceDescriptor  m_sourceDesc;
    bool              m_repeat     = false;
    bool              m_aSelected  = false;
    bool              m_bSelected  = false;
    bool              m_outputSelected = false;
    CardMode          m_cardMode   = CardMode::Deck;
    float             m_transformX = 0.f;
    float             m_transformY = 0.f;
    float             m_transformW = 1.f;
    float             m_transformH = 1.f;
    bool              m_blockTransformSignal = false;
    ClipSettings      m_settings;
    QLabel           *m_hotkeyBadge = nullptr;
};
