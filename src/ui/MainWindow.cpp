#include "ui/MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/VideoWidget.h"
#include "core/ThumbnailExtractor.h"
#include "core/VideoFileSource.h"
#include "core/SlideshowSource.h"
#include "core/CameraSource.h"
#include "core/ScreenSource.h"
#include "core/CanvasSource.h"
#include "core/ImageSource.h"
#include "core/ShaderSource.h"
#include "core/HtmlSource.h"
#include "ui/ShaderEditDialog.h"
#include "ui/HtmlEditDialog.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QShortcut>
#include <QComboBox>
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
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QDoubleSpinBox>
#include <QWebEngineView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <numeric>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    setAcceptDrops(true);

    outputWindow = new OutputWindow(this);
    outputWindow->show();

    // Create a stacked widget to manage placeholder vs. node editor
    m_stackWidget = new QStackedWidget(ui->gridWidget);
    ui->gridLayout->addWidget(m_stackWidget, 0, 0, 1, 1);

    // Initialize the ClipNodeEditor
    m_clipNodeEditor = new ClipNodeEditor();
    m_clipNodeEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stackWidget->addWidget(m_clipNodeEditor);

    // Build the empty-state placeholder (shown before any media is loaded)
    m_emptyPlaceholder = new QWidget();
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

    m_stackWidget->addWidget(m_emptyPlaceholder);
    m_stackWidget->setCurrentWidget(m_emptyPlaceholder);

    // ── Transition mode combobox & Auto/Cut controls ──────────────────────────
    // Inject transition controls into the A/B Crossfader group (faderGroup),
    // right after the "B ►" label and before the vertical spacer.
    {
        auto *faderLayout = qobject_cast<QVBoxLayout *>(ui->faderGroup->layout());
        if (faderLayout) {
            auto *lbl = new QLabel("Transition:", ui->faderGroup);
            lbl->setStyleSheet("font-size: 10px;");

            m_transitionCombo = new QComboBox(ui->faderGroup);
            m_transitionCombo->addItems({
                "Crossfade", "Cut",
                "Wipe ←", "Wipe →", "Wipe ↑", "Wipe ↓",
                "Slide ←", "Slide →", "Slide ↑", "Slide ↓",
                "Dip to Black", "Dip to White",
                "Additive Glow", "Cross Zoom",
                "Split Door (H)", "Split Door (V)",
                "Vortex Spin", "Split 4-Corner"
            });
            m_transitionCombo->setToolTip(
                "Crossfade: alpha blend\n"
                "Cut: hard switch at fader centre\n"
                "Wipe ←: B reveals from the left\n"
                "Wipe →: B reveals from the right\n"
                "Wipe ↑: B reveals from bottom to top\n"
                "Wipe ↓: B reveals from top to bottom\n"
                "Slide ←: A exits left, B enters right\n"
                "Slide →: A exits right, B enters left\n"
                "Slide ↑: A exits top, B enters bottom\n"
                "Slide ↓: A exits bottom, B enters top\n"
                "Dip to Black: fade through black\n"
                "Dip to White: fade through white\n"
                "Additive Glow: add colors for VJ brightness\n"
                "Cross Zoom: dynamic zoom scaling\n"
                "Split Door (H): horizontal split reveal\n"
                "Split Door (V): vertical split reveal\n"
                "Vortex Spin: spinning vortex wrap\n"
                "Split 4-Corner: split into 4 quadrants sliding outwards"
            );
            m_transitionCombo->setStyleSheet("font-size: 10px;");

            // Time/Duration spinbox
            auto *durContainer = new QWidget(ui->faderGroup);
            auto *durLayout = new QHBoxLayout(durContainer);
            durLayout->setContentsMargins(0, 0, 0, 0);
            durLayout->setSpacing(4);
            auto *durLbl = new QLabel("Time (s):", durContainer);
            durLbl->setStyleSheet("font-size: 10px;");
            m_durationSpin = new QDoubleSpinBox(durContainer);
            m_durationSpin->setRange(0.1, 10.0);
            m_durationSpin->setSingleStep(0.1);
            m_durationSpin->setValue(1.0);
            m_durationSpin->setStyleSheet("font-size: 10px; background-color: #1e1e1e; color: #fff;");
            durLayout->addWidget(durLbl);
            durLayout->addWidget(m_durationSpin);

            // Auto / Cut buttons
            auto *btnContainer = new QWidget(ui->faderGroup);
            auto *btnLayout = new QHBoxLayout(btnContainer);
            btnLayout->setContentsMargins(0, 0, 0, 0);
            btnLayout->setSpacing(6);
            m_autoBtn = new QPushButton("AUTO", btnContainer);
            m_autoBtn->setToolTip("Smooth auto transition using selected mode");
            m_autoBtn->setStyleSheet(
                "QPushButton {"
                "  font-size: 10px; font-weight: bold; background-color: #1E1E1E; color: #00FFFF; "
                "  border: 1px solid #00BFFF; border-radius: 3px; padding: 4px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #00BFFF; color: #0F0F0F; border: 1px solid #00FFFF;"
                "}"
                "QPushButton:pressed {"
                "  background-color: #FF4500; border: 1px solid #FFD700; color: #0F0F0F;"
                "}"
            );
            m_cutBtn = new QPushButton("CUT", btnContainer);
            m_cutBtn->setToolTip("Instant cut to opposite deck");
            m_cutBtn->setStyleSheet(
                "QPushButton {"
                "  font-size: 10px; font-weight: bold; background-color: #1E1E1E; color: #FF4500; "
                "  border: 1px solid #FF4500; border-radius: 3px; padding: 4px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #FF4500; color: #0F0F0F; border: 1px solid #FFD700;"
                "}"
                "QPushButton:pressed {"
                "  background-color: #00BFFF; border: 1px solid #00FFFF; color: #0F0F0F;"
                "}"
            );
            btnLayout->addWidget(m_autoBtn);
            btnLayout->addWidget(m_cutBtn);

            // Insert before the spacer (index 3: labelA=0, slider=1, labelB=2, spacer=3)
            faderLayout->insertWidget(3, btnContainer);
            faderLayout->insertWidget(3, durContainer);
            faderLayout->insertWidget(3, m_transitionCombo);
            faderLayout->insertWidget(3, lbl);
        }
    }

    setupConnections();
    applyTheme();

    // Kick the Qt Multimedia GStreamer backend into life early so that
    // QMediaDevices::videoInputs() is populated by the time the user
    // clicks "Add Camera".  The singleShot lets the event loop spin once
    // first so the window is already shown.
    QTimer::singleShot(0, []() {
        [[maybe_unused]] auto _ = QMediaDevices::videoInputs();
    });

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::onTimerUpdate);
    updateTimer->start(100);

    qDebug() << "SwitchX initialized - Live Media Control Mode";
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::setupConnections() {
    connect(ui->actionLoadFolder, &QAction::triggered, this, &MainWindow::onLoadFolderClicked);
    connect(ui->actionAddFolder,  &QAction::triggered, this, &MainWindow::onAddFolderClicked);
    connect(ui->actionAddFiles,   &QAction::triggered, this, &MainWindow::onAddFilesClicked);
    connect(ui->actionClearAll,   &QAction::triggered, this, &MainWindow::onClearAllClicked);

    // ── Session save / load ───────────────────────────────────────────────────
    ui->menuMedia->addSeparator();
    {
        auto *saveAct = new QAction("💾  Save Session…", this);
        saveAct->setShortcut(QKeySequence::Save);
        connect(saveAct, &QAction::triggered, this, &MainWindow::onSaveSessionClicked);
        ui->menuMedia->addAction(saveAct);

        auto *loadAct = new QAction("📂  Load Session…", this);
        loadAct->setShortcut(QKeySequence::Open);
        connect(loadAct, &QAction::triggered, this, &MainWindow::onLoadSessionClicked);
        ui->menuMedia->addAction(loadAct);
    }

    connect(ui->actionShowOutput, &QAction::triggered, this, [this]() {
        outputWindow->show();
        outputWindow->raise();
        outputWindow->activateWindow();
    });
    connect(ui->actionStayOnTop, &QAction::toggled, this, [this](bool on) {
        Qt::WindowFlags flags = outputWindow->windowFlags();
        if (on)
            flags |= Qt::WindowStaysOnTopHint;
        else
            flags &= ~Qt::WindowStaysOnTopHint;
        outputWindow->setWindowFlags(flags);
        outputWindow->show();
    });

    // ── Add Element submenu in menubar ────────────────────────────────────────
    auto *addElemMenu = ui->menuMedia->addMenu("Add Element");
    addElemMenu->addAction("🎬  Video File…",  this, &MainWindow::onAddFilesClicked);
    addElemMenu->addAction("🖼  Photo…",       this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Add Photos", "",
            "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)");
        if (files.isEmpty()) return;
        const QStringList before = clipManager.getClips();
        clipManager.addFiles(files);
        QStringList added;
        const QStringList after = clipManager.getClips();
        for (const QString &clipPath : after) {
            if (!before.contains(clipPath))
                added << clipPath;
        }
        appendClipsToEditor(added);
    });
    addElemMenu->addSeparator();
    addElemMenu->addAction("📁  Slideshow…",   this, &MainWindow::onAddElementSlideshow);
    addElemMenu->addSeparator();
    addElemMenu->addAction("📷  Camera…",          this, &MainWindow::onAddElementCamera);
    addElemMenu->addAction("🖥  Screen Capture…",  this, &MainWindow::onAddElementScreen);
    addElemMenu->addAction("🪟  Window / Tab…",    this, &MainWindow::onAddElementWindow);
    addElemMenu->addSeparator();
    addElemMenu->addAction("⬜  Canvas…",          this, &MainWindow::onAddElementCanvas);
    addElemMenu->addSeparator();
    addElemMenu->addAction("≋  Shader…",           this, &MainWindow::onAddElementShader);
    addElemMenu->addAction("🌐  HTML Overlay…",        this, &MainWindow::onAddElementDynamicInterface);

    // ── ClipNodeEditor signals ────────────────────────────────────────────────
    connect(m_clipNodeEditor, &ClipNodeEditor::deckAClipChanged, this, &MainWindow::onNodeAButtonClicked);
    connect(m_clipNodeEditor, &ClipNodeEditor::deckBClipChanged, this, &MainWindow::onNodeBButtonClicked);
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,   this, &MainWindow::assignHotkeyToNode);
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeRemoved, this, &MainWindow::onNodeRemoveRequested);

    // ── Transition combobox & buttons ─────────────────────────────────────────
    if (m_transitionCombo) {
        connect(m_transitionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onTransitionModeChanged);
    }
    if (m_autoBtn) {
        connect(m_autoBtn, &QPushButton::clicked, this, &MainWindow::onAutoTransitionClicked);
    }
    if (m_cutBtn) {
        connect(m_cutBtn, &QPushButton::clicked, this, &MainWindow::onCutTransitionClicked);
    }
    connect(ui->crossfaderSlider, &QSlider::sliderPressed, this, [this]() {
        if (m_transitionAnimation) {
            m_transitionAnimation->stop();
            m_transitionAnimation->deleteLater();
            m_transitionAnimation = nullptr;
        }
    });

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

void MainWindow::onLoadFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Select Media Folder");
    if (path.isEmpty()) return;
    m_clipNodeEditor->clearAllNodes();
    clipManager.loadFolder(path);
    m_aClipNodeId = 0;
    m_bClipNodeId = 0;
    outputWindow->videoWidget()->setNodeChainA({});
    outputWindow->videoWidget()->setNodeChainB({});
    for (int i = 0; i < clipManager.getClipCount(); ++i) {
        QString clipPath = clipManager.getClipPath(i);
        QPixmap thumb = ThumbnailExtractor::extract(clipPath, 110, 65);
        m_clipNodeEditor->addClipNode(clipPath, thumb);
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::onAddFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Add Media Folder");
    if (path.isEmpty()) return;
    const QStringList before = clipManager.getClips();
    clipManager.addFolder(path);
    QStringList added;
    const QStringList after = clipManager.getClips();
    for (const QString &clipPath : after) {
        if (!before.contains(clipPath))
            added << clipPath;
    }
    appendClipsToEditor(added);
}

void MainWindow::onAddFilesClicked() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Media Files", "",
        "Media Files (*.mp4 *.avi *.mov *.mkv *.webm *.png *.jpg *.jpeg *.bmp *.webp *.gif)");
    if (files.isEmpty()) return;
    const QStringList before = clipManager.getClips();
    clipManager.addFiles(files);
    QStringList added;
    const QStringList after = clipManager.getClips();
    for (const QString &clipPath : after) {
        if (!before.contains(clipPath))
            added << clipPath;
    }
    appendClipsToEditor(added);
}

void MainWindow::onClearAllClicked() {
    clipManager.clear();
    m_clipNodeEditor->clearAllNodes();
    m_aClipNodeId = 0;
    m_bClipNodeId = 0;
    outputWindow->videoWidget()->setNodeChainA({});
    outputWindow->videoWidget()->setNodeChainB({});
    m_clipNodeEditor->hide();
    m_emptyPlaceholder->show();
}

// ── Element management ────────────────────────────────────────────────────────

void MainWindow::addElementNode(const SourceDescriptor &desc, const QPixmap &thumb) {
    m_clipNodeEditor->addSourceNode(desc, thumb);
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::appendClipsToEditor(const QStringList &clipPaths) {
    if (clipPaths.isEmpty()) return;

    for (const QString &path : clipPaths) {
        QPixmap thumb = ThumbnailExtractor::extract(path, 110, 65);
        m_clipNodeEditor->addClipNode(path, thumb);
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
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

QPixmap MainWindow::makeCanvasThumb(const QString &label,
                                    SourceDescriptor::CanvasFill fill,
                                    const QColor &color,
                                    int w, int h) {
    QPixmap pix(w, h);
    if (fill == SourceDescriptor::CanvasFill::Color) {
        pix.fill(color);
        return pix;
    }

    pix.fill(QColor("#1c1d1f"));
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#8b93a1"));
    p.setBrush(Qt::NoBrush);
    p.drawRect(8, 8, w - 16, h - 16);
    p.setPen(QColor("#c8ccd4"));
    p.drawText(pix.rect(), Qt::AlignCenter,
               fill == SourceDescriptor::CanvasFill::Transparent ? "TR" : label);
    return pix;
}

QPixmap MainWindow::makeShaderThumb(const QString &code, int w, int h) {
    ShaderSource src(code, QSize(w, h));
    if (!src.nextFrame() || !src.isReady())
        return makeIconThumb("≋", w, h);
    const uint8_t *data = src.frameData();
    QImage img(data, w, h, w * 3, QImage::Format_RGB888);
    return QPixmap::fromImage(img.copy());
}

QPixmap MainWindow::makeHtmlThumb(const QString &html, const QString &filePath, int w, int h) {
    QWebEngineView view;
    view.resize(1280, 720);
    view.setAttribute(Qt::WA_TranslucentBackground);
    view.page()->setBackgroundColor(Qt::transparent);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.show();

    QEventLoop loop;
    QObject::connect(&view, &QWebEngineView::loadFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit); // bail out after 8 s

    if (!filePath.isEmpty())
        view.load(QUrl::fromLocalFile(filePath));
    else
        view.setHtml(html, QUrl("qrc:/"));

    loop.exec();

    QPixmap grab = view.grab();
    if (grab.isNull())
        return makeIconThumb("🌐", w, h);
    return grab.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QPixmap MainWindow::makeQmlThumb(const QString &, int w, int h) {
    return makeIconThumb("🌐", w, h);
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

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    // ClipNodeEditor handles its own layout updates.
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

    const QStringList before = clipManager.getClips();
    bool hasContent = false;
    for (const QUrl &url : mimeData->urls()) {
        QString p = url.toLocalFile();
        QFileInfo fi(p);
        if (fi.isDir()) {
            clipManager.addFolder(p);
            hasContent = true;
        }
    }

    QStringList paths;
    for (const QUrl &url : mimeData->urls()) {
        QString p = url.toLocalFile();
        QFileInfo fi(p);
        if (!fi.isDir()) {
            paths << p;
            hasContent = true;
        }
    }

    if (!paths.isEmpty()) {
        clipManager.addFiles(paths);
        hasContent = true;
    }

    if (hasContent) {
        QStringList added;
        const QStringList after = clipManager.getClips();
        for (const QString &clipPath : after) {
            if (!before.contains(clipPath))
                added << clipPath;
        }
        appendClipsToEditor(added);
    }

    event->acceptProposedAction();
}

static QString formatTimeShort(double secs) {
    if (secs < 0) secs = 0;
    int m = (int)secs / 60, s = (int)secs % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

void MainWindow::assignNodeToDeck(ClipNodeModel *node, NodeId nodeId, bool deckA,
                                   VideoWidget *out,
                                   QSlider *progressSlider, QPushButton *playBtn,
                                   QLabel *selectedLabel, QLabel *timeLabel) {
    if (!node) return;

    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = node->sourceDescriptor();

    switch (desc.kind) {

    case Kind::VideoFile:
    case Kind::Image: {
        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            if (deckA) out->setSourceA(nullptr);
            else       out->setSourceB(nullptr);
            return;
        }

        if (deckA) {
            out->setRepeatA(node->isRepeat());
            out->setTrimPointsA(node->startTime(), node->endTime());
            out->setBaseA(baseX, baseY, baseW, baseH);
            out->setCropA(node->cropX(), node->cropY(), node->cropW(), node->cropH());
            out->setOverlaysA(node->overlays());
            out->loadVideoA(desc.path);
            if (node->startTime() > 0) out->seekA(node->startTime());
            out->playA();
        } else {
            out->setRepeatB(node->isRepeat());
            out->setTrimPointsB(node->startTime(), node->endTime());
            out->setBaseB(baseX, baseY, baseW, baseH);
            out->setCropB(node->cropX(), node->cropY(), node->cropW(), node->cropH());
            out->setOverlaysB(node->overlays());
            out->loadVideoB(desc.path);
            if (node->startTime() > 0) out->seekB(node->startTime());
            out->playB();
        }
        double dur = deckA ? out->getDurationA() : out->getDurationB();
        progressSlider->setRange(0, 1000);
        progressSlider->setValue(0);
        progressSlider->setEnabled(desc.kind == Kind::VideoFile && dur > 0);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText(formatTimeShort(node->startTime()) + " / " + formatTimeShort(dur));
        break;
    }

    case Kind::Slideshow: {
        auto src = std::make_unique<SlideshowSource>();
        if (!src->loadFolder(desc.path, desc.slideshowIntervalMs)) {
            QMessageBox::warning(nullptr, "Slideshow", "No images found in folder.");
            return;
        }

        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            baseX = 0.f; baseY = 0.f; baseW = 1.f; baseH = 1.f;
        }

        if (deckA) {
            out->setBaseA(baseX, baseY, baseW, baseH);
            out->setSourceA(std::move(src)); out->playA();
        } else {
            out->setBaseB(baseX, baseY, baseW, baseH);
            out->setSourceB(std::move(src)); out->playB();
        }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("—");
        break;
    }

    case Kind::Camera: {
        auto src = std::make_unique<CameraSource>();

        if (desc.path.isEmpty()) {
            src->start({});
        } else {
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
                src->startDevice(desc.path);
            }
        }

        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            baseX = 0.f; baseY = 0.f; baseW = 1.f; baseH = 1.f;
        }

        if (deckA) {
            out->setBaseA(baseX, baseY, baseW, baseH);
            out->setSourceA(std::move(src)); out->playA();
        } else {
            out->setBaseB(baseX, baseY, baseW, baseH);
            out->setSourceB(std::move(src)); out->playB();
        }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Screen: {
        auto src = std::make_unique<ScreenSource>();
        if (!src->start(ScreenSource::CaptureType::Monitor)) return;

        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            baseX = 0.f; baseY = 0.f; baseW = 1.f; baseH = 1.f;
        }

        if (deckA) {
            out->setBaseA(baseX, baseY, baseW, baseH);
            out->setSourceA(std::move(src)); out->playA();
        } else {
            out->setBaseB(baseX, baseY, baseW, baseH);
            out->setSourceB(std::move(src)); out->playB();
        }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Window: {
        auto src = std::make_unique<ScreenSource>();
        if (!src->start(ScreenSource::CaptureType::Window)) return;

        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            baseX = 0.f; baseY = 0.f; baseW = 1.f; baseH = 1.f;
        }

        if (deckA) {
            out->setBaseA(baseX, baseY, baseW, baseH);
            out->setSourceA(std::move(src)); out->playA();
        } else {
            out->setBaseB(baseX, baseY, baseW, baseH);
            out->setSourceB(std::move(src)); out->playB();
        }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Canvas: {
        const QSize size(desc.canvasWidth, desc.canvasHeight);

        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            baseX = 0.f; baseY = 0.f; baseW = 1.f; baseH = 1.f;
        }

        if (desc.canvasFill == SourceDescriptor::CanvasFill::Transparent) {
            if (deckA) out->setSourceA(nullptr);
            else        out->setSourceB(nullptr);
        } else {
            auto fill = (desc.canvasFill == SourceDescriptor::CanvasFill::Color)
                ? CanvasSource::Fill::SolidColor
                : CanvasSource::Fill::Checkered;
            auto src = std::make_unique<CanvasSource>(fill, size, desc.color);
            if (deckA) {
                out->setBaseA(baseX, baseY, baseW, baseH);
                out->setSourceA(std::move(src));
            } else {
                out->setBaseB(baseX, baseY, baseW, baseH);
                out->setSourceB(std::move(src));
            }
        }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(false);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText(QString("%1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight));
        break;
    }

    case Kind::Shader: {
        auto src = std::make_unique<ShaderSource>(desc.shaderCode);

        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            baseX = 0.f; baseY = 0.f; baseW = 1.f; baseH = 1.f;
        }

        if (deckA) {
            out->setBaseA(baseX, baseY, baseW, baseH);
            out->setSourceA(std::move(src)); out->playA();
        } else {
            out->setBaseB(baseX, baseY, baseW, baseH);
            out->setSourceB(std::move(src)); out->playB();
        }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(false);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Html: {
        auto src = std::make_unique<HtmlSource>(desc.htmlContent, desc.path);

        float baseX, baseY, baseW, baseH;
        if (!m_clipNodeEditor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
            baseX = 0.f; baseY = 0.f; baseW = 1.f; baseH = 1.f;
        }

        if (deckA) {
            out->setBaseA(baseX, baseY, baseW, baseH);
            out->setSourceA(std::move(src)); out->playA();
        } else {
            out->setBaseB(baseX, baseY, baseW, baseH);
            out->setSourceB(std::move(src)); out->playB();
        }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(false);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    }
}

static VideoWidget::NodeChainSource makeNodeChainSource(ClipNodeModel *node, ClipNodeEditor *editor) {
    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = node->sourceDescriptor();
    VideoWidget::NodeChainSource entry;
    entry.cropX = node->cropX(); entry.cropY = node->cropY();
    entry.cropW = node->cropW(); entry.cropH = node->cropH();

    if (!editor->clipTransform(node->nodeId(), entry.baseX, entry.baseY, entry.baseW, entry.baseH)) {
        entry.baseX = 0.f; entry.baseY = 0.f; entry.baseW = 1.f; entry.baseH = 1.f;
    }

    switch (desc.kind) {
    case Kind::VideoFile: {
        auto src = std::make_unique<VideoFileSource>();
        if (!src->open(desc.path)) break;
        if (node->startTime() > 0) src->seek(node->startTime());
        src->nextFrame();
        entry.playing = true;
        entry.source  = std::move(src);
        break;
    }
    case Kind::Image: {
        auto src = std::make_unique<ImageSource>();
        if (!src->load(desc.path)) break;
        src->nextFrame();
        entry.source = std::move(src);
        break;
    }
    case Kind::Canvas:
        if (desc.canvasFill == SourceDescriptor::CanvasFill::Transparent) {
            entry.source.reset();
            entry.playing = false;
            break;
        }
        entry.source = std::make_unique<CanvasSource>(
            desc.canvasFill == SourceDescriptor::CanvasFill::Color
                ? CanvasSource::Fill::SolidColor
                : CanvasSource::Fill::Checkered,
            QSize(desc.canvasWidth, desc.canvasHeight),
            desc.color);
        entry.playing = true;
        break;
    case Kind::Slideshow: {
        auto src = std::make_unique<SlideshowSource>();
        if (!src->loadFolder(desc.path, desc.slideshowIntervalMs)) break;
        entry.source  = std::move(src);
        entry.playing = true;
        break;
    }
    case Kind::Camera: {
        auto src = std::make_unique<CameraSource>();
        if (desc.path.isEmpty()) {
            src->start({});
        } else {
            bool matched = false;
            const auto devs = QMediaDevices::videoInputs();
            for (const auto &dev : devs) {
                if (QString::fromUtf8(dev.id()) == desc.path) {
                    src->start(dev); matched = true; break;
                }
            }
            if (!matched) src->startDevice(desc.path);
        }
        entry.source  = std::move(src);
        entry.playing = true;
        break;
    }
    case Kind::Screen: {
        auto src = std::make_unique<ScreenSource>();
        if (src->start(ScreenSource::CaptureType::Monitor)) {
            entry.source = std::move(src);
            entry.playing = true;
        }
        break;
    }
    case Kind::Window: {
        auto src = std::make_unique<ScreenSource>();
        if (src->start(ScreenSource::CaptureType::Window)) {
            entry.source = std::move(src);
            entry.playing = true;
        }
        break;
    }
    case Kind::Shader:
        entry.source  = std::make_unique<ShaderSource>(desc.shaderCode);
        entry.playing = true;
        break;
    case Kind::Html:
        entry.source  = std::make_unique<HtmlSource>(desc.htmlContent, desc.path);
        entry.playing = true;
        break;
    }
    return entry;
}

static std::vector<VideoWidget::NodeChainSource>
buildNodeChain(const QVector<ClipNodeModel *> &chain, ClipNodeEditor *editor, int canvasWidth = 0, int canvasHeight = 0) {
    std::vector<VideoWidget::NodeChainSource> out;
    for (int i = 1; i < chain.size(); ++i) {
        auto entry = makeNodeChainSource(chain[i], editor);
        if (entry.source) {
            entry.canvasWidth = canvasWidth;
            entry.canvasHeight = canvasHeight;
            out.push_back(std::move(entry));
        }
    }
    return out;
}

void MainWindow::onNodeAButtonClicked(NodeId nodeId) {
    if (!nodeId) return;
    ClipNodeModel *node = m_clipNodeEditor->nodeAt(nodeId);
    if (!node || !node->hasSource()) return;

    if (m_aClipNodeId) {
        if (auto *oldNode = m_clipNodeEditor->nodeAt(m_aClipNodeId))
            oldNode->setASelected(false);
    }
    m_aClipNodeId = nodeId;
    node->setASelected(true);

    int canvasW = 0, canvasH = 0;
    m_clipNodeEditor->contextCanvasSize(nodeId, canvasW, canvasH);

    auto *out = outputWindow->videoWidget();
    out->setCanvasSizeA(canvasW, canvasH);
    assignNodeToDeck(node, nodeId, true, out, ui->aProgressSlider, ui->aDeckPlayBtn,
                     ui->aSelectedLabel, ui->aTimeLabel);
    out->setNodeChainA(buildNodeChain(m_clipNodeEditor->getClipChain(nodeId), m_clipNodeEditor, canvasW, canvasH));
}

void MainWindow::onNodeBButtonClicked(NodeId nodeId) {
    if (!nodeId) return;
    ClipNodeModel *node = m_clipNodeEditor->nodeAt(nodeId);
    if (!node || !node->hasSource()) return;

    if (m_bClipNodeId) {
        if (auto *oldNode = m_clipNodeEditor->nodeAt(m_bClipNodeId))
            oldNode->setBSelected(false);
    }
    m_bClipNodeId = nodeId;
    node->setBSelected(true);

    int canvasW = 0, canvasH = 0;
    m_clipNodeEditor->contextCanvasSize(nodeId, canvasW, canvasH);

    auto *out = outputWindow->videoWidget();
    out->setCanvasSizeB(canvasW, canvasH);
    assignNodeToDeck(node, nodeId, false, out, ui->bProgressSlider, ui->bDeckPlayBtn,
                     ui->bSelectedLabel, ui->bTimeLabel);
    out->setNodeChainB(buildNodeChain(m_clipNodeEditor->getClipChain(nodeId), m_clipNodeEditor, canvasW, canvasH));
}

void MainWindow::onNodeRemoveRequested(NodeId nodeId) {
    releaseHotkeyForNode(nodeId);
    m_clipNodeEditor->removeNode(nodeId);
    auto *out = outputWindow->videoWidget();
    if (m_aClipNodeId == nodeId) { m_aClipNodeId = 0; out->setNodeChainA({}); }
    if (m_bClipNodeId == nodeId) { m_bClipNodeId = 0; out->setNodeChainB({}); }
    if (m_clipNodeEditor->allNodes().isEmpty()) {
        m_clipNodeEditor->hide();
        m_emptyPlaceholder->show();
    }
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

    addElementNode(desc, thumb);
}

void MainWindow::onAddElementCamera() {
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
    desc.path        = entry.id;
    desc.displayName = entry.isDefault ? "Default Camera"
                     : entry.label.section("  [", 0, 0).trimmed();
    if (desc.displayName.isEmpty()) desc.displayName = entry.id;
    desc.cameraIndex = idx;

    addElementNode(desc, makeIconThumb("📷"));
}

void MainWindow::onAddElementScreen() {
    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Screen;
    desc.displayName = "Screen Capture";
    desc.screenIndex = 0;

    addElementNode(desc, makeIconThumb("🖥"));
}

void MainWindow::onAddElementWindow() {
    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Window;
    desc.displayName = "Window / Tab";
    desc.windowIndex = 0;

    addElementNode(desc, makeIconThumb("🪟"));
}

void MainWindow::onAddElementCanvas() {
    struct CanvasPreset {
        const char *label;
        int width;
        int height;
    };
    const CanvasPreset presets[] = {
        {"16:9  (1280x720)", 1280, 720},
        {"4:3  (1024x768)", 1024, 768},
        {"1:1  (1080x1080)", 1080, 1080},
        {"9:16  (1080x1920)", 1080, 1920},
    };

    QStringList options;
    for (const auto &preset : presets) options << QString::fromUtf8(preset.label);
    options << "Custom…";

    bool ok = false;
    const QString choice = QInputDialog::getItem(this, "Canvas",
                                                 "Aspect ratio:", options, 0, false, &ok);
    if (!ok || choice.isEmpty()) return;

    int width = 1280;
    int height = 720;
    if (choice == "Custom…") {
        width = QInputDialog::getInt(this, "Canvas Width", "Width:", 1280, 16, 16384, 1, &ok);
        if (!ok) return;
        height = QInputDialog::getInt(this, "Canvas Height", "Height:", 720, 16, 16384, 1, &ok);
        if (!ok) return;
    } else {
        for (const auto &preset : presets) {
            if (choice == QString::fromUtf8(preset.label)) {
                width = preset.width;
                height = preset.height;
                break;
            }
        }
    }

    const int g = std::gcd(width, height);
    const QString ratioText = QString("%1:%2").arg(width / g).arg(height / g);

    const QStringList fillOptions = {"Checkered", "Transparent", "Color"};
    const QString fillChoice = QInputDialog::getItem(this, "Canvas Fill",
                                                     "Fill type:", fillOptions, 0, false, &ok);
    if (!ok || fillChoice.isEmpty()) return;

    SourceDescriptor desc;
    desc.kind         = SourceDescriptor::Kind::Canvas;
    desc.canvasWidth  = width;
    desc.canvasHeight = height;
    desc.canvasFill   = SourceDescriptor::CanvasFill::Checkered;
    desc.color        = Qt::white;
    QString fillLabel = "Checkered";
    if (fillChoice == "Transparent") {
        desc.canvasFill = SourceDescriptor::CanvasFill::Transparent;
        fillLabel = "Transparent";
    } else if (fillChoice == "Color") {
        QColor c = QColorDialog::getColor(Qt::white, this, "Pick Canvas Color");
        if (!c.isValid()) return;
        desc.canvasFill = SourceDescriptor::CanvasFill::Color;
        desc.color = c;
        fillLabel = c.name().toUpper();
    }

    desc.displayName  = QString("Canvas %1 (%2)").arg(ratioText, fillLabel);
    addElementNode(desc, makeCanvasThumb(ratioText, desc.canvasFill, desc.color));
}

void MainWindow::onAddElementShader() {
    ShaderEditDialog dlg(QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString code = dlg.resultCode().trimmed();
    if (code.isEmpty()) return;

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Shader;
    desc.shaderCode  = code;
    desc.displayName = "Shader";

    addElementNode(desc, makeShaderThumb(code));
}

void MainWindow::onAddElementDynamicInterface() {
    HtmlEditDialog dlg(QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString filePath = dlg.resultFilePath();
    QString html     = dlg.resultHtml().trimmed();
    if (filePath.isEmpty() && html.isEmpty()) return;

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Html;
    desc.htmlContent = html;
    desc.path        = filePath;
    desc.displayName = filePath.isEmpty() ? "HTML Overlay"
                                          : QFileInfo(filePath).fileName();

    addElementNode(desc, makeHtmlThumb(html, filePath));
}

// ── Hotkey grid ───────────────────────────────────────────────────────────────

const QList<Qt::Key> &MainWindow::hotkeySequence() {
    static const QList<Qt::Key> seq = {
        // Row 1: number row
        Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,
        Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9, Qt::Key_0,
        // Row 2: QWERTY top
        Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
        Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
        // Row 3: home row
        Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G,
        Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L,
        // Row 4: bottom row
        Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B,
        Qt::Key_N, Qt::Key_M,
    };
    return seq;
}

void MainWindow::assignHotkeyToNode(NodeId nodeId) {
    ClipNodeModel *node = m_clipNodeEditor->nodeAt(nodeId);
    if (!node) return;

    // Find the first unoccupied slot in the VJ grid sequence.
    Qt::Key chosen = Qt::Key_unknown;
    for (Qt::Key k : hotkeySequence()) {
        if (!m_keyToNode.contains(k)) { chosen = k; break; }
    }
    if (chosen == Qt::Key_unknown) return; // All 36 slots occupied

    // Register the mapping.
    m_nodeHotkeys[nodeId]  = chosen;
    m_keyToNode[chosen]    = nodeId;

    // Deck-A shortcut: bare key press.
    auto *scA = new QShortcut(QKeySequence(chosen), this);
    scA->setContext(Qt::ApplicationShortcut);
    connect(scA, &QShortcut::activated, this, [this, chosen]() {
        NodeId id = m_keyToNode.value(chosen, 0);
        if (id) onNodeAButtonClicked(id);
    });

    // Deck-B shortcut: Shift + key.
    auto *scB = new QShortcut(QKeySequence(Qt::SHIFT | chosen), this);
    scB->setContext(Qt::ApplicationShortcut);
    connect(scB, &QShortcut::activated, this, [this, chosen]() {
        NodeId id = m_keyToNode.value(chosen, 0);
        if (id) onNodeBButtonClicked(id);
    });

    m_nodeShortcuts[nodeId] = {scA, scB};

    // Show the badge on the card.
    const QString keyName = QKeySequence(chosen).toString();
    node->setHotkeyLabel(keyName);
}

void MainWindow::releaseHotkeyForNode(NodeId nodeId) {
    auto hit = m_nodeHotkeys.find(nodeId);
    if (hit == m_nodeHotkeys.end()) return;

    Qt::Key key = hit.value();
    m_nodeHotkeys.erase(hit);
    m_keyToNode.remove(key);

    auto sit = m_nodeShortcuts.find(nodeId);
    if (sit != m_nodeShortcuts.end()) {
        delete sit.value().deckA;
        delete sit.value().deckB;
        m_nodeShortcuts.erase(sit);
    }
    // The ClipCard widget is still alive here (removal happens after this call),
    // so clear its badge to avoid stale display if the card is somehow reused.
    if (ClipNodeModel *node = m_clipNodeEditor->nodeAt(nodeId))
        node->setHotkeyLabel({});
}

// ── Transition mode ───────────────────────────────────────────────────────────

void MainWindow::onTransitionModeChanged(int index) {
    using TM = VideoWidget::TransitionMode;
    static const TM modes[] = {
        TM::Crossfade, TM::Cut,
        TM::WipeLeft, TM::WipeRight, TM::WipeUp, TM::WipeDown,
        TM::SlideLeft, TM::SlideRight, TM::SlideUp, TM::SlideDown,
        TM::DipToBlack, TM::DipToWhite,
        TM::Additive, TM::CrossZoom, TM::SplitDoor, TM::SplitDoorVert,
        TM::VortexSpin, TM::SplitQuadrants
    };
    if (index >= 0 && index < 18)
        outputWindow->videoWidget()->setTransitionMode(modes[index]);
}

void MainWindow::onAutoTransitionClicked() {
    if (m_transitionAnimation) {
        m_transitionAnimation->stop();
        delete m_transitionAnimation;
        m_transitionAnimation = nullptr;
    }

    int currentVal = ui->crossfaderSlider->value();
    int targetVal = (currentVal <= 50) ? 100 : 0;
    if (currentVal == targetVal)
        return;

    double durationSec = m_durationSpin ? m_durationSpin->value() : 1.0;
    int durationMs = static_cast<int>(durationSec * 1000.0);

    m_transitionAnimation = new QVariantAnimation(this);
    m_transitionAnimation->setDuration(durationMs);
    m_transitionAnimation->setStartValue(currentVal);
    m_transitionAnimation->setEndValue(targetVal);
    m_transitionAnimation->setEasingCurve(QEasingCurve::InOutQuad);

    connect(m_transitionAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        ui->crossfaderSlider->setValue(value.toInt());
    });

    connect(m_transitionAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_transitionAnimation) {
            m_transitionAnimation->deleteLater();
            m_transitionAnimation = nullptr;
        }
    });

    m_transitionAnimation->start();
}

void MainWindow::onCutTransitionClicked() {
    if (m_transitionAnimation) {
        m_transitionAnimation->stop();
        delete m_transitionAnimation;
        m_transitionAnimation = nullptr;
    }

    int currentVal = ui->crossfaderSlider->value();
    int targetVal = (currentVal <= 50) ? 100 : 0;
    ui->crossfaderSlider->setValue(targetVal);
}


// ── Session save / load ───────────────────────────────────────────────────────

void MainWindow::onSaveSessionClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Session", QString(), "SwitchX Session (*.sxs);;All Files (*)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".sxs", Qt::CaseInsensitive))
        path += ".sxs";

    QJsonObject root;
    root["version"]            = 1;
    root["crossfader"]         = ui->crossfaderSlider->value();
    root["transitionMode"]     = m_transitionCombo ? m_transitionCombo->currentIndex() : 0;
    root["transitionDuration"] = m_durationSpin    ? m_durationSpin->value()           : 1.0;
    root["activeNodeA"]        = (qint64)m_aClipNodeId;
    root["activeNodeB"]        = (qint64)m_bClipNodeId;

    // Save hotkeys: nodeId → key code (int)
    QJsonArray hotkeys;
    for (auto it = m_nodeHotkeys.cbegin(); it != m_nodeHotkeys.cend(); ++it) {
        QJsonObject hk;
        hk["nodeId"] = (qint64)it.key();
        hk["key"]    = (int)it.value();
        hotkeys.append(hk);
    }
    root["hotkeys"] = hotkeys;

    root["graph"] = m_clipNodeEditor->saveState();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Save Session",
                             QString("Cannot write to file:\n%1").arg(path));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void MainWindow::onLoadSessionClicked() {
    QString path = QFileDialog::getOpenFileName(
        this, "Load Session", QString(), "SwitchX Session (*.sxs);;All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Load Session",
                             QString("Cannot open file:\n%1").arg(path));
        return;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (doc.isNull()) {
        QMessageBox::warning(this, "Load Session",
                             QString("Invalid session file:\n%1").arg(err.errorString()));
        return;
    }
    const QJsonObject root = doc.object();
    if (root["version"].toInt() != 1) {
        QMessageBox::warning(this, "Load Session",
                             "Unsupported session version.");
        return;
    }

    // Tear down current state ──────────────────────────────────────────────────
    clipManager.clear();
    outputWindow->videoWidget()->setSourceA(nullptr);
    outputWindow->videoWidget()->setSourceB(nullptr);
    outputWindow->videoWidget()->setNodeChainA({});
    outputWindow->videoWidget()->setNodeChainB({});
    m_aClipNodeId = 0;
    m_bClipNodeId = 0;

    // Block auto-hotkey assignment while we restore nodes.
    disconnect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,
               this, &MainWindow::assignHotkeyToNode);

    m_clipNodeEditor->restoreState(root["graph"].toObject());

    // Re-wire nodeAdded after restore.
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,
            this, &MainWindow::assignHotkeyToNode);

    m_stackWidget->setCurrentWidget(m_clipNodeEditor);

    // Re-generate thumbnails for every restored node ───────────────────────────
    for (ClipNodeModel *model : m_clipNodeEditor->allNodes()) {
        const SourceDescriptor &desc = model->sourceDescriptor();
        using Kind = SourceDescriptor::Kind;
        QPixmap thumb;
        switch (desc.kind) {
        case Kind::VideoFile:
        case Kind::Image:
            thumb = ThumbnailExtractor::extract(desc.path, 110, 65);
            break;
        case Kind::Slideshow: {
            QDir dir(desc.path);
            QStringList imgs = dir.entryList({"*.png","*.jpg","*.jpeg","*.bmp","*.webp"},
                                             QDir::Files, QDir::Name);
            if (!imgs.isEmpty())
                thumb = ThumbnailExtractor::extract(dir.absoluteFilePath(imgs.first()), 110, 65);
            if (thumb.isNull()) thumb = makeIconThumb("📁");
            break;
        }
        case Kind::Camera:  thumb = makeIconThumb("📷");  break;
        case Kind::Screen:  thumb = makeIconThumb("🖥");   break;
        case Kind::Window:  thumb = makeIconThumb("🪟");   break;
        case Kind::Canvas:
            thumb = makeCanvasThumb(
                QString("%1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight),
                desc.canvasFill, desc.color);
            break;
        case Kind::Shader:  thumb = makeShaderThumb(desc.shaderCode);           break;
        case Kind::Html:    thumb = makeHtmlThumb(desc.htmlContent, desc.path); break;
        }
        if (!thumb.isNull()) {
            if (desc.kind == Kind::VideoFile || desc.kind == Kind::Image)
                model->loadClip(desc.path, thumb);
            else
                model->loadSource(desc, thumb);
            // Re-apply settings that loadClip/loadSource would reset.
            model->applySettings(model->settings());
        }
    }

    // Restore hotkeys ─────────────────────────────────────────────────────────
    // Clear any stale mappings first.
    for (NodeId id : m_nodeHotkeys.keys()) releaseHotkeyForNode(id);

    const QJsonArray hotkeys = root["hotkeys"].toArray();
    for (const auto &hkVal : hotkeys) {
        const QJsonObject hk = hkVal.toObject();
        const NodeId nodeId  = (NodeId)hk["nodeId"].toInteger();
        const Qt::Key key    = (Qt::Key)hk["key"].toInt();

        ClipNodeModel *node = m_clipNodeEditor->nodeAt(nodeId);
        if (!node) continue;
        if (m_keyToNode.contains(key)) continue; // already taken

        m_nodeHotkeys[nodeId] = key;
        m_keyToNode[key]      = nodeId;

        auto *scA = new QShortcut(QKeySequence(key), this);
        scA->setContext(Qt::ApplicationShortcut);
        connect(scA, &QShortcut::activated, this, [this, key]() {
            NodeId id = m_keyToNode.value(key, 0);
            if (id) onNodeAButtonClicked(id);
        });

        auto *scB = new QShortcut(QKeySequence(Qt::SHIFT | key), this);
        scB->setContext(Qt::ApplicationShortcut);
        connect(scB, &QShortcut::activated, this, [this, key]() {
            NodeId id = m_keyToNode.value(key, 0);
            if (id) onNodeBButtonClicked(id);
        });

        m_nodeShortcuts[nodeId] = {scA, scB};
        node->setHotkeyLabel(QKeySequence(key).toString());
    }

    // Restore crossfader and transition settings ───────────────────────────────
    ui->crossfaderSlider->setValue(root["crossfader"].toInt(50));
    if (m_transitionCombo)
        m_transitionCombo->setCurrentIndex(root["transitionMode"].toInt(0));
    if (m_durationSpin)
        m_durationSpin->setValue(root["transitionDuration"].toDouble(1.0));

    // Restore active deck assignments ──────────────────────────────────────────
    const NodeId savedA = (NodeId)root["activeNodeA"].toInteger();
    const NodeId savedB = (NodeId)root["activeNodeB"].toInteger();
    if (savedA) onNodeAButtonClicked(savedA);
    if (savedB) onNodeBButtonClicked(savedB);
}

// ── Card management (obsolete methods removed - now handled by ClipNodeEditor) ──
