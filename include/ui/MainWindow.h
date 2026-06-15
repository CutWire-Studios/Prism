#pragma once

#include <QMainWindow>
#include <QVector>
#include "ui/ClipCard.h"
#include "ui/OutputWindow.h"
#include "core/ClipManager.h"
#include "core/SourceDescriptor.h"
#include "core/MediaSource.h"

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onClipGridClicked(int index);
    void onLoadFolderClicked();
    void onAddFolderClicked();
    void onAddFilesClicked();
    void onClearAllClicked();
    void onCrossfaderMoved(int value);
    void onADeckPlayClicked();
    void onBDeckPlayClicked();
    void onADeckSpeedChanged(int value);
    void onBDeckSpeedChanged(int value);
    void onTimerUpdate();
    void onAButtonClicked(int index);
    void onBButtonClicked(int index);

    // ── Add Element handlers ──────────────────────────────────────────────────
    void onAddElementSlideshow();
    void onAddElementCamera();
    void onAddElementScreen();
    void onAddElementWindow();
    void onAddElementColor();

    // ── Card management ───────────────────────────────────────────────────────
    void onCardRemoveRequested(int index);
    void onCardSourceDescriptorChanged(int index, const SourceDescriptor &desc);

private:
    static constexpr int CARD_WIDTH = 122;
    static constexpr int MIN_COLS   = 2;
    int dynamicCols = 8;

    Ui::MainWindow  *ui;
    OutputWindow    *outputWindow       = nullptr;
    QWidget         *m_emptyPlaceholder = nullptr;

    QVector<ClipCard *> m_clipCards;   // file clips — rebuilt by rebuildGrid()
    QVector<ClipCard *> m_liveCards;   // element cards — survive rebuildGrid()

    bool m_aSliderDragging = false;
    bool m_bSliderDragging = false;

    ClipManager clipManager;
    QTimer     *updateTimer       = nullptr;
    int         selectedClipIndex = -1;
    int         aClipIndex        = -1;
    int         bClipIndex        = -1;

    // Return the card at a unified index (file cards first, then live cards).
    ClipCard *cardAtIndex(int index) const;

    // Add an element card to m_liveCards and insert it into the grid.
    void addElementCard(const SourceDescriptor &desc, const QPixmap &thumb);

    // Assign a ready-made source to the active deck (based on crossfader).
    void assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                  const QString &name);

    // Update deck UI state after a source is assigned.
    void updateDeckUI(bool deckA, const QString &name, bool hasTimeline);

    void setupConnections();
    void applyTheme();
    void rebuildGrid();
    void updateGridLayout();

    static QPixmap makeIconThumb(const QString &glyph, int w = 110, int h = 65);
    static QPixmap makeColorThumb(const QColor &color, int w = 110, int h = 65);
    static QString formatTimeShort(double secs);
};
