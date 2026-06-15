#include "ui/MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/VideoWidget.h"
#include "core/ThumbnailExtractor.h"
#include "core/SlideshowSource.h"
#include "core/CameraSource.h"
#include "core/ScreenSource.h"
#include "core/WindowCaptureSource.h"
#include "core/ColorSource.h"
#include "core/ImageSource.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <glob.h>
#include <QFileDialog>
#include <QTimer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    setAcceptDrops(true);

    outputWindow = new OutputWindow(this);
    outputWindow->show();

    setupConnections();
    applyTheme();

    // Kick the Qt Multimedia GStreamer backend into life early so that
    // QMediaDevices::videoInputs() is populated by the time the user
    // clicks "Add Camera".  The singleShot lets the event loop spin once
    // first so the window is already shown.
    QTimer::singleShot(0, []() {
        [[maybe_unused]] auto _ = QMediaDevices::videoInputs();
    });

    // Build the empty-state placeholder (shown before any media is loaded)
    m_emptyPlaceholder = new QWidget(ui->gridWidget);
    m_emptyPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *phLayout = new QVBoxLayout(m_emptyPlaceholder);
    phLayout->setAlignment(Qt::AlignCenter);
    phLayout->setSpacing(16);

    auto *phIcon = new QLabel("📁", m_emptyPlaceholder);
    phIcon->setAlignment(Qt::AlignCenter);
    phIcon->setStyleSheet("font-size: 52px;");

    auto *phText = new QLabel("No media loaded\nLoad a folder, add another folder, or pick individual files.",
                              m_emptyPlaceholder);
    phText->setAlignment(Qt::AlignCenter);
    phText->setWordWrap(true);
    phText->setStyleSheet("color: #555; font-size: 13px;");

    // Quick-action buttons embedded in the placeholder
    auto *phBtnRow = new QWidget(m_emptyPlaceholder);
    auto *phBtns   = new QHBoxLayout(phBtnRow);
    phBtns->setSpacing(10);
    auto *phLoad  = new QPushButton("📁  Load Folder",  phBtnRow);
    auto *phAdd   = new QPushButton("📂  Add Folder",   phBtnRow);
    auto *phFiles = new QPushButton("＋  Add Files",    phBtnRow);
    for (auto *b : {phLoad, phAdd, phFiles}) {
        b->setStyleSheet("font-size: 12px; padding: 6px 16px;");
        phBtns->addWidget(b);
    }
    connect(phLoad,  &QPushButton::clicked, this, &MainWindow::onLoadFolderClicked);
    connect(phAdd,   &QPushButton::clicked, this, &MainWindow::onAddFolderClicked);
    connect(phFiles, &QPushButton::clicked, this, &MainWindow::onAddFilesClicked);

    phLayout->addWidget(phIcon);
    phLayout->addWidget(phText);
    phLayout->addWidget(phBtnRow);

    ui->gridLayout->addWidget(m_emptyPlaceholder, 0, 0, 1, 1);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::onTimerUpdate);
    updateTimer->start(100);

    qDebug() << "SwitchX initialized - Live Media Control Mode";
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::setupConnections() {
    connect(ui->loadFolderBtn,    &QPushButton::clicked,  this, &MainWindow::onLoadFolderClicked);
    connect(ui->addFolderBtn,     &QPushButton::clicked,  this, &MainWindow::onAddFolderClicked);
    connect(ui->addFilesBtn,      &QPushButton::clicked,  this, &MainWindow::onAddFilesClicked);
    connect(ui->clearAllBtn,      &QPushButton::clicked,  this, &MainWindow::onClearAllClicked);

    // ── Add Element dropdown ──────────────────────────────────────────────────
    auto *elemMenu = new QMenu(this);
    elemMenu->addAction("🎬  Video File…",  this, &MainWindow::onAddFilesClicked);
    elemMenu->addAction("🖼  Photo…",       this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Add Photos", "",
            "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)");
        if (files.isEmpty()) return;
        clipManager.addFiles(files);
        rebuildGrid();
    });
    elemMenu->addSeparator();
    elemMenu->addAction("📁  Slideshow…",   this, &MainWindow::onAddElementSlideshow);
    elemMenu->addSeparator();
    elemMenu->addAction("📷  Camera…",          this, &MainWindow::onAddElementCamera);
    elemMenu->addAction("🖥  Screen Capture…",  this, &MainWindow::onAddElementScreen);
    elemMenu->addAction("🪟  Window / Tab…",    this, &MainWindow::onAddElementWindow);
    elemMenu->addSeparator();
    elemMenu->addAction("⬛  Solid Color…",     this, &MainWindow::onAddElementColor);

    ui->addElementBtn->setMenu(elemMenu);
    connect(ui->aDeckPlayBtn,     &QPushButton::clicked,  this, &MainWindow::onADeckPlayClicked);
    connect(ui->bDeckPlayBtn,     &QPushButton::clicked,  this, &MainWindow::onBDeckPlayClicked);
    connect(ui->aDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onADeckSpeedChanged);
    connect(ui->bDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onBDeckSpeedChanged);
    connect(ui->crossfaderSlider, &QSlider::valueChanged, this, &MainWindow::onCrossfaderMoved);

    connect(ui->aProgressSlider, &QSlider::sliderPressed, this, [this]() { m_aSliderDragging = true; });
    connect(ui->aProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_aSliderDragging = false;
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0) {
            double t = ui->aProgressSlider->value() / 1000.0 * dur;
            out->seekA(t);
        }
    });
    connect(ui->aProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0)
            ui->aTimeLabel->setText(formatTimeShort(value / 1000.0 * dur) + " / " + formatTimeShort(dur));
    });

    connect(ui->bProgressSlider, &QSlider::sliderPressed, this, [this]() { m_bSliderDragging = true; });
    connect(ui->bProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_bSliderDragging = false;
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0) {
            double t = ui->bProgressSlider->value() / 1000.0 * dur;
            out->seekB(t);
        }
    });
    connect(ui->bProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0)
            ui->bTimeLabel->setText(formatTimeShort(value / 1000.0 * dur) + " / " + formatTimeShort(dur));
    });
}

void MainWindow::onClipGridClicked(int index) {
    if (index < 0 || index >= m_clipCards.size()) return;
    ClipCard *card = m_clipCards[index];
    if (!card || card->clipPath().isEmpty()) return;

    for (ClipCard *c : m_clipCards) c->setActive(false);
    selectedClipIndex = index;
    card->setActive(true);

    if (ui->crossfaderSlider->value() <= 50)
        onAButtonClicked(index);
    else
        onBButtonClicked(index);
}

void MainWindow::onLoadFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Select Media Folder");
    if (path.isEmpty()) return;
    clipManager.loadFolder(path);
    selectedClipIndex = aClipIndex = bClipIndex = -1;
    rebuildGrid();
}

void MainWindow::onAddFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Add Media Folder");
    if (path.isEmpty()) return;
    clipManager.addFolder(path);
    rebuildGrid();
}

void MainWindow::onAddFilesClicked() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Media Files", "",
        "Media Files (*.mp4 *.avi *.mov *.mkv *.webm *.png *.jpg *.jpeg)");
    if (files.isEmpty()) return;
    clipManager.addFiles(files);
    rebuildGrid();
}

void MainWindow::onClearAllClicked() {
    clipManager.clear();
    selectedClipIndex = aClipIndex = bClipIndex = -1;
    rebuildGrid();
}

// ── Grid management ───────────────────────────────────────────────────────────

void MainWindow::rebuildGrid() {
    // Clear layout without destroying live-source cards.
    while (QLayoutItem *item = ui->gridLayout->takeAt(0)) {
        if (item->widget()) item->widget()->hide();
        delete item;
    }

    // Delete file-based cards only; live cards stay alive in m_liveCards.
    for (ClipCard *c : m_clipCards) c->deleteLater();
    m_clipCards.clear();

    int n = clipManager.getClipCount();
    bool hasAnything = (n > 0) || !m_liveCards.isEmpty();

    if (!hasAnything) {
        m_emptyPlaceholder->show();
        ui->gridLayout->addWidget(m_emptyPlaceholder, 0, 0, 1, 1);
        return;
    }

    m_emptyPlaceholder->hide();

    int availW = ui->clipsScrollArea->viewport()->width();
    if (availW < 10) availW = width() - 40;
    dynamicCols = std::max(MIN_COLS, availW / (CARD_WIDTH + 4));

    // Rebuild file-based cards.
    for (int i = 0; i < n; ++i) {
        auto *card = new ClipCard(i, ui->gridWidget);
        connect(card, &ClipCard::triggered,               this, &MainWindow::onClipGridClicked);
        connect(card, &ClipCard::aButtonClicked,           this, &MainWindow::onAButtonClicked);
        connect(card, &ClipCard::bButtonClicked,           this, &MainWindow::onBButtonClicked);
        connect(card, &ClipCard::removeRequested,          this, &MainWindow::onCardRemoveRequested);
        connect(card, &ClipCard::sourceDescriptorChanged,  this, &MainWindow::onCardSourceDescriptorChanged);

        QString path  = clipManager.getClipPath(i);
        QPixmap thumb = ThumbnailExtractor::extract(path, 110, 65);
        card->loadClip(path, thumb);

        ui->gridLayout->addWidget(card, i / dynamicCols, i % dynamicCols);
        card->show();
        m_clipCards.append(card);
    }

    // Re-append live-source cards after the file cards.
    for (int j = 0; j < m_liveCards.size(); ++j) {
        ClipCard *card = m_liveCards[j];
        int idx = n + j;
        card->setIndex(idx);
        card->setParent(ui->gridWidget);
        ui->gridLayout->addWidget(card, idx / dynamicCols, idx % dynamicCols);
        card->show();
    }

    qDebug() << "Grid rebuilt with" << n << "clips +" << m_liveCards.size() << "live";
}

// ── Helpers ───────────────────────────────────────────────────────────────────

ClipCard *MainWindow::cardAtIndex(int index) const {
    if (index < m_clipCards.size()) return m_clipCards.value(index, nullptr);
    index -= m_clipCards.size();
    return m_liveCards.value(index, nullptr);
}

void MainWindow::addElementCard(const SourceDescriptor &desc, const QPixmap &thumb) {
    int idx = m_clipCards.size() + m_liveCards.size();
    auto *card = new ClipCard(idx, ui->gridWidget);
    connect(card, &ClipCard::triggered,               this, &MainWindow::onClipGridClicked);
    connect(card, &ClipCard::aButtonClicked,           this, &MainWindow::onAButtonClicked);
    connect(card, &ClipCard::bButtonClicked,           this, &MainWindow::onBButtonClicked);
    connect(card, &ClipCard::removeRequested,          this, &MainWindow::onCardRemoveRequested);
    connect(card, &ClipCard::sourceDescriptorChanged,  this, &MainWindow::onCardSourceDescriptorChanged);
    card->loadSource(desc, thumb);
    m_liveCards.append(card);

    m_emptyPlaceholder->hide();

    int availW = ui->clipsScrollArea->viewport()->width();
    if (availW < 10) availW = width() - 40;
    dynamicCols = std::max(MIN_COLS, availW / (CARD_WIDTH + 4));

    ui->gridLayout->addWidget(card, idx / dynamicCols, idx % dynamicCols);
    card->show();
}

void MainWindow::assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                          const QString &name) {
    const bool toA = (ui->crossfaderSlider->value() <= 50);
    VideoWidget *out = outputWindow->videoWidget();
    if (toA) {
        out->setSourceA(std::move(src));
        out->playA();
    } else {
        out->setSourceB(std::move(src));
        out->playB();
    }
    updateDeckUI(toA, name, false);
}

void MainWindow::updateDeckUI(bool deckA, const QString &name, bool hasTimeline) {
    if (deckA) {
        ui->aSelectedLabel->setText(QString("A: %1").arg(name));
        ui->aProgressSlider->setEnabled(hasTimeline);
        ui->aDeckPlayBtn->setEnabled(true);
        if (!hasTimeline) { ui->aProgressSlider->setValue(0); ui->aTimeLabel->setText("—"); }
    } else {
        ui->bSelectedLabel->setText(QString("B: %1").arg(name));
        ui->bProgressSlider->setEnabled(hasTimeline);
        ui->bDeckPlayBtn->setEnabled(true);
        if (!hasTimeline) { ui->bProgressSlider->setValue(0); ui->bTimeLabel->setText("—"); }
    }
}

QPixmap MainWindow::makeIconThumb(const QString &glyph, int w, int h) {
    QPixmap pix(w, h);
    pix.fill(QColor("#1c1d1f"));
    QPainter p(&pix);
    QFont f;
    f.setPixelSize(32);
    p.setFont(f);
    p.setPen(QColor("#888888"));
    p.drawText(pix.rect(), Qt::AlignCenter, glyph);
    return pix;
}

QPixmap MainWindow::makeColorThumb(const QColor &color, int w, int h) {
    QPixmap pix(w, h);
    pix.fill(color);
    return pix;
}

// ── Crossfader ────────────────────────────────────────────────────────────────

void MainWindow::onCrossfaderMoved(int value) {
    outputWindow->videoWidget()->setCrossfade(value / 100.f);
}

void MainWindow::onADeckPlayClicked() {
    VideoWidget *output = outputWindow->videoWidget();
    if (output->isPlayingA())
        output->pauseA();
    else
        output->playA();
}

void MainWindow::onBDeckPlayClicked() {
    VideoWidget *output = outputWindow->videoWidget();
    if (output->isPlayingB())
        output->pauseB();
    else
        output->playB();
}

void MainWindow::onADeckSpeedChanged(int value) {
    qDebug() << "A Deck Speed:" << value << "%";
}

void MainWindow::onBDeckSpeedChanged(int value) {
    qDebug() << "B Deck Speed:" << value << "%";
}

void MainWindow::onTimerUpdate() {
    VideoWidget *out = outputWindow->videoWidget();

    double durA = out->getDurationA();
    double timeA = out->getCurrentTimeA();
    if (durA > 0) {
        if (!m_aSliderDragging) {
            ui->aProgressSlider->blockSignals(true);
            ui->aProgressSlider->setValue((int)(timeA / durA * 1000));
            ui->aProgressSlider->blockSignals(false);
        }
        ui->aTimeLabel->setText(formatTimeShort(timeA) + " / " + formatTimeShort(durA));
    }
    ui->aDeckPlayBtn->setText(out->isPlayingA() ? "⏸" : "▶");

    QImage frameA = out->getFrameA();
    if (!frameA.isNull())
        ui->aPreviewLabel->setPixmap(QPixmap::fromImage(
            frameA.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));

    double durB = out->getDurationB();
    double timeB = out->getCurrentTimeB();
    if (durB > 0) {
        if (!m_bSliderDragging) {
            ui->bProgressSlider->blockSignals(true);
            ui->bProgressSlider->setValue((int)(timeB / durB * 1000));
            ui->bProgressSlider->blockSignals(false);
        }
        ui->bTimeLabel->setText(formatTimeShort(timeB) + " / " + formatTimeShort(durB));
    }
    ui->bDeckPlayBtn->setText(out->isPlayingB() ? "⏸" : "▶");

    QImage frameB = out->getFrameB();
    if (!frameB.isNull())
        ui->bPreviewLabel->setPixmap(QPixmap::fromImage(
            frameB.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));
}

void MainWindow::updateGridLayout() {
    if (m_clipCards.isEmpty()) return;

    while (QLayoutItem *item = ui->gridLayout->takeAt(0)) {
        if (item->widget()) item->widget()->hide();
        delete item;
    }

    int availW = ui->clipsScrollArea->viewport()->width();
    if (availW < 10) availW = width() - 40;
    dynamicCols = std::max(MIN_COLS, availW / (CARD_WIDTH + 4));

    for (int i = 0; i < m_clipCards.size(); ++i) {
        m_clipCards[i]->show();
        ui->gridLayout->addWidget(m_clipCards[i], i / dynamicCols, i % dynamicCols);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateGridLayout();
}

void MainWindow::applyTheme() {
    QString styleSheet = R"(
        QMainWindow, QDialog, QWidget {
            background-color: #242528;
            color: #E0E0E0;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 13px;
        }

        QGroupBox {
            background-color: #242528;
            border: 1px solid #1c1d1f;
            border-radius: 12px;
            padding-top: 10px;
            margin-top: 10px;
            color: #E0E0E0;
            font-weight: bold;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px 0 5px;
            color: #2a8fa0;
            font-weight: bold;
            font-size: 12px;
        }

        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2a2c30, stop:1 #1e1f22);
            color: #E0E0E0;
            border-top: 1px solid #33363b;
            border-left: 1px solid #33363b;
            border-bottom: 1px solid #151618;
            border-right: 1px solid #151618;
            border-radius: 6px;
            padding: 4px 10px;
            font-weight: bold;
            font-size: 11px;
            min-height: 22px;
            height: 22px;
        }

        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2e3136, stop:1 #222326);
            color: #FFFFFF;
        }

        QPushButton:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #191a1c, stop:1 #2b2d32);
            border-top: 1px solid #121314;
            border-left: 1px solid #121314;
            border-bottom: 1px solid #3a3d43;
            border-right: 1px solid #3a3d43;
            color: #aaaaaa;
        }

        QPushButton#accentButton, QPushButton[text*="Load"], QPushButton[text*="Play"], QPushButton[text*="Fullscreen"] {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3a6670, stop:1 #1f3d45);
            color: #FFFFFF;
            border-top: 1px solid #4a7f8c;
            border-left: 1px solid #4a7f8c;
            border-bottom: 1px solid #112226;
            border-right: 1px solid #112226;
            padding: 4px 12px;
            font-size: 11px;
            min-height: 22px;
            height: 22px;
        }

        QPushButton#accentButton:pressed, QPushButton[text*="Play"]:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #15292e, stop:1 #2f545c);
            border-top: 1px solid #0f1d21;
            border-left: 1px solid #0f1d21;
        }

        QScrollBar:vertical {
            background-color: #1c1d1f;
            width: 12px;
            margin: 0px;
            border-radius: 6px;
        }

        QScrollBar::handle:vertical {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #32353a, stop:1 #242528);
            min-height: 20px;
            border-radius: 6px;
            border: 1px solid #151618;
        }

        QScrollBar::handle:vertical:hover { background-color: #3d4147; }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            background: none;
            height: 0px;
        }

        QScrollBar:horizontal {
            background-color: #1c1d1f;
            height: 12px;
            margin: 0px;
            border-radius: 6px;
        }

        QScrollBar::handle:horizontal {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #32353a, stop:1 #242528);
            min-width: 20px;
            border-radius: 6px;
            border: 1px solid #151618;
        }

        QScrollBar::handle:horizontal:hover { background-color: #3d4147; }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            background: none;
            width: 0px;
        }

        QSlider::groove:horizontal {
            border: 1px solid #1c1d1f;
            height: 6px;
            background: #18191b;
            border-radius: 3px;
        }

        QSlider::sub-page:horizontal {
            background: #2a5c66;
            border-radius: 3px;
        }

        QSlider::handle:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3a3d43, stop:1 #1c1d1f);
            border: 1px solid #4a4e56;
            width: 14px;
            margin-top: -5px;
            margin-bottom: -5px;
            border-radius: 7px;
        }

        QSlider::handle:horizontal:hover { background: #4a4e56; }

        QSlider::groove:vertical {
            border: 1px solid #1c1d1f;
            width: 6px;
            background: #18191b;
            border-radius: 3px;
        }

        QSlider::handle:vertical {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3a3d43, stop:1 #1c1d1f);
            border: 1px solid #4a4e56;
            height: 14px;
            margin-left: -5px;
            margin-right: -5px;
            border-radius: 7px;
        }

        QLabel {
            color: #E0E0E0;
            background-color: transparent;
            font-size: 12px;
        }

        QSpinBox {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2a2c30, stop:1 #1e1f22);
            color: #E0E0E0;
            border-top: 1px solid #33363b;
            border-left: 1px solid #33363b;
            border-bottom: 1px solid #151618;
            border-right: 1px solid #151618;
            border-radius: 6px;
            padding: 4px;
            selection-background-color: #2a5c66;
        }

        QSpinBox:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2e3136, stop:1 #222326);
        }

        QSpinBox::up-button, QSpinBox::down-button {
            background-color: #1e1f22;
            border: 1px solid #151618;
            border-radius: 3px;
            width: 16px;
        }

        QSpinBox::up-button:pressed, QSpinBox::down-button:pressed {
            background-color: #151618;
        }

        QListWidget {
            background-color: #1c1d1f;
            border: 1px solid #151618;
            border-radius: 8px;
            color: #E0E0E0;
        }

        QListWidget::item { padding: 4px; }
        QListWidget::item:selected { background-color: #2a5c66; color: #FFFFFF; }
        QListWidget::item:hover { background-color: #2a2c30; }
    )";
    qApp->setStyle("fusion");
    qApp->setStyleSheet(styleSheet);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    QStringList paths;
    for (const QUrl &url : mimeData->urls()) {
        QString p = url.toLocalFile();
        QFileInfo fi(p);
        if (fi.isDir()) {
            clipManager.addFolder(p);
        } else {
            paths << p;
        }
    }
    if (!paths.isEmpty())
        clipManager.addFiles(paths);
    rebuildGrid();
    event->acceptProposedAction();
}

static QString formatTimeShort(double secs) {
    if (secs < 0) secs = 0;
    int m = (int)secs / 60, s = (int)secs % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// Shared logic for assigning any card to a deck.
static void assignCardToDeck(ClipCard *card, bool deckA,
                             VideoWidget *out,
                             QSlider *progressSlider, QPushButton *playBtn,
                             QLabel *selectedLabel, QLabel *timeLabel) {
    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = card->sourceDescriptor();

    switch (desc.kind) {

    case Kind::VideoFile:
    case Kind::Image: {
        if (deckA) {
            out->setRepeatA(card->isRepeat());
            out->setTrimPointsA(card->startTime(), card->endTime());
            out->setCropA(card->cropX(), card->cropY(), card->cropW(), card->cropH());
            out->setOverlaysA(card->overlays());
            out->loadVideoA(desc.path);
            if (card->startTime() > 0) out->seekA(card->startTime());
            out->playA();
        } else {
            out->setRepeatB(card->isRepeat());
            out->setTrimPointsB(card->startTime(), card->endTime());
            out->setCropB(card->cropX(), card->cropY(), card->cropW(), card->cropH());
            out->setOverlaysB(card->overlays());
            out->loadVideoB(desc.path);
            if (card->startTime() > 0) out->seekB(card->startTime());
            out->playB();
        }
        double dur = deckA ? out->getDurationA() : out->getDurationB();
        progressSlider->setRange(0, 1000);
        progressSlider->setValue(0);
        progressSlider->setEnabled(desc.kind == Kind::VideoFile && dur > 0);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", card->sourceName()));
        timeLabel->setText(formatTimeShort(card->startTime()) + " / " + formatTimeShort(dur));
        break;
    }

    case Kind::Slideshow: {
        auto src = std::make_unique<SlideshowSource>();
        if (!src->loadFolder(desc.path, desc.slideshowIntervalMs)) {
            QMessageBox::warning(nullptr, "Slideshow", "No images found in folder.");
            return;
        }
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", card->sourceName()));
        timeLabel->setText("—");
        break;
    }

    case Kind::Camera: {
        auto src = std::make_unique<CameraSource>();

        if (desc.path.isEmpty()) {
            // "Default Camera" — QCamera() with no device arg; Qt picks the
            // default camera internally bypassing QMediaDevices enumeration.
            src->start({});
        } else {
            // Try to match stored device ID against Qt's enumerated list.
            bool matched = false;
            const auto qtDevices = QMediaDevices::videoInputs();
            for (const auto &dev : qtDevices) {
                if (QString::fromUtf8(dev.id()) == desc.path) {
                    src->start(dev);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                // Device not in Qt list (e.g. pure V4L2 path) — use startDevice
                // which also falls back to default if unmatched.
                src->startDevice(desc.path);
            }
        }

        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", card->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Screen: {
        auto scrs = QGuiApplication::screens();
        if (desc.screenIndex >= scrs.size()) return;
        auto src = std::make_unique<ScreenSource>();
        if (!src->start(scrs[desc.screenIndex])) return;
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", card->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Window: {
        const auto wins = WindowCaptureSource::capturableWindows();
        if (desc.windowIndex >= wins.size()) {
            QMessageBox::warning(nullptr, "Window Capture",
                "The selected window is no longer available.\nPlease add a new Window Capture source.");
            return;
        }
        auto src = std::make_unique<WindowCaptureSource>();
        if (!src->start(wins[desc.windowIndex])) return;
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", card->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Color: {
        auto src = std::make_unique<ColorSource>(desc.color);
        if (deckA) out->setSourceA(std::move(src));
        else        out->setSourceB(std::move(src));
        progressSlider->setEnabled(false);
        playBtn->setEnabled(false);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", card->sourceName()));
        timeLabel->setText("—");
        break;
    }

    }
}

void MainWindow::onAButtonClicked(int index) {
    ClipCard *card = cardAtIndex(index);
    if (!card || !card->hasSource()) return;

    if (aClipIndex >= 0) { if (auto *c = cardAtIndex(aClipIndex)) c->setASelected(false); }
    aClipIndex = index;
    card->setASelected(true);

    assignCardToDeck(card, true, outputWindow->videoWidget(),
                     ui->aProgressSlider, ui->aDeckPlayBtn,
                     ui->aSelectedLabel,  ui->aTimeLabel);
}

void MainWindow::onBButtonClicked(int index) {
    ClipCard *card = cardAtIndex(index);
    if (!card || !card->hasSource()) return;

    if (bClipIndex >= 0) { if (auto *c = cardAtIndex(bClipIndex)) c->setBSelected(false); }
    bClipIndex = index;
    card->setBSelected(true);

    assignCardToDeck(card, false, outputWindow->videoWidget(),
                     ui->bProgressSlider, ui->bDeckPlayBtn,
                     ui->bSelectedLabel,  ui->bTimeLabel);
}

QString MainWindow::formatTimeShort(double secs) {
    return ::formatTimeShort(secs);   // delegates to the file-scope helper above
}

// ── Add Element handlers ──────────────────────────────────────────────────────

void MainWindow::onAddElementSlideshow() {
    QString folder = QFileDialog::getExistingDirectory(this, "Select Image Folder for Slideshow");
    if (folder.isEmpty()) return;

    bool ok = false;
    int interval = QInputDialog::getInt(this, "Slideshow Interval",
                                        "Seconds per slide:", 3, 1, 60, 1, &ok);
    if (!ok) return;

    // Use the first image as the thumbnail.
    QDir dir(folder);
    QStringList imgs = dir.entryList({"*.png","*.jpg","*.jpeg","*.bmp","*.webp"},
                                     QDir::Files, QDir::Name);
    QPixmap thumb;
    if (!imgs.isEmpty())
        thumb = ThumbnailExtractor::extract(dir.absoluteFilePath(imgs.first()), 110, 65);
    if (thumb.isNull())
        thumb = makeIconThumb("📁");

    SourceDescriptor desc;
    desc.kind                = SourceDescriptor::Kind::Slideshow;
    desc.path                = folder;
    desc.displayName         = QFileInfo(folder).fileName();
    desc.slideshowIntervalMs = interval * 1000;

    addElementCard(desc, thumb);
}

void MainWindow::onAddElementCamera() {
    // Build device list ────────────────────────────────────────────────────────
    // Strategy:
    //   1. Qt/GStreamer enumeration (nice names). Retry once after 800 ms to let
    //      GStreamer's device monitor finish lazy initialisation.
    //   2. POSIX glob for /dev/video* to catch devices GStreamer doesn't surface.
    //   3. Always add a "Default Camera" fallback entry so the user is never
    //      blocked — QCamera() with no device arg can work even when
    //      QMediaDevices::videoInputs() returns empty (Intel IPU6 / MIPI cameras).

    auto qtDevices = QMediaDevices::videoInputs();
    if (qtDevices.isEmpty()) {
        QEventLoop loop;
        QTimer::singleShot(1200, &loop, &QEventLoop::quit);
        loop.exec();
        qtDevices = QMediaDevices::videoInputs();
    }

    struct CamEntry { QString id; QString label; bool isDefault; };
    QList<CamEntry> devices;

    for (const auto &d : qtDevices) {
        QString id    = QString::fromUtf8(d.id());
        QString label = d.description().isEmpty() ? id
                      : QString("%1  [%2]").arg(d.description(), id);
        devices.append({id, label, false});
    }

    // Merge /dev/videoN nodes not already in the Qt list
    {
        glob_t g{};
        if (::glob("/dev/video*", GLOB_NOSORT, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                QString path = QString::fromLocal8Bit(g.gl_pathv[i]);
                bool already = std::any_of(devices.begin(), devices.end(),
                                           [&](const CamEntry &e){ return e.id == path; });
                if (!already)
                    devices.append({path, path, false});
            }
        }
        ::globfree(&g);
    }

    // Always include a "Default Camera" option (QCamera with no device arg)
    devices.append({"", "Default Camera  (let the system choose)", true});

    QStringList names;
    for (const auto &e : devices) names << e.label;

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, "Select Camera",
                                           "Camera device:", names, 0, false, &ok);
    if (!ok) return;

    int idx = names.indexOf(chosen);
    const CamEntry &entry = devices[idx];

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Camera;
    desc.path        = entry.id;          // empty string → default camera
    desc.displayName = entry.isDefault ? "Default Camera"
                     : entry.label.section("  [", 0, 0).trimmed();
    if (desc.displayName.isEmpty()) desc.displayName = entry.id;
    desc.cameraIndex = idx;

    addElementCard(desc, makeIconThumb("📷"));
}

void MainWindow::onAddElementScreen() {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        QMessageBox::information(this, "Screen", "No screens found.");
        return;
    }

    QStringList names;
    for (const auto *s : screens)
        names << QString("%1 (%2×%3)").arg(s->name())
                                      .arg(s->size().width())
                                      .arg(s->size().height());

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, "Select Screen", "Screen:", names, 0, false, &ok);
    if (!ok) return;

    int idx = names.indexOf(chosen);

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Screen;
    desc.displayName = screens[idx]->name();
    desc.screenIndex = idx;

    addElementCard(desc, makeIconThumb("🖥"));
}

void MainWindow::onAddElementWindow() {
    const auto windows = WindowCaptureSource::capturableWindows();

    if (windows.isEmpty()) {
        QMessageBox::information(this, "Window / Tab Capture",
            "No capturable windows were found.\n\n"
            "On Wayland the system portal will let you choose any window "
            "or browser tab when the source is sent to a deck.\n\n"
            "Try adding the source anyway and pressing A or B — "
            "the portal picker will appear at that point.");

        // Add a generic 'Portal Window' card that triggers the portal at assign time
        SourceDescriptor desc;
        desc.kind        = SourceDescriptor::Kind::Window;
        desc.displayName = "Portal Window";
        desc.windowIndex = 0;
        addElementCard(desc, makeIconThumb("🪟"));
        return;
    }

    QStringList names;
    for (const auto &w : windows)
        names << (w.description().isEmpty() ? "(unnamed)" : w.description());

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, "Select Window / Tab",
                                           "Capturable windows:", names, 0, false, &ok);
    if (!ok) return;

    int idx = names.indexOf(chosen);

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Window;
    desc.displayName = names[idx];
    desc.windowIndex = idx;

    addElementCard(desc, makeIconThumb("🪟"));
}

void MainWindow::onAddElementColor() {
    QColor color = QColorDialog::getColor(Qt::black, this, "Pick Solid Color");
    if (!color.isValid()) return;

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Color;
    desc.color       = color;
    desc.displayName = color.name().toUpper();

    addElementCard(desc, makeColorThumb(color));
}

// ── Card management ────────────────────────────────────────────────────────────

void MainWindow::onCardRemoveRequested(int index) {
    const int nFile = m_clipCards.size();

    if (index < nFile) {
        // File-based clip — remove from ClipManager and rebuild.
        clipManager.removeClip(index);
        rebuildGrid();
    } else {
        // Live element card — remove from m_liveCards and delete.
        int liveIdx = index - nFile;
        if (liveIdx < 0 || liveIdx >= m_liveCards.size()) return;
        ClipCard *card = m_liveCards.takeAt(liveIdx);
        card->deleteLater();
        rebuildGrid();
    }
}

void MainWindow::onCardSourceDescriptorChanged(int /*index*/, const SourceDescriptor & /*desc*/) {
    // The card already updated its own m_sourceDesc.
    // If the card is currently assigned to a deck, re-assign the new source.
    // For now, no automatic re-push — user can press A/B again to pick it up.
}
