#pragma once

#include <QFrame>
#include "core/OverlayItem.h"
#include "core/SourceDescriptor.h"

namespace Ui { class ClipCard; }

class ClipCard : public QFrame {
    Q_OBJECT
public:
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

    // Update this card's grid index (called after rebuildGrid re-flows cards).
    void setIndex(int idx) { m_index = idx; }

    QString clipPath()   const { return m_clipPath; }
    bool    hasSource()  const { return !m_sourceDesc.displayName.isEmpty(); }
    bool    isLiveSource() const { return m_sourceDesc.isLiveSource(); }
    QString sourceName() const { return m_sourceDesc.displayName; }
    const SourceDescriptor &sourceDescriptor() const { return m_sourceDesc; }

    bool isMuted()   const { return m_muted; }
    int  volume()    const;
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

signals:
    void triggered(int index);
    void aButtonClicked(int index);
    void bButtonClicked(int index);
    void removeRequested(int index);
    // Emitted when the user changes a live source's settings via Edit.
    void sourceDescriptorChanged(int index, const SourceDescriptor &desc);

private slots:
    void onMuteClicked();
    void onRepeatClicked();
    void onEditClicked();
    void onAButtonClicked();
    void onBButtonClicked();
    void onRemoveClicked();

private:
    Ui::ClipCard     *ui;
    int               m_index;
    QString           m_clipPath;
    SourceDescriptor  m_sourceDesc;
    bool              m_muted    = false;
    bool              m_repeat   = false;
    bool              m_aSelected = false;
    bool              m_bSelected = false;
    ClipSettings      m_settings;
};
