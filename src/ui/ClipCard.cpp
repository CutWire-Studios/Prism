#include "ui/ClipCard.h"
#include "ui_ClipCard.h"
#include "ui/ClipEditDialog.h"
#include "ui/ShaderEditDialog.h"
#include "ui/HtmlEditDialog.h"
#include "core/ImageSource.h"
#include "core/ShaderSource.h"
#include "core/HtmlSource.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QCapturableWindow>
#include <QWindowCapture>
#include <QApplication>
#include <numeric>

// When the card is embedded in a QGraphicsProxyWidget (node editor), parenting a
// dialog to the card would embed the dialog inside the scene. In that case fall
// back to the active top-level window so dialogs appear as normal windows.
static QWidget *dialogParent(QWidget *w) {
    if (w->graphicsProxyWidget())
        return QApplication::activeWindow();
    return w;
}

ClipCard::ClipCard(int index, QWidget *parent)
    : QFrame(parent), m_index(index), ui(new Ui::ClipCard) {
    ui->setupUi(this);

    // Hotkey badge — floats over the top-left corner of the thumbnail area.
    // The thumbnail button is at (5, 5) in ClipCard's local coords (layout margins).
    m_hotkeyBadge = new QLabel(this);
    m_hotkeyBadge->setAlignment(Qt::AlignCenter);
    m_hotkeyBadge->setFixedSize(20, 15);
    m_hotkeyBadge->move(6, 6);
    m_hotkeyBadge->setStyleSheet(
        "QLabel { background-color: #152a30; color: #2adcf5; "
        "border: 1px solid #2a8fa0; border-radius: 3px; "
        "font-size: 8px; font-weight: bold; padding: 0 2px; }");
    m_hotkeyBadge->hide();
    m_hotkeyBadge->raise();

    connect(ui->thumbnailBtn, &QPushButton::clicked, this, [this]() {
        if (!m_clipPath.isEmpty()) emit triggered(m_index);
    });
    connect(ui->muteBtn,    &QPushButton::clicked, this, &ClipCard::onMuteClicked);
    connect(ui->repeatBtn,  &QPushButton::clicked, this, &ClipCard::onRepeatClicked);
    connect(ui->editBtn,    &QPushButton::clicked, this, &ClipCard::onEditClicked);
    connect(ui->aBtn,       &QPushButton::clicked, this, &ClipCard::onAButtonClicked);
    connect(ui->bBtn,       &QPushButton::clicked, this, &ClipCard::onBButtonClicked);
    connect(ui->removeBtn,  &QPushButton::clicked, this, &ClipCard::onRemoveClicked);

    clearClip();
}

ClipCard::~ClipCard() {
    delete ui;
}

void ClipCard::setHotkeyLabel(const QString &key) {
    if (key.isEmpty()) {
        m_hotkeyBadge->hide();
    } else {
        m_hotkeyBadge->setText(key);
        m_hotkeyBadge->setToolTip(
            QString("Hotkey: %1\nPress  %1       → Deck A\nPress  Shift+%1 → Deck B").arg(key));
        m_hotkeyBadge->show();
        m_hotkeyBadge->raise();
    }
}

int ClipCard::volume() const {
    return ui->volumeSlider->value();
}

void ClipCard::loadClip(const QString &path, const QPixmap &thumbnail) {
    m_clipPath = path;
    m_settings = ClipSettings::loadFor(path);

    // Build source descriptor so onAButtonClicked can create the right source.
    const bool isImage = ImageSource::isStaticImageFile(path);
    m_sourceDesc = {};
    m_sourceDesc.kind        = isImage ? SourceDescriptor::Kind::Image
                                       : SourceDescriptor::Kind::VideoFile;
    m_sourceDesc.path        = path;
    m_sourceDesc.displayName = QFileInfo(path).baseName();

    if (!thumbnail.isNull()) {
        ui->thumbnailBtn->setIcon(QIcon(thumbnail));
        ui->thumbnailBtn->setText("");
    } else {
        ui->thumbnailBtn->setIcon(QIcon());
        ui->thumbnailBtn->setText("No Preview");
    }

    QFontMetrics fm(ui->titleLabel->font());
    ui->titleLabel->setText(fm.elidedText(m_sourceDesc.displayName, Qt::ElideRight, 108));
    ui->titleLabel->setToolTip(m_sourceDesc.displayName);

    ui->repeatBtn->setEnabled(true);
    ui->editBtn->setEnabled(true);
    ui->muteBtn->setEnabled(true);
    ui->volumeSlider->setEnabled(true);
    ui->aBtn->setEnabled(true);
    ui->bBtn->setEnabled(true);
    setActive(false);
}

void ClipCard::loadSource(const SourceDescriptor &desc, const QPixmap &thumbnail) {
    m_clipPath.clear();
    m_sourceDesc = desc;
    m_settings   = {};

    if (!thumbnail.isNull()) {
        ui->thumbnailBtn->setIcon(QIcon(thumbnail));
        ui->thumbnailBtn->setText("");
    } else {
        ui->thumbnailBtn->setIcon(QIcon());
        ui->thumbnailBtn->setText(desc.displayName.left(6));
    }

    QFontMetrics fm(ui->titleLabel->font());
    ui->titleLabel->setText(fm.elidedText(desc.displayName, Qt::ElideRight, 108));
    ui->titleLabel->setToolTip(desc.displayName);

    // Repeat only makes sense for slideshows; edit available for all non-empty sources.
    ui->repeatBtn->setEnabled(desc.kind == SourceDescriptor::Kind::Slideshow);
    ui->editBtn->setEnabled(true);
    ui->muteBtn->setEnabled(false);
    ui->volumeSlider->setEnabled(false);
    ui->aBtn->setEnabled(true);
    ui->bBtn->setEnabled(true);
    ui->ovlBtn->setVisible(false);
    setActive(false);
}

void ClipCard::clearClip() {
    m_clipPath.clear();
    m_settings = {};
    ui->thumbnailBtn->setIcon(QIcon());
    ui->thumbnailBtn->setText("Empty");
    ui->titleLabel->setText("—");
    ui->titleLabel->setToolTip({});
    ui->repeatBtn->setEnabled(false);
    ui->editBtn->setEnabled(false);
    ui->muteBtn->setEnabled(false);
    ui->volumeSlider->setEnabled(false);
    ui->aBtn->setEnabled(false);
    ui->bBtn->setEnabled(false);
    ui->ovlBtn->setVisible(false);
    setActive(false);
    setASelected(false);
    setBSelected(false);
}

void ClipCard::setActive(bool active) {
    if (active) {
        setStyleSheet("ClipCard { background-color: #1a3d45; border: 2px solid #2a8fa0; border-radius: 8px; }");
    } else {
        setStyleSheet("ClipCard { background-color: #242528; border: 1px solid #1c1d1f; border-radius: 8px; }");
    }
}

void ClipCard::setASelected(bool selected) {
    m_aSelected = selected;
    if (selected) {
        ui->aBtn->setStyleSheet("QPushButton { background-color: #2a5c66; color: #FFFFFF; font-weight: bold; font-size: 9px; min-height: 0; height: 20px; border-radius: 4px; }");
    } else {
        ui->aBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px;");
    }
}

void ClipCard::setBSelected(bool selected) {
    m_bSelected = selected;
    if (selected) {
        ui->bBtn->setStyleSheet("QPushButton { background-color: #2a5c66; color: #FFFFFF; font-weight: bold; font-size: 9px; min-height: 0; height: 20px; border-radius: 4px; }");
    } else {
        ui->bBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px;");
    }
}

void ClipCard::onMuteClicked() {
    m_muted = !m_muted;
    ui->muteBtn->setText(m_muted ? "🔇" : "🔊");
}

void ClipCard::onRepeatClicked() {
    m_repeat = !m_repeat;
    ui->repeatBtn->setText(m_repeat ? "↺  On" : "↺  Off");
}

void ClipCard::onEditClicked() {
    using Kind = SourceDescriptor::Kind;

    QWidget *parent = dialogParent(this);

    switch (m_sourceDesc.kind) {

    case Kind::VideoFile: {
        if (m_clipPath.isEmpty()) return;
        ClipEditDialog dlg(m_clipPath, m_settings, parent);
        if (dlg.exec() == QDialog::Accepted) {
            m_settings = dlg.resultSettings();
            m_settings.saveFor(m_clipPath);
        }
        break;
    }

    case Kind::Image: {
        if (m_clipPath.isEmpty()) return;
        ClipEditDialog dlg(m_clipPath, m_settings, parent);
        dlg.hideTrimTab();
        if (dlg.exec() == QDialog::Accepted) {
            m_settings = dlg.resultSettings();
            m_settings.saveFor(m_clipPath);
        }
        break;
    }

    case Kind::Slideshow: {
        QDialog dlg(parent);
        dlg.setWindowTitle("Edit Slideshow");
        dlg.setMinimumWidth(400);

        auto *form = new QFormLayout;
        auto *folderEdit = new QLineEdit(m_sourceDesc.path, &dlg);
        auto *browseBtn  = new QPushButton("Browse…", &dlg);
        auto *folderRow  = new QHBoxLayout;
        folderRow->addWidget(folderEdit);
        folderRow->addWidget(browseBtn);

        auto *intervalSpin = new QSpinBox(&dlg);
        intervalSpin->setRange(1, 3600);
        intervalSpin->setSuffix(" s");
        intervalSpin->setValue(m_sourceDesc.slideshowIntervalMs / 1000);

        form->addRow("Folder:", folderRow);
        form->addRow("Seconds per slide:", intervalSpin);

        connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
            QString dir = QFileDialog::getExistingDirectory(&dlg, "Select Image Folder",
                                                            m_sourceDesc.path);
            if (!dir.isEmpty()) folderEdit->setText(dir);
        });

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        auto *layout = new QVBoxLayout(&dlg);
        layout->addLayout(form);
        layout->addWidget(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            m_sourceDesc.path               = folderEdit->text();
            m_sourceDesc.slideshowIntervalMs = intervalSpin->value() * 1000;
            emit sourceDescriptorChanged(m_index, m_sourceDesc);
        }
        break;
    }

    case Kind::Camera: {
        const auto cameras = QMediaDevices::videoInputs();
        if (cameras.isEmpty()) {
            QDialog info(parent);
            info.setWindowTitle("No Cameras");
            auto *lbl = new QLabel("No camera devices were found.", &info);
            auto *btn = new QDialogButtonBox(QDialogButtonBox::Ok, &info);
            connect(btn, &QDialogButtonBox::accepted, &info, &QDialog::accept);
            auto *lay = new QVBoxLayout(&info);
            lay->addWidget(lbl);
            lay->addWidget(btn);
            info.exec();
            break;
        }

        QDialog dlg(parent);
        dlg.setWindowTitle("Select Camera");
        dlg.setMinimumWidth(360);

        auto *combo = new QComboBox(&dlg);
        for (const auto &cam : cameras)
            combo->addItem(cam.description());
        combo->setCurrentIndex(qBound(0, m_sourceDesc.cameraIndex, cameras.size() - 1));

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        auto *layout = new QVBoxLayout(&dlg);
        layout->addWidget(new QLabel("Choose camera device:", &dlg));
        layout->addWidget(combo);
        layout->addWidget(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            m_sourceDesc.cameraIndex  = combo->currentIndex();
            m_sourceDesc.displayName  = cameras[combo->currentIndex()].description();
            QFontMetrics fm(ui->titleLabel->font());
            ui->titleLabel->setText(fm.elidedText(m_sourceDesc.displayName, Qt::ElideRight, 108));
            ui->titleLabel->setToolTip(m_sourceDesc.displayName);
            emit sourceDescriptorChanged(m_index, m_sourceDesc);
        }
        break;
    }

    case Kind::Screen: {
        const auto screens = QGuiApplication::screens();
        QDialog dlg(parent);
        dlg.setWindowTitle("Select Screen");
        dlg.setMinimumWidth(360);

        auto *combo = new QComboBox(&dlg);
        for (int i = 0; i < screens.size(); ++i)
            combo->addItem(QString("Screen %1 — %2").arg(i + 1).arg(screens[i]->name()));
        combo->setCurrentIndex(qBound(0, m_sourceDesc.screenIndex, screens.size() - 1));

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        auto *layout = new QVBoxLayout(&dlg);
        layout->addWidget(new QLabel("Choose screen to capture:", &dlg));
        layout->addWidget(combo);
        layout->addWidget(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            m_sourceDesc.screenIndex  = combo->currentIndex();
            m_sourceDesc.displayName  = QString("Screen %1").arg(combo->currentIndex() + 1);
            QFontMetrics fm(ui->titleLabel->font());
            ui->titleLabel->setText(fm.elidedText(m_sourceDesc.displayName, Qt::ElideRight, 108));
            ui->titleLabel->setToolTip(m_sourceDesc.displayName);
            emit sourceDescriptorChanged(m_index, m_sourceDesc);
        }
        break;
    }

    case Kind::Window: {
        const auto windows = QWindowCapture::capturableWindows();
        if (windows.isEmpty()) {
            // Nothing to choose from on Wayland — just keep existing
            break;
        }

        QDialog dlg(parent);
        dlg.setWindowTitle("Select Window / Tab");
        dlg.setMinimumWidth(400);

        auto *combo = new QComboBox(&dlg);
        for (const auto &w : windows)
            combo->addItem(w.description().isEmpty() ? "(unnamed)" : w.description());
        combo->setCurrentIndex(qBound(0, m_sourceDesc.windowIndex, windows.size() - 1));

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        auto *layout = new QVBoxLayout(&dlg);
        layout->addWidget(new QLabel("Choose window to capture:", &dlg));
        layout->addWidget(combo);
        layout->addWidget(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            m_sourceDesc.windowIndex  = combo->currentIndex();
            m_sourceDesc.displayName  = combo->currentText();
            QFontMetrics fm(ui->titleLabel->font());
            ui->titleLabel->setText(fm.elidedText(m_sourceDesc.displayName, Qt::ElideRight, 108));
            ui->titleLabel->setToolTip(m_sourceDesc.displayName);
            emit sourceDescriptorChanged(m_index, m_sourceDesc);
        }
        break;
    }

    case Kind::Canvas: {
        QDialog dlg(parent);
        dlg.setWindowTitle("Edit Canvas");
        dlg.setMinimumWidth(360);

        auto *widthSpin = new QSpinBox(&dlg);
        widthSpin->setRange(16, 16384);
        widthSpin->setValue(m_sourceDesc.canvasWidth);
        auto *heightSpin = new QSpinBox(&dlg);
        heightSpin->setRange(16, 16384);
        heightSpin->setValue(m_sourceDesc.canvasHeight);
        auto *fillCombo = new QComboBox(&dlg);
        fillCombo->addItems({"Checkered", "Transparent", "Color"});
        const int fillIndex = m_sourceDesc.canvasFill == SourceDescriptor::CanvasFill::Transparent ? 1
                            : m_sourceDesc.canvasFill == SourceDescriptor::CanvasFill::Color ? 2 : 0;
        fillCombo->setCurrentIndex(fillIndex);
        auto *colorBtn = new QPushButton("Pick…", &dlg);
        colorBtn->setEnabled(fillIndex == 2);
        QColor chosenColor = m_sourceDesc.color;
        connect(fillCombo, &QComboBox::currentIndexChanged, &dlg, [=](int idx) {
            colorBtn->setEnabled(idx == 2);
        });
        connect(colorBtn, &QPushButton::clicked, &dlg, [&]() {
            QColor c = QColorDialog::getColor(chosenColor, &dlg, "Canvas Color");
            if (c.isValid()) chosenColor = c;
        });

        auto *form = new QFormLayout;
        form->addRow("Width:", widthSpin);
        form->addRow("Height:", heightSpin);
        form->addRow("Fill:", fillCombo);
        form->addRow("Color:", colorBtn);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        auto *layout = new QVBoxLayout(&dlg);
        layout->addLayout(form);
        layout->addWidget(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            m_sourceDesc.canvasWidth = widthSpin->value();
            m_sourceDesc.canvasHeight = heightSpin->value();
            m_sourceDesc.canvasFill = fillCombo->currentIndex() == 1 ? SourceDescriptor::CanvasFill::Transparent
                                    : fillCombo->currentIndex() == 2 ? SourceDescriptor::CanvasFill::Color
                                                                      : SourceDescriptor::CanvasFill::Checkered;
            if (m_sourceDesc.canvasFill == SourceDescriptor::CanvasFill::Color)
                m_sourceDesc.color = chosenColor;
            const int g = std::gcd(m_sourceDesc.canvasWidth, m_sourceDesc.canvasHeight);
            QString fillLabel = m_sourceDesc.canvasFill == SourceDescriptor::CanvasFill::Transparent
                              ? "Transparent"
                              : m_sourceDesc.canvasFill == SourceDescriptor::CanvasFill::Color
                              ? m_sourceDesc.color.name().toUpper()
                              : "Checkered";
            m_sourceDesc.displayName = QString("Canvas %1:%2 (%3)")
                                           .arg(m_sourceDesc.canvasWidth / g)
                                           .arg(m_sourceDesc.canvasHeight / g)
                                           .arg(fillLabel);

            QFontMetrics fm(ui->titleLabel->font());
            ui->titleLabel->setText(fm.elidedText(m_sourceDesc.displayName, Qt::ElideRight, 108));
            ui->titleLabel->setToolTip(m_sourceDesc.displayName);

            QPixmap px(110, 65);
            if (m_sourceDesc.canvasFill == SourceDescriptor::CanvasFill::Color) {
                px.fill(m_sourceDesc.color);
            } else if (m_sourceDesc.canvasFill == SourceDescriptor::CanvasFill::Checkered) {
                px.fill(QColor("#1c1d1f"));
                QPainter p(&px);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QColor("#8b93a1"));
                p.setBrush(Qt::NoBrush);
                p.drawRect(8, 8, 94, 49);
                p.setPen(QColor("#c8ccd4"));
                p.drawText(px.rect(), Qt::AlignCenter, "CHK");
            } else {
                px.fill(QColor("#1c1d1f"));
                QPainter p(&px);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QColor("#8b93a1"));
                p.setBrush(Qt::NoBrush);
                p.drawRect(8, 8, 94, 49);
                p.setPen(QColor("#c8ccd4"));
                p.drawText(px.rect(), Qt::AlignCenter, "TR");
            }
            ui->thumbnailBtn->setIcon(QIcon(px));
            emit sourceDescriptorChanged(m_index, m_sourceDesc);
        }
        break;
    }

    case Kind::Shader: {
        ShaderEditDialog dlg(m_sourceDesc.shaderCode, parent);
        if (dlg.exec() == QDialog::Accepted) {
            QString newCode = dlg.resultCode().trimmed();
            if (!newCode.isEmpty()) {
                m_sourceDesc.shaderCode = newCode;
                ShaderSource src(newCode, QSize(110, 65));
                if (src.nextFrame() && src.isReady()) {
                    const uint8_t *data = src.frameData();
                    QImage img(data, 110, 65, 110 * 3, QImage::Format_RGB888);
                    ui->thumbnailBtn->setIcon(QIcon(QPixmap::fromImage(img.copy())));
                }
                emit sourceDescriptorChanged(m_index, m_sourceDesc);
            }
        }
        break;
    }

    case Kind::Html: {
        HtmlEditDialog dlg(m_sourceDesc.htmlContent, this);
        if (dlg.exec() == QDialog::Accepted) {
            QString filePath = dlg.resultFilePath();
            QString html     = dlg.resultHtml().trimmed();
            if (!filePath.isEmpty() || !html.isEmpty()) {
                m_sourceDesc.path        = filePath;
                m_sourceDesc.htmlContent = html;
                if (!filePath.isEmpty())
                    m_sourceDesc.displayName = QFileInfo(filePath).fileName();
                emit sourceDescriptorChanged(m_index, m_sourceDesc);
            }
        }
        break;
    }

    } // switch
}

void ClipCard::onRemoveClicked() {
    emit removeRequested(m_index);
}

void ClipCard::onAButtonClicked() {
    if (!hasSource()) return;
    emit aButtonClicked(m_index);
}

void ClipCard::onBButtonClicked() {
    if (!hasSource()) return;
    emit bButtonClicked(m_index);
}
