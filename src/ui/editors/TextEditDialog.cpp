#include "ui/editors/TextEditDialog.h"
#include "ui_TextEditDialog.h"

#include "core/sources/TextSource.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDrag>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>

namespace {

// Accent shared with the ScriptOut/DataIn port color in the node editor, so
// variable blocks visually match the wire that feeds them.
const QColor kVarAccent(0x70, 0xc0, 0xa8);
const QColor kUnknownAccent(0xe0, 0xa0, 0x50);
const QColor kNeutralAccent(0x6e, 0xa8, 0xd8);

QIcon makeHAlignIcon(Qt::Alignment align) {
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xd0, 0xd4, 0xda));
    static const int widths[4] = { 14, 9, 12, 7 };
    for (int i = 0; i < 4; ++i) {
        int w = widths[i];
        int x;
        if (align & Qt::AlignJustify) { w = 16; x = 2; }
        else if (align & Qt::AlignRight)   x = 18 - w;
        else if (align & Qt::AlignHCenter) x = (20 - w) / 2;
        else                               x = 2;
        p.drawRect(x, 3 + i * 4, w, 2);
    }
    return QIcon(pm);
}

QIcon makeVAlignIcon(Qt::Alignment align) {
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xd0, 0xd4, 0xda));
    int top;
    if (align & Qt::AlignBottom)       top = 11;
    else if (align & Qt::AlignVCenter) top = 7;
    else                               top = 3;
    static const int widths[2] = { 14, 10 };
    for (int i = 0; i < 2; ++i)
        p.drawRect((20 - widths[i]) / 2, top + i * 4, widths[i], 2);
    return QIcon(pm);
}

QString displayValue(const QJsonValue &v) {
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true")
                                               : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        const qint64 i = static_cast<qint64>(d);
        if (static_cast<double>(i) == d)
            return QString::number(i);
        return QString::number(d);
    }
    case QJsonValue::Array:
        return QString::fromUtf8(
            QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(
            QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    default:
        return QStringLiteral("—");
    }
}

// Heuristic scan of Lua source for `return { key = …, … }` table keys, so
// variables can be offered before the script has produced any output.
QStringList luaReturnTableKeys(const QString &code) {
    QStringList keys;
    static const QRegularExpression returnRx(QStringLiteral("\\breturn\\s*\\{"));
    static const QRegularExpression identRx(QStringLiteral("[a-zA-Z_][a-zA-Z0-9_]*"));

    QRegularExpressionMatchIterator blocks = returnRx.globalMatch(code);
    while (blocks.hasNext()) {
        int i = blocks.next().capturedEnd();
        int depth = 1;
        bool expectKey = true;
        while (i < code.size() && depth > 0) {
            const QChar c = code.at(i);
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                const QChar quote = c;
                ++i;
                while (i < code.size() && code.at(i) != quote) {
                    if (code.at(i) == QLatin1Char('\\')) ++i;
                    ++i;
                }
                ++i;
                expectKey = false;
                continue;
            }
            if (c == QLatin1Char('-') && i + 1 < code.size()
                && code.at(i + 1) == QLatin1Char('-')) {
                while (i < code.size() && code.at(i) != QLatin1Char('\n')) ++i;
                continue;
            }
            if (c == QLatin1Char('{')) { ++depth; ++i; continue; }
            if (c == QLatin1Char('}')) { --depth; ++i; continue; }
            if (depth == 1) {
                if (c == QLatin1Char(',') || c == QLatin1Char(';')) {
                    expectKey = true;
                    ++i;
                    continue;
                }
                if (expectKey && (c.isLetter() || c == QLatin1Char('_'))) {
                    const QRegularExpressionMatch m = identRx.match(
                        code, i, QRegularExpression::NormalMatch,
                        QRegularExpression::AnchorAtOffsetMatchOption);
                    int j = m.capturedEnd();
                    while (j < code.size() && code.at(j).isSpace()) ++j;
                    if (j < code.size() && code.at(j) == QLatin1Char('=')
                        && (j + 1 >= code.size() || code.at(j + 1) != QLatin1Char('='))) {
                        keys << m.captured();
                        i = j + 1;
                        expectKey = false;
                        continue;
                    }
                    expectKey = false;
                    i = m.capturedEnd();
                    continue;
                }
                if (!c.isSpace())
                    expectKey = false;
            }
            ++i;
        }
    }
    keys.removeDuplicates();
    return keys;
}

QPixmap composePreview(const QImage &frame, const QSize &viewport) {
    const QSize inner = viewport - QSize(12, 12);
    if (frame.isNull() || inner.width() < 16 || inner.height() < 16)
        return QPixmap();

    QSize target = frame.size();
    target.scale(inner, Qt::KeepAspectRatio);
    QPixmap pm(target);
    QPainter p(&pm);
    const int cell = 10;
    for (int y = 0; y < target.height(); y += cell)
        for (int x = 0; x < target.width(); x += cell)
            p.fillRect(x, y, cell, cell,
                       ((x / cell + y / cell) % 2) ? QColor(0x1a, 0x1a, 0x1a)
                                                   : QColor(0x24, 0x24, 0x24));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(QPoint(0, 0), target), frame);
    p.setPen(QColor(0x3a, 0x3a, 0x3a));
    p.setBrush(Qt::NoBrush);
    p.drawRect(0, 0, target.width() - 1, target.height() - 1);
    return pm;
}

// A draggable/clickable block representing one script variable.
class VariableChip : public QFrame {
public:
    VariableChip(const QString &name, QWidget *parent)
        : QFrame(parent), m_name(name)
    {
        setObjectName(QStringLiteral("varChip"));
        setCursor(Qt::OpenHandCursor);
        setStyleSheet(QStringLiteral(
            "QFrame#varChip { background:#16221e; border:1px solid #2d3e37; border-radius:9px; }"
            "QFrame#varChip:hover { border-color:#70c0a8; }"));

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(9, 4, 9, 4);
        lay->setSpacing(6);

        auto *nameLabel = new QLabel(QStringLiteral("{%1}").arg(name), this);
        nameLabel->setStyleSheet(QStringLiteral("color:#70c0a8; font-weight:600;"));
        lay->addWidget(nameLabel);

        m_valueLabel = new QLabel(QStringLiteral("—"), this);
        m_valueLabel->setStyleSheet(QStringLiteral("color:#8b93a3; font-size:11px;"));
        lay->addWidget(m_valueLabel);
    }

    void setValue(const QString &value) {
        const QString v = value.isEmpty() ? QStringLiteral("—") : value;
        m_valueLabel->setText(m_valueLabel->fontMetrics().elidedText(v, Qt::ElideRight, 110));
        setToolTip(tr("Click or drag into the template to insert {%1}\nCurrent value: %2")
                       .arg(m_name, v));
    }

    QString name() const { return m_name; }

    std::function<void(const QString &)> onInsert;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            m_pressPos = e->pos();
            m_dragStarted = false;
        }
        QFrame::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton) || m_dragStarted)
            return;
        if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;
        m_dragStarted = true;
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("{%1}").arg(m_name));
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        drag->exec(Qt::CopyAction);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && !m_dragStarted
            && rect().contains(e->pos()) && onInsert)
            onInsert(QStringLiteral("{%1}").arg(m_name));
        QFrame::mouseReleaseEvent(e);
    }

private:
    QString m_name;
    QLabel *m_valueLabel;
    QPoint m_pressPos;
    bool m_dragStarted = false;
};

} // namespace

// Colors {placeholder} tokens in the template: green when the connected script
// provides the variable, amber when it doesn't, blue when no script is wired.
class TextEditDialog::Highlighter : public QSyntaxHighlighter {
public:
    explicit Highlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {}

    void setContext(const QSet<QString> &known, bool scriptConnected) {
        m_known = known;
        m_connected = scriptConnected;
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override {
        static const QRegularExpression tokenRx(
            QStringLiteral("\\{([a-zA-Z_][a-zA-Z0-9_]*)\\}"));
        QRegularExpressionMatchIterator it = tokenRx.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QTextCharFormat fmt;
            fmt.setFontWeight(QFont::DemiBold);
            if (!m_connected)
                fmt.setForeground(kNeutralAccent);
            else if (m_known.contains(m.captured(1)))
                fmt.setForeground(kVarAccent);
            else
                fmt.setForeground(kUnknownAccent);
            setFormat(m.capturedStart(), m.capturedLength(), fmt);
        }
    }

private:
    QSet<QString> m_known;
    bool m_connected = false;
};

TextEditDialog::TextEditDialog(const SourceDescriptor &initial, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TextEditDialog)
{
    ui->setupUi(this);

    ui->fillModeCombo->addItems({ tr("Solid color"), tr("Linear gradient") });
    ui->gradientDirCombo->addItems({
        tr("Vertical ↓"), tr("Horizontal →"), tr("Diagonal ↘"), tr("Diagonal ↗"),
    });

    QFont f = ui->boldBtn->font();
    f.setBold(true);
    ui->boldBtn->setFont(f);
    f = ui->italicBtn->font();
    f.setItalic(true);
    ui->italicBtn->setFont(f);
    f = ui->underlineBtn->font();
    f.setUnderline(true);
    ui->underlineBtn->setFont(f);

    ui->alignLeftBtn->setIcon(makeHAlignIcon(Qt::AlignLeft));
    ui->alignHCenterBtn->setIcon(makeHAlignIcon(Qt::AlignHCenter));
    ui->alignRightBtn->setIcon(makeHAlignIcon(Qt::AlignRight));
    ui->alignJustifyBtn->setIcon(makeHAlignIcon(Qt::AlignJustify));
    ui->alignTopBtn->setIcon(makeVAlignIcon(Qt::AlignTop));
    ui->alignVCenterBtn->setIcon(makeVAlignIcon(Qt::AlignVCenter));
    ui->alignBottomBtn->setIcon(makeVAlignIcon(Qt::AlignBottom));

    m_hAlignGroup = new QButtonGroup(this);
    m_hAlignGroup->addButton(ui->alignLeftBtn, int(Qt::AlignLeft));
    m_hAlignGroup->addButton(ui->alignHCenterBtn, int(Qt::AlignHCenter));
    m_hAlignGroup->addButton(ui->alignRightBtn, int(Qt::AlignRight));
    m_hAlignGroup->addButton(ui->alignJustifyBtn, int(Qt::AlignJustify));
    m_vAlignGroup = new QButtonGroup(this);
    m_vAlignGroup->addButton(ui->alignTopBtn, int(Qt::AlignTop));
    m_vAlignGroup->addButton(ui->alignVCenterBtn, int(Qt::AlignVCenter));
    m_vAlignGroup->addButton(ui->alignBottomBtn, int(Qt::AlignBottom));
    ui->alignHCenterBtn->setChecked(true);
    ui->alignVCenterBtn->setChecked(true);

    ui->runScriptBtn->hide();
    ui->chipsScroll->hide();

    m_highlighter = new Highlighter(ui->templateEdit->document());

    connect(ui->templateEdit, &QPlainTextEdit::textChanged,
            this, &TextEditDialog::updatePreview);
    connect(ui->fontCombo, &QFontComboBox::currentFontChanged,
            this, [this](const QFont &) { updatePreview(); });
    for (QSpinBox *spin : { ui->sizeSpin, ui->spacingSpin, ui->lineHeightSpin,
                            ui->outlineSpin, ui->shadowXSpin, ui->shadowYSpin })
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &TextEditDialog::updatePreview);
    for (QToolButton *btn : { ui->boldBtn, ui->italicBtn, ui->underlineBtn })
        connect(btn, &QToolButton::toggled, this, &TextEditDialog::updatePreview);
    connect(m_hAlignGroup, &QButtonGroup::idClicked,
            this, [this](int) { updatePreview(); });
    connect(m_vAlignGroup, &QButtonGroup::idClicked,
            this, [this](int) { updatePreview(); });
    connect(ui->fillModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateFillControls();
                updatePreview();
            });
    connect(ui->gradientDirCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updatePreview(); });
    connect(ui->bgTransparentCheck, &QCheckBox::toggled, this, [this](bool transparent) {
        ui->bgColorBtn->setEnabled(!transparent);
        updatePreview();
    });

    connect(ui->textColorBtn, &QPushButton::clicked, this,
            [this]() { pickColor(m_textColor, tr("Text Color")); });
    connect(ui->textColor2Btn, &QPushButton::clicked, this,
            [this]() { pickColor(m_textColor2, tr("Gradient End Color")); });
    connect(ui->outlineColorBtn, &QPushButton::clicked, this,
            [this]() { pickColor(m_outlineColor, tr("Outline Color")); });
    connect(ui->shadowColorBtn, &QPushButton::clicked, this,
            [this]() { pickColor(m_shadowColor, tr("Shadow Color")); });
    connect(ui->bgColorBtn, &QPushButton::clicked, this,
            [this]() { pickColor(m_bgColor, tr("Background Color")); });

    connect(ui->runScriptBtn, &QToolButton::clicked, this, [this]() {
        if (m_binding.requestRun)
            m_binding.requestRun();
    });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &TextEditDialog::tryAccept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *acceptShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(acceptShortcut, &QShortcut::activated, this, &TextEditDialog::tryAccept);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setToolTip(tr("Ctrl+Enter"));

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout, this, &TextEditDialog::onPollScriptOutput);

    if (initial.kind == SourceDescriptor::Kind::Text)
        setFromDescriptor(initial);
    else
        ui->templateEdit->setPlainText(QStringLiteral("Hello"));

    updateColorButtons();
    updateFillControls();
    ui->bgColorBtn->setEnabled(!ui->bgTransparentCheck->isChecked());
}

TextEditDialog::~TextEditDialog() {
    delete ui;
}

void TextEditDialog::setScriptBinding(const ScriptBinding &binding) {
    m_binding = binding;
    if (!m_binding.connected()) {
        m_highlighter->setContext({}, false);
        return;
    }

    m_declaredVars = luaReturnTableKeys(m_binding.code);
    {
        QMutexLocker lock(&m_binding.output->mutex);
        const QJsonDocument doc = QJsonDocument::fromJson(m_binding.output->json.toUtf8());
        if (doc.isObject())
            m_vars = doc.object();
    }
    m_lastSeenVersion = m_binding.output->version.load(std::memory_order_acquire);

    ui->runScriptBtn->setVisible(bool(m_binding.requestRun));
    rebuildVariableChips();
    m_pollTimer->start();
    updatePreview();
}

QStringList TextEditDialog::knownVariableNames() const {
    QStringList names = m_declaredVars;
    QStringList jsonKeys = m_vars.keys();
    for (const QString &key : jsonKeys) {
        if (!names.contains(key))
            names << key;
    }
    return names;
}

void TextEditDialog::rebuildVariableChips() {
    const QStringList names = knownVariableNames();
    m_chipKeys = names;

    qDeleteAll(m_chipWidgets);
    m_chipWidgets.clear();

    QSet<QString> known(names.cbegin(), names.cend());
    m_highlighter->setContext(known, true);

    if (names.isEmpty()) {
        ui->chipsScroll->hide();
        ui->varsHint->setText(tr("The connected script hasn't produced any variables yet. "
                                 "Press \"Run script\" or wait for its next run."));
        ui->varsHint->show();
        return;
    }

    ui->varsHint->hide();
    auto *lay = static_cast<QHBoxLayout *>(ui->chipsHost->layout());
    if (!m_chipStretchAdded) {
        lay->addStretch(1);
        m_chipStretchAdded = true;
    }
    for (const QString &name : names) {
        auto *chip = new VariableChip(name, ui->chipsHost);
        chip->onInsert = [this](const QString &token) { insertToken(token); };
        lay->insertWidget(lay->count() - 1, chip);
        m_chipWidgets << chip;
        chip->setValue(displayValue(m_vars.value(name)));
    }
    ui->chipsScroll->show();
}

void TextEditDialog::insertToken(const QString &token) {
    ui->templateEdit->textCursor().insertText(token);
    ui->templateEdit->setFocus();
}

void TextEditDialog::onPollScriptOutput() {
    if (!m_binding.connected())
        return;
    const uint ver = m_binding.output->version.load(std::memory_order_acquire);
    if (ver == m_lastSeenVersion)
        return;
    m_lastSeenVersion = ver;

    QString json;
    {
        QMutexLocker lock(&m_binding.output->mutex);
        json = m_binding.output->json;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    m_vars = doc.isObject() ? doc.object() : QJsonObject();

    if (knownVariableNames() != m_chipKeys) {
        rebuildVariableChips();
    } else {
        for (QWidget *w : m_chipWidgets) {
            auto *chip = static_cast<VariableChip *>(w);
            chip->setValue(displayValue(m_vars.value(chip->name())));
        }
    }
    updatePreview();
}

void TextEditDialog::setFromDescriptor(const SourceDescriptor &desc) {
    ui->templateEdit->setPlainText(desc.textTemplate);
    ui->fontCombo->setCurrentFont(QFont(desc.fontFamily.isEmpty()
        ? QStringLiteral("Sans Serif") : desc.fontFamily));
    ui->sizeSpin->setValue(desc.fontSize > 0 ? desc.fontSize : 48);
    ui->boldBtn->setChecked(desc.textBold);
    ui->italicBtn->setChecked(desc.textItalic);
    ui->underlineBtn->setChecked(desc.textUnderline);
    ui->spacingSpin->setValue(desc.textLetterSpacing);
    ui->lineHeightSpin->setValue(desc.textLineHeight > 0 ? desc.textLineHeight : 100);

    const Qt::Alignment a(desc.textAlign);
    if (a & Qt::AlignJustify)      ui->alignJustifyBtn->setChecked(true);
    else if (a & Qt::AlignRight)   ui->alignRightBtn->setChecked(true);
    else if (a & Qt::AlignLeft)    ui->alignLeftBtn->setChecked(true);
    else                           ui->alignHCenterBtn->setChecked(true);
    if (a & Qt::AlignTop)          ui->alignTopBtn->setChecked(true);
    else if (a & Qt::AlignBottom)  ui->alignBottomBtn->setChecked(true);
    else                           ui->alignVCenterBtn->setChecked(true);

    ui->fillModeCombo->setCurrentIndex(desc.textGradient ? 1 : 0);
    ui->gradientDirCombo->setCurrentIndex(qBound(0, desc.textGradientDir, 3));
    m_textColor  = desc.color.isValid() ? desc.color : QColor(Qt::white);
    m_textColor2 = desc.textColor2.isValid() ? desc.textColor2 : QColor(0x00, 0xbf, 0xff);

    ui->outlineSpin->setValue(desc.textOutlineWidth);
    m_outlineColor = desc.textOutlineColor.isValid() ? desc.textOutlineColor
                                                     : QColor(Qt::black);
    ui->shadowXSpin->setValue(desc.textShadowDx);
    ui->shadowYSpin->setValue(desc.textShadowDy);
    m_shadowColor = desc.textShadowColor.isValid() ? desc.textShadowColor
                                                   : QColor(0, 0, 0, 160);

    ui->bgTransparentCheck->setChecked(desc.textBgTransparent);
    m_bgColor = desc.textBgColor.isValid() ? desc.textBgColor : QColor(Qt::black);

    if (desc.canvasWidth > 0)  m_canvasW = desc.canvasWidth;
    if (desc.canvasHeight > 0) m_canvasH = desc.canvasHeight;
}

SourceDescriptor TextEditDialog::currentDescriptor() const {
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Text;
    desc.textTemplate = ui->templateEdit->toPlainText().trimmed();
    desc.fontFamily = ui->fontCombo->currentFont().family();
    desc.fontSize = ui->sizeSpin->value();
    desc.textBold = ui->boldBtn->isChecked();
    desc.textItalic = ui->italicBtn->isChecked();
    desc.textUnderline = ui->underlineBtn->isChecked();
    desc.textLetterSpacing = ui->spacingSpin->value();
    desc.textLineHeight = ui->lineHeightSpin->value();

    const int h = m_hAlignGroup->checkedId() > 0 ? m_hAlignGroup->checkedId()
                                                 : int(Qt::AlignHCenter);
    const int v = m_vAlignGroup->checkedId() > 0 ? m_vAlignGroup->checkedId()
                                                 : int(Qt::AlignVCenter);
    desc.textAlign = h | v;

    desc.color = m_textColor;
    desc.textGradient = ui->fillModeCombo->currentIndex() == 1;
    desc.textColor2 = m_textColor2;
    desc.textGradientDir = ui->gradientDirCombo->currentIndex();
    desc.textOutlineWidth = ui->outlineSpin->value();
    desc.textOutlineColor = m_outlineColor;
    desc.textShadowDx = ui->shadowXSpin->value();
    desc.textShadowDy = ui->shadowYSpin->value();
    desc.textShadowColor = m_shadowColor;
    desc.textBgTransparent = ui->bgTransparentCheck->isChecked();
    desc.textBgColor = m_bgColor;
    desc.canvasWidth = m_canvasW;
    desc.canvasHeight = m_canvasH;

    const QString flat = desc.textTemplate.simplified();
    desc.displayName = flat.length() > 24 ? flat.left(21) + QStringLiteral("…") : flat;
    return desc;
}

SourceDescriptor TextEditDialog::resultDescriptor() const {
    return currentDescriptor();
}

void TextEditDialog::tryAccept() {
    if (ui->templateEdit->toPlainText().trimmed().isEmpty()) {
        ui->templateEdit->setFocus();
        return;
    }
    accept();
}

void TextEditDialog::pickColor(QColor &target, const QString &title) {
    const QColor c = QColorDialog::getColor(target, this, title,
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        target = c;
        updateColorButtons();
        updatePreview();
    }
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
    styleColorBtn(ui->textColor2Btn, m_textColor2);
    styleColorBtn(ui->outlineColorBtn, m_outlineColor);
    styleColorBtn(ui->shadowColorBtn, m_shadowColor);
    styleColorBtn(ui->bgColorBtn, m_bgColor);
}

void TextEditDialog::updateFillControls() {
    const bool gradient = ui->fillModeCombo->currentIndex() == 1;
    ui->textColor2Label->setVisible(gradient);
    ui->textColor2Btn->setVisible(gradient);
    ui->gradientDirLabel->setVisible(gradient);
    ui->gradientDirCombo->setVisible(gradient);
}

void TextEditDialog::updatePreview() {
    const SourceDescriptor desc = currentDescriptor();
    const QString resolved = m_binding.connected()
        ? TextSource::substitutePlaceholders(desc.textTemplate, m_vars)
        : desc.textTemplate;
    const QImage frame = TextSource::renderDescriptor(desc, resolved);
    const QPixmap pm = composePreview(frame, ui->previewLabel->size());
    if (!pm.isNull())
        ui->previewLabel->setPixmap(pm);
}

void TextEditDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    updatePreview();
}

void TextEditDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    updatePreview();
}
