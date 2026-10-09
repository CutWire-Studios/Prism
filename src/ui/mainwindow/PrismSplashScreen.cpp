#include "ui/mainwindow/PrismSplashScreen.h"
#include "release.h"
#include "ui/common/Theme.h"
#include <QPainter>
#include <QPaintEvent>

PrismSplashScreen::PrismSplashScreen(const QPixmap &pixmap)
    : QSplashScreen(pixmap)
{
    // Default transparent canvas of size 600x360
    QPixmap defaultPixmap(600, 360);
    defaultPixmap.fill(Qt::transparent);
    setPixmap(defaultPixmap);

    m_logo.load(QStringLiteral(":/Prism_icon.png"));
}

void PrismSplashScreen::setProgress(int percentage, const QString &statusText) {
    m_progress = percentage;
    m_statusText = statusText;
    showMessage(statusText); // forces repaint
}

void PrismSplashScreen::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto &t = Theme::instance().tokens();

    // 1. Draw Background
    QRect rect = this->rect();
    Theme::instance().paintBackdrop(painter, rect);

    QPen borderPen(t.stroke, 1);
    painter.setPen(borderPen);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));

    // 2. Draw Logo
    if (!m_logo.isNull()) {
        painter.drawPixmap(40, 110, 110, 110, m_logo);
    }

    // 3. Draw Title
    painter.setPen(t.text);
    QFont titleFont = painter.font();
    titleFont.setFamily(QStringLiteral("Inter"));
    titleFont.setPointSize(28);
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setLetterSpacing(QFont::PercentageSpacing, 98);
    painter.setFont(titleFont);
    painter.drawText(180, 150, QStringLiteral("CutWire Prism"));

    // 4. Draw Subtitle
    painter.setPen(t.textSecondary);
    QFont subtitleFont = painter.font();
    subtitleFont.setPointSize(12);
    subtitleFont.setWeight(QFont::Normal);
    painter.setFont(subtitleFont);
    painter.drawText(180, 185, QStringLiteral("Live Media Trigger & Control"));

    // 5. Draw Version
    painter.setPen(t.textDisabled);
    QFont versionFont = painter.font();
    versionFont.setPointSize(10);
    painter.setFont(versionFont);
    painter.drawText(180, 210, QStringLiteral(PRISM_VERSION_STRING " (GPLv3)"));

    // 5b. Draw community/GitHub info
    painter.setPen(t.textSecondary);
    QFont infoFont = painter.font();
    infoFont.setPointSize(10);
    painter.setFont(infoFont);
    painter.drawText(180, 240, QStringLiteral("Presented by CutWire Studios as a free, open-source project."));

    painter.setPen(t.textSecondary);
    QFont urlFont = painter.font();
    urlFont.setPointSize(9);
    painter.setFont(urlFont);
    painter.drawText(180, 260, QStringLiteral("https://github.com/CutWire-Studios/Prism"));

    // 6. Draw Status Text
    painter.setPen(t.text);
    QFont statusFont = painter.font();
    statusFont.setPointSize(10);
    painter.setFont(statusFont);
    painter.drawText(40, 310, m_statusText);

    // 7. Draw sleek progress bar at the bottom
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.stroke); // Background track
    painter.drawRect(40, 325, 520, 6);

    int fillWidth = (520 * m_progress) / 100;
    if (fillWidth > 0) {
        painter.setBrush(t.text);
        painter.drawRect(40, 325, fillWidth, 6);
    }
}
