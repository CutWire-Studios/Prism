#pragma once

#include "core/audio/AudioRack.h"

#include "core/audio/dsp/PedalCatalog.h"

#include <QDialog>
#include <QJsonObject>
#include <functional>

class QBoxLayout;
class QLabel;
class QMenu;
class QScrollArea;
class QToolButton;
class QTreeWidgetItem;
class QVBoxLayout;

class PedalboardDialog : public QDialog {
    Q_OBJECT
public:
    PedalboardDialog(const QJsonObject &params, std::function<void(const QJsonObject &)> onLiveChange,
                     QWidget *parent = nullptr);

    QJsonObject params() const { return prism::audiofx::toJson(m_rack); }

private:
    struct KnobArgs;

    void edited();
    void structural();
    void scheduleRebuild();
    void rebuild();
    bool applyOp(bool ok);
    void showStatus(const QString &text);
    void selectLane(const QString &split, int lane);

    void addFromPalette(QTreeWidgetItem *item);
    prism::audiofx::RackSlot targetSlot() const;

    QWidget *buildChain(const std::vector<prism::audiofx::RackItem> &items, const QString &split, int lane, bool top);
    QWidget *buildPedalCard(const prism::audiofx::RackPedal &pedal, const QString &split, int lane, int index,
                            int count);
    QWidget *buildSplitCard(const prism::audiofx::RackSplit &split, const QString &parent, int lane, int index,
                            int count);
    QWidget *buildModulators();
    QWidget *buildTargets(const QString &modId);
    QWidget *knobPanel(const std::vector<KnobArgs> &knobs);
    QWidget *makeKnob(const KnobArgs &args, bool compact = false);
    static KnobArgs specArgs(const prism::audiofx::KnobSpec &spec, double value);
    QToolButton *makeMenuButton(const std::function<void(QMenu *)> &fill);
    QToolButton *makeItemMenu(const QString &id, const QString &split, int lane, int index, int count,
                              const std::function<void(QMenu *)> &extra = {});
    void routeMenu(const QPoint &globalPos, const QString &pedalId, const QString &knobId);

    prism::audiofx::AudioRack m_rack;
    std::function<void(const QJsonObject &)> m_onLive;
    QVBoxLayout *m_right = nullptr;
    QScrollArea *m_boardScroll = nullptr;
    QScrollArea *m_modScroll = nullptr;
    QLabel *m_modHint = nullptr;
    QLabel *m_status = nullptr;
    QString m_selSplit;
    int m_selLane = 0;
    bool m_rebuildPending = false;
};
