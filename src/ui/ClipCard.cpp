#include "ClipCard.h"
#include "ClipEditDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QFontMetrics>

ClipCard::ClipCard(int index, QWidget *parent)
    : QFrame(parent), m_index(index) {
    setFixedSize(122, 176);
    setFrameShape(QFrame::StyledPanel);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(3);

    thumbnailBtn = new QPushButton(this);
    thumbnailBtn->setFixedSize(110, 65);
    thumbnailBtn->setIconSize(QSize(110, 65));
    thumbnailBtn->setFlat(true);
    thumbnailBtn->setStyleSheet(
        "QPushButton { background-color: #18191b; border: 1px solid #2a2c30;"
        " border-radius: 4px; font-size: 9px; color: #555; }"
        "QPushButton:hover { border-color: #2a8fa0; }"
        "QPushButton:pressed { background-color: #1a3d45; }");
    connect(thumbnailBtn, &QPushButton::clicked, this, [this]() {
        if (!m_clipPath.isEmpty()) emit triggered(m_index);
    });
    layout->addWidget(thumbnailBtn);

    titleLabel = new QLabel("—", this);
    titleLabel->setFixedWidth(110);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 9px; color: #aaa; background: transparent;");
    layout->addWidget(titleLabel);

    QWidget *volRow = new QWidget(this);
    QHBoxLayout *volLayout = new QHBoxLayout(volRow);
    volLayout->setContentsMargins(0, 0, 0, 0);
    volLayout->setSpacing(3);

    muteBtn = new QPushButton("🔊", this);
    muteBtn->setFixedSize(24, 20);
    muteBtn->setStyleSheet("font-size: 11px; padding: 0; min-height: 0; height: 20px;");
    connect(muteBtn, &QPushButton::clicked, this, &ClipCard::onMuteClicked);

    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);
    volumeSlider->setFixedHeight(20);

    volLayout->addWidget(muteBtn);
    volLayout->addWidget(volumeSlider, 1);
    layout->addWidget(volRow);

    repeatBtn = new QPushButton("↺  Off", this);
    repeatBtn->setFixedHeight(20);
    repeatBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px; text-align: left; padding-left: 6px;");
    connect(repeatBtn, &QPushButton::clicked, this, &ClipCard::onRepeatClicked);
    layout->addWidget(repeatBtn);

    editBtn = new QPushButton("Edit", this);
    editBtn->setFixedHeight(20);
    editBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px;");
    connect(editBtn, &QPushButton::clicked, this, &ClipCard::onEditClicked);
    layout->addWidget(editBtn);

    QWidget *abRow = new QWidget(this);
    QHBoxLayout *abLayout = new QHBoxLayout(abRow);
    abLayout->setContentsMargins(0, 0, 0, 0);
    abLayout->setSpacing(3);

    aBtn = new QPushButton("A", this);
    aBtn->setFixedHeight(20);
    aBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px;");
    connect(aBtn, &QPushButton::clicked, this, &ClipCard::onAButtonClicked);

    bBtn = new QPushButton("B", this);
    bBtn->setFixedHeight(20);
    bBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px;");
    connect(bBtn, &QPushButton::clicked, this, &ClipCard::onBButtonClicked);

    abLayout->addWidget(aBtn, 1);
    abLayout->addWidget(bBtn, 1);
    layout->addWidget(abRow);

    clearClip();
}

void ClipCard::loadClip(const QString &path, const QPixmap &thumbnail) {
    m_clipPath = path;
    m_startTime = 0.0;
    m_endTime = -1.0;

    if (!thumbnail.isNull()) {
        thumbnailBtn->setIcon(QIcon(thumbnail));
        thumbnailBtn->setText("");
    } else {
        thumbnailBtn->setIcon(QIcon());
        thumbnailBtn->setText("No Preview");
    }

    QFileInfo info(path);
    QString name = info.baseName();
    QFontMetrics fm(titleLabel->font());
    titleLabel->setText(fm.elidedText(name, Qt::ElideRight, 108));
    titleLabel->setToolTip(name);

    repeatBtn->setEnabled(true);
    editBtn->setEnabled(true);
    muteBtn->setEnabled(true);
    volumeSlider->setEnabled(true);
    aBtn->setEnabled(true);
    bBtn->setEnabled(true);
    setActive(false);
}

void ClipCard::clearClip() {
    m_clipPath.clear();
    m_startTime = 0.0;
    m_endTime = -1.0;
    thumbnailBtn->setIcon(QIcon());
    thumbnailBtn->setText("Empty");
    titleLabel->setText("—");
    titleLabel->setToolTip({});
    repeatBtn->setEnabled(false);
    editBtn->setEnabled(false);
    muteBtn->setEnabled(false);
    volumeSlider->setEnabled(false);
    aBtn->setEnabled(false);
    bBtn->setEnabled(false);
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
        aBtn->setStyleSheet("QPushButton { background-color: #2a5c66; color: #FFFFFF; font-weight: bold; font-size: 9px; min-height: 0; height: 20px; border-radius: 4px; }");
    } else {
        aBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px;");
    }
}

void ClipCard::setBSelected(bool selected) {
    m_bSelected = selected;
    if (selected) {
        bBtn->setStyleSheet("QPushButton { background-color: #2a5c66; color: #FFFFFF; font-weight: bold; font-size: 9px; min-height: 0; height: 20px; border-radius: 4px; }");
    } else {
        bBtn->setStyleSheet("font-size: 9px; min-height: 0; height: 20px;");
    }
}

void ClipCard::onMuteClicked() {
    m_muted = !m_muted;
    muteBtn->setText(m_muted ? "🔇" : "🔊");
}

void ClipCard::onRepeatClicked() {
    m_repeat = !m_repeat;
    repeatBtn->setText(m_repeat ? "↺  On" : "↺  Off");
}

void ClipCard::onEditClicked() {
    if (m_clipPath.isEmpty()) return;
    ClipEditDialog dlg(m_clipPath, m_startTime, m_endTime, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_startTime = dlg.startTime();
        m_endTime = dlg.endTime();
    }
}

void ClipCard::onAButtonClicked() {
    if (m_clipPath.isEmpty()) return;
    emit aButtonClicked(m_index);
}

void ClipCard::onBButtonClicked() {
    if (m_clipPath.isEmpty()) return;
    emit bButtonClicked(m_index);
}
