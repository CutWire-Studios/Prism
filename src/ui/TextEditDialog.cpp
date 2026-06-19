#include "ui/TextEditDialog.h"
#include "ui_TextEditDialog.h"

#include <QColorDialog>

namespace {

static const int kAlignments[] = {
    int(Qt::AlignLeft | Qt::AlignTop),
    int(Qt::AlignHCenter | Qt::AlignTop),
    int(Qt::AlignRight | Qt::AlignTop),
    int(Qt::AlignLeft | Qt::AlignVCenter),
    int(Qt::AlignCenter),
    int(Qt::AlignRight | Qt::AlignVCenter),
    int(Qt::AlignLeft | Qt::AlignBottom),
    int(Qt::AlignHCenter | Qt::AlignBottom),
    int(Qt::AlignRight | Qt::AlignBottom),
};
static const int kAlignmentCount = static_cast<int>(sizeof(kAlignments) / sizeof(kAlignments[0]));

} // namespace

TextEditDialog::TextEditDialog(const SourceDescriptor &initial, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TextEditDialog)
{
    ui->setupUi(this);

    ui->alignCombo->addItems({
        tr("Top left"), tr("Top center"), tr("Top right"),
        tr("Center left"), tr("Center"), tr("Center right"),
        tr("Bottom left"), tr("Bottom center"), tr("Bottom right"),
    });

    connect(ui->textColorBtn, &QPushButton::clicked, this, &TextEditDialog::onPickTextColor);
    connect(ui->bgColorBtn, &QPushButton::clicked, this, &TextEditDialog::onPickBgColor);
    connect(ui->bgTransparentCheck, &QCheckBox::toggled,
            this, &TextEditDialog::onBgTransparentToggled);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (ui->templateEdit->text().trimmed().isEmpty()) {
            ui->templateEdit->setFocus();
            return;
        }
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (initial.kind == SourceDescriptor::Kind::Text)
        setFromDescriptor(initial);
    else
        ui->templateEdit->setText(QStringLiteral("Hello"));

    updateColorButtons();
}

TextEditDialog::~TextEditDialog() {
    delete ui;
}

int TextEditDialog::alignmentToIndex(int textAlign) {
    for (int i = 0; i < kAlignmentCount; ++i) {
        if (kAlignments[i] == textAlign)
            return i;
    }
    return 4;
}

void TextEditDialog::setFromDescriptor(const SourceDescriptor &desc) {
    ui->templateEdit->setText(desc.textTemplate);
    ui->fontEdit->setText(desc.fontFamily.isEmpty() ? QStringLiteral("Sans Serif")
                                                    : desc.fontFamily);
    ui->sizeSpin->setValue(desc.fontSize > 0 ? desc.fontSize : 48);
    ui->alignCombo->setCurrentIndex(alignmentToIndex(desc.textAlign));
    m_textColor = desc.color.isValid() ? desc.color : Qt::white;
    m_bgColor = desc.textBgColor.isValid() ? desc.textBgColor : Qt::black;
    ui->bgTransparentCheck->setChecked(desc.textBgTransparent);
    ui->bgColorBtn->setEnabled(!desc.textBgTransparent);
}

SourceDescriptor TextEditDialog::resultDescriptor() const {
    const QString tmpl = ui->templateEdit->text().trimmed();

    SourceDescriptor desc;
    desc.kind              = SourceDescriptor::Kind::Text;
    desc.textTemplate      = tmpl;
    desc.fontFamily        = ui->fontEdit->text().trimmed().isEmpty()
        ? QStringLiteral("Sans Serif") : ui->fontEdit->text().trimmed();
    desc.fontSize          = ui->sizeSpin->value();
    desc.textAlign         = kAlignments[ui->alignCombo->currentIndex()];
    desc.color             = m_textColor;
    desc.textBgTransparent = ui->bgTransparentCheck->isChecked();
    desc.textBgColor       = m_bgColor;
    desc.canvasWidth       = 1280;
    desc.canvasHeight      = 720;
    desc.displayName       = tmpl.length() > 24 ? tmpl.left(21) + QStringLiteral("…") : tmpl;
    return desc;
}

void TextEditDialog::onPickTextColor() {
    const QColor c = QColorDialog::getColor(m_textColor, this, tr("Text Color"));
    if (c.isValid()) {
        m_textColor = c;
        updateColorButtons();
    }
}

void TextEditDialog::onPickBgColor() {
    const QColor c = QColorDialog::getColor(m_bgColor, this, tr("Background Color"));
    if (c.isValid()) {
        m_bgColor = c;
        updateColorButtons();
    }
}

void TextEditDialog::onBgTransparentToggled(bool transparent) {
    ui->bgColorBtn->setEnabled(!transparent);
}

void TextEditDialog::updateColorButtons() {
    auto styleColorBtn = [](QPushButton *btn, const QColor &c) {
        btn->setStyleSheet(QStringLiteral(
            "background-color:%1; color:%2; border:1px solid #555; border-radius:3px; padding:4px 10px;")
            .arg(c.name(QColor::HexArgb),
                 c.lightness() > 128 ? QStringLiteral("#111") : QStringLiteral("#eee")));
        btn->setText(c.name(QColor::HexArgb).toUpper());
    };
    styleColorBtn(ui->textColorBtn, m_textColor);
    styleColorBtn(ui->bgColorBtn, m_bgColor);
}
