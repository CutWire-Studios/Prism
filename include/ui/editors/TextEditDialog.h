#pragma once

#include "core/scripting/ScriptOutput.h"
#include "core/sources/SourceDescriptor.h"
#include "core/text/TextStyle.h"
#include <QColor>
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QPointer>
#include <functional>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTabBar;
class QTabWidget;
class QTimer;
class QToolButton;

namespace style {
class ColorButton;
class ShadingLayerStackEditor;
class SliderSpinRow;
}

/// Editor for a Text source: template with {placeholder} tokens, a Type / Look / Animate
/// inspector over the Skia text model, and a live preview (held pose or In -> Hold -> Out
/// playback) rendered by the same code as the program output. When the text node's DataIn port
/// is wired to a Script node, the script's variables appear as draggable blocks with live values.
class TextEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit TextEditDialog(const SourceDescriptor &initial = SourceDescriptor(),
                            QWidget *parent = nullptr);
    ~TextEditDialog() override;

    /// Attach the Script node wired to this text node's DataIn port (if any).
    void setScriptBinding(const ScriptBinding &binding);

    SourceDescriptor resultDescriptor() const;

    /// The style being edited (tests).
    const prism::TextStyle &style() const { return m_style; }
    void applyStylePack(const QString &id);
    void applyAnimationPreset(int slot, const QString &presetId);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updatePreview();
    void onPollScriptOutput();
    void tryAccept();

private:
    class Highlighter;
    class ParamForm;

    struct ThumbJob {
        QPointer<QListWidget> list;
        QString id;
        QString key;
        std::function<QPixmap()> render;
    };

    void buildTypeTab(QWidget *page);
    void buildLookTab(QWidget *page);
    void buildAnimateTab(QWidget *page);

    void setFromDescriptor(const SourceDescriptor &desc);
    SourceDescriptor currentDescriptor() const;
    QString resolvedText() const;
    void syncControlsFromStyle();
    void syncLookParams();
    void syncAnimTab();
    void syncPackSelection();

    void styleEdited();
    void layersEdited();
    void scheduleRender();
    void renderHeld();
    void renderPlayFrame();
    void startPlay();
    void stopPlay();
    void refreshDurations();

    void rebuildPackLists();
    void rebuildAnimList();
    void fillThumbs(QListWidget *list, const std::function<QString(const QString &)> &keyFor,
                    const std::function<QPixmap(const QString &)> &render);
    void runThumbBatch();
    void saveStylePreset();

    void rebuildVariableChips();
    void insertToken(const QString &name);
    QStringList knownVariableNames() const;

    prism::TextAnimationSlot &slotRef(int slot);

    prism::TextStyle m_style;
    int m_canvasW = 1280;
    int m_canvasH = 720;
    bool m_loading = false;

    QPlainTextEdit *m_templateEdit = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_playBtn = nullptr;
    QToolButton *m_runScriptBtn = nullptr;
    QLabel *m_varsHint = nullptr;
    QScrollArea *m_chipsScroll = nullptr;
    QWidget *m_chipsHost = nullptr;
    QTabWidget *m_tabs = nullptr;
    Highlighter *m_highlighter = nullptr;

    QFontComboBox *m_font = nullptr;
    QComboBox *m_weight = nullptr, *m_align = nullptr, *m_valign = nullptr, *m_accentRule = nullptr;
    QCheckBox *m_italic = nullptr, *m_wrap = nullptr;
    QGroupBox *m_boxGroup = nullptr, *m_hlGroup = nullptr, *m_underlineGroup = nullptr,
              *m_accentColorGroup = nullptr, *m_accentOutlineGroup = nullptr, *m_accentPillGroup = nullptr;
    QWidget *m_accentBody = nullptr;
    style::SliderSpinRow *m_size = nullptr, *m_lineHeight = nullptr, *m_letterSpacing = nullptr, *m_bend = nullptr,
                         *m_boxPadding = nullptr, *m_boxRadius = nullptr, *m_hlPadding = nullptr,
                         *m_hlRadius = nullptr, *m_ulWidth = nullptr, *m_ulOffset = nullptr,
                         *m_accentN = nullptr, *m_accentSize = nullptr, *m_accentOutlineWidth = nullptr;
    style::ColorButton *m_boxColor = nullptr, *m_hlColor = nullptr, *m_ulColor = nullptr,
                       *m_accentColor = nullptr, *m_accentOutlineColor = nullptr, *m_accentPillColor = nullptr;

    QListWidget *m_userPacks = nullptr, *m_builtinPacks = nullptr, *m_lookList = nullptr;
    QLabel *m_myStylesHeader = nullptr;
    ParamForm *m_lookParams = nullptr;
    style::ShadingLayerStackEditor *m_layers = nullptr;

    QTabBar *m_slotBar = nullptr;
    QComboBox *m_animCategory = nullptr;
    QListWidget *m_animList = nullptr;
    QLabel *m_animNote = nullptr;
    QPushButton *m_animRevert = nullptr;
    ParamForm *m_animParams = nullptr;

    QHash<QString, QPixmap> m_thumbCache;
    QList<ThumbJob> m_thumbJobs;
    QTimer *m_thumbTimer = nullptr;

    QTimer *m_renderTimer = nullptr;
    QTimer *m_playTimer = nullptr;
    QElapsedTimer m_playClock;
    bool m_playing = false;
    bool m_durDirty = true;
    double m_inSec = 0.0;
    double m_outSec = 0.0;

    ScriptBinding m_binding;
    QJsonObject m_vars;              // latest values published by the script
    QStringList m_declaredVars;      // keys parsed from the Lua source
    QStringList m_chipKeys;          // keys the current chips were built for
    QList<QWidget *> m_chipWidgets;
    bool m_chipStretchAdded = false;
    uint m_lastSeenVersion = 0;
    QTimer *m_pollTimer = nullptr;
};
