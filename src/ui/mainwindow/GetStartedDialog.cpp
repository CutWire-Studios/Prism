#include "ui/mainwindow/GetStartedDialog.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QSettings>
#include <QTimer>
#include <QPixmap>

#include "ui/common/Theme.h"

namespace {

constexpr auto kSettingsKey = "onboarding/getStartedShown";

} // namespace

GetStartedDialog::GetStartedDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Get Started"));
    setModal(true);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(10);

    auto *logo = new QLabel(this);
    const QPixmap logoPx(QStringLiteral(":/Prism_icon.png"));
    logo->setPixmap(logoPx.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);

    auto *title = new QLabel(tr("Welcome to CutWire Prism"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));

    auto *subtitle = new QLabel(
        tr("Build live media shows by connecting sources on the node canvas. "
           "Use the asset library on the left and Media menu to add clips and inputs."),
        this);
    subtitle->setWordWrap(true);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setProperty("role", "secondary");
    subtitle->setStyleSheet(QStringLiteral("font-size: 12px;"));

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background-color: %1; border: none;")
                               .arg(Theme::instance().tokens().stroke.name(QColor::HexArgb)));

    auto *quickStartLabel = new QLabel(tr("Quick Start"), this);
    quickStartLabel->setProperty("role", "secondary");
    quickStartLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold;"));

    const QString accent = Theme::instance().tokens().accent.name();

    auto *docsLink = new QLabel(this);
    docsLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    docsLink->setOpenExternalLinks(true);
    docsLink->setWordWrap(true);
    docsLink->setText(
        QStringLiteral("<a href=\"https://docs.cutwire.org/prism\" style=\"color: %2;\">%1</a>")
            .arg(tr("CutWire Prism documentation"), accent));

    auto *issuesLink = new QLabel(this);
    issuesLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    issuesLink->setOpenExternalLinks(true);
    issuesLink->setWordWrap(true);
    issuesLink->setText(
        QStringLiteral("<a href=\"https://github.com/CutWire-Studios/Prism/issues\" style=\"color: %2;\">%1</a>")
            .arg(tr("Report an issue on GitHub"), accent));

    auto *closeBtn = new QPushButton(tr("Get Started"), this);
    closeBtn->setDefault(true);
    closeBtn->setMinimumWidth(120);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(4);
    layout->addWidget(divider);
    layout->addWidget(quickStartLabel);
    layout->addWidget(docsLink);
    layout->addWidget(issuesLink);
    layout->addSpacing(8);
    layout->addWidget(closeBtn, 0, Qt::AlignHCenter);
}

bool GetStartedDialog::shouldShow()
{
    QSettings settings;
    return !settings.value(QLatin1String(kSettingsKey), false).toBool();
}

void GetStartedDialog::markShown()
{
    QSettings settings;
    settings.setValue(QLatin1String(kSettingsKey), true);
}

void GetStartedDialog::showIfNeeded(QWidget *parent)
{
    if (!parent || !shouldShow())
        return;

    QTimer::singleShot(0, parent, [parent]() {
        if (!shouldShow())
            return;
        GetStartedDialog dlg(parent);
        dlg.setWindowModality(Qt::ApplicationModal);
        if (dlg.exec() == QDialog::Accepted)
            markShown();
    });
}
