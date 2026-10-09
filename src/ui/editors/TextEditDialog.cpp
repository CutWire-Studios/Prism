#include "ui/editors/TextEditDialog.h"

#include "core/sources/TextSource.h"
#include "ui/common/Theme.h"

#include "core/render/SkiaRender.h"
#include "core/text/TextAnimationPreset.h"
#include "core/text/TextLook.h"
#include "core/text/TextPresetStore.h"
#include "ui/editors/style/ShadingLayerStackEditor.h"
#include "ui/editors/style/StyleControls.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QGroupBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
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
        const auto &t = Theme::instance().tokens();
        setStyleSheet(QStringLiteral(
            "QFrame#varChip { background:%1; border:1px solid %2; border-radius:9px; }"
            "QFrame#varChip:hover { border-color:%3; }")
            .arg(t.glassControl.name(QColor::HexArgb), t.stroke.name(QColor::HexArgb),
                 t.textSecondary.name(QColor::HexArgb)));

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(9, 4, 9, 4);
        lay->setSpacing(6);

        auto *nameLabel = new QLabel(QStringLiteral("{%1}").arg(name), this);
        nameLabel->setProperty("role", "secondary");
        nameLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
        lay->addWidget(nameLabel);

        m_valueLabel = new QLabel(QStringLiteral("—"), this);
        m_valueLabel->setProperty("role", "secondary");
        m_valueLabel->setStyleSheet(QStringLiteral("font-size:11px;"));
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
                fmt.setForeground(Theme::instance().tokens().textSecondary);
            else if (m_known.contains(m.captured(1)))
                fmt.setForeground(Theme::instance().tokens().accent);
            else
                fmt.setForeground(Theme::instance().tokens().warning);
            setFormat(m.capturedStart(), m.capturedLength(), fmt);
        }
    }

private:
    QSet<QString> m_known;
    bool m_connected = false;
};


namespace {

using prism::TextAnimParamSpec;
using prism::VectorSlotValue;
using style::ColorButton;
using style::SliderSpinRow;

QString prettyId(const QString &id) {
    QString out;
    for (int i = 0; i < id.size(); ++i) {
        const QChar c = id.at(i);
        if (i == 0) {
            out += c.toUpper();
        } else if (c.isUpper()) {
            out += QLatin1Char(' ');
            out += c.toLower();
        } else {
            out += c;
        }
    }
    return out;
}

int decimalsFor(double step) {
    int d = 0;
    while (step < 0.999 && d < 4) {
        step *= 10;
        ++d;
    }
    return d;
}

QPixmap cardPixmap(const QImage &img, const QSize &size) {
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x24, 0x24, 0x24));
    p.drawRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 6, 6);
    if (!img.isNull())
        p.drawImage(QRect(QPoint(0, 0), size), img);
    return pm;
}

QSize canvasForCard(const QSize &size, int height) {
    return QSize(qMax(1, height * size.width() / qMax(1, size.height())), height);
}

QPixmap packThumb(const prism::TextPreset &preset, const QSize &size) {
    const QSize canvas(1080, qMax(1, 1080 * size.height() / qMax(1, size.width())));
    const QString text = preset.sampleText.isEmpty() ? QStringLiteral("Your text here") : preset.sampleText;
    prism::TextAnimClock clock;
    clock.totalSec = prism::textAnimationInSeconds(preset.style, text, canvas);
    return cardPixmap(prism::renderText(preset.style, text, canvas, clock), size);
}

QPixmap lookThumb(const QString &id, const QString &family, int weight, bool italic, const QSize &size) {
    prism::TextStyle s;
    s.fontFamily = family.isEmpty() ? QStringLiteral("Inter") : family;
    s.fontWeight = weight;
    s.italic = italic;
    s.pixelSize = 58;
    s.wordWrap = false;
    prism::applyTextLook(s, id, {});
    return cardPixmap(prism::renderText(s, QStringLiteral("Aa"), canvasForCard(size, 150), {}), size);
}

QPixmap animThumb(int slot, const prism::TextAnimationPreset &preset, const QSize &size) {
    prism::TextStyle s;
    s.pixelSize = 58;
    prism::TextAnimationSlot anim;
    anim.presetId = preset.id;
    (slot == 0 ? s.animation.in : slot == 1 ? s.animation.out : s.animation.loop) = anim;
    const QSize canvas = canvasForCard(size, 150);
    const QString text = preset.sampleText.isEmpty() ? QStringLiteral("Your text") : preset.sampleText;
    prism::TextAnimClock clock;
    if (slot == 0) {
        const double len = prism::textAnimationInSeconds(s, text, canvas);
        clock.totalSec = len;
        if (len > 0) {
            clock.phase = prism::TextAnimClock::Phase::In;
            clock.phaseElapsedSec = clock.totalSec = len * 0.55;
        }
    } else if (slot == 1) {
        const double len = prism::textAnimationOutSeconds(s, text, canvas);
        if (len > 0) {
            clock.phase = prism::TextAnimClock::Phase::Out;
            clock.phaseElapsedSec = clock.totalSec = len * 0.45;
            clock.outWindowSec = len;
        }
    } else {
        clock.totalSec = 0.5;
    }
    return cardPixmap(prism::renderText(s, text, canvas, clock), size);
}

QLabel *sectionHeader(const QString &text, QWidget *parent) {
    auto *h = new QLabel(text, parent);
    h->setObjectName(QStringLiteral("sectionHeader"));
    return h;
}

QListWidget *makeIconList(QWidget *parent, const QSize &icon, const QSize &grid) {
    auto *list = new QListWidget(parent);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(icon);
    list->setGridSize(grid);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return list;
}

void fitListHeight(QListWidget *list, int perRow, int maxRows) {
    const int rows = qMax(1, (list->count() + perRow - 1) / perRow);
    list->setFixedHeight(qMin(rows, maxRows) * list->gridSize().height() + 8);
}

void selectById(QListWidget *list, const QString &id) {
    const QSignalBlocker block(list);
    list->clearSelection();
    list->setCurrentItem(nullptr);
    if (id.isEmpty() && list->objectName() != QStringLiteral("animList"))
        return;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole).toString() == id) {
            list->setCurrentRow(i);
            return;
        }
    }
}

QWidget *scrollPage(QWidget *content, QWidget *parent) {
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

} // namespace

// Renders one control per TextAnimParamSpec (look params, animation slot params).
class TextEditDialog::ParamForm : public QWidget {
public:
    explicit ParamForm(QWidget *parent = nullptr) : QWidget(parent) {
        m_lay = new QVBoxLayout(this);
        m_lay->setContentsMargins(0, 0, 0, 0);
        m_lay->setSpacing(6);
    }

    std::function<void(const QString &, const VectorSlotValue &)> onChanged;

    void setSpecs(const QList<TextAnimParamSpec> &specs, const QMap<QString, VectorSlotValue> &values) {
        for (const Row &row : std::as_const(m_rows)) {
            m_lay->removeWidget(row.widget);
            row.widget->hide();
            row.widget->deleteLater();
        }
        m_rows.clear();
        for (const TextAnimParamSpec &spec : specs)
            addRow(spec, values.value(spec.id, spec.defaultValue));
    }

    void setValues(const QMap<QString, VectorSlotValue> &values) {
        for (const Row &row : std::as_const(m_rows))
            row.set(values.value(row.id, row.def));
    }

    void setRowEnabled(const QString &id, bool on) {
        for (const Row &row : std::as_const(m_rows))
            if (row.id == id)
                row.widget->setEnabled(on);
    }

private:
    struct Row {
        QString id;
        VectorSlotValue def;
        QWidget *widget = nullptr;
        std::function<void(const VectorSlotValue &)> set;
    };

    void emitChange(const QString &id, const VectorSlotValue &v) {
        if (onChanged)
            onChanged(id, v);
    }

    void addRow(const TextAnimParamSpec &spec, const VectorSlotValue &val) {
        using Type = TextAnimParamSpec::Type;
        Row row;
        row.id = spec.id;
        row.def = spec.defaultValue;
        const QString id = spec.id;
        switch (spec.type) {
        case Type::Scalar: {
            const double step = spec.step > 0 ? spec.step : (spec.max - spec.min) / 100.0;
            auto *r = new SliderSpinRow(spec.label, spec.min, spec.max, step, decimalsFor(step), spec.unit, this);
            r->setValue(val.scalar);
            connect(r, &SliderSpinRow::valueChanged, this,
                    [this, id](double v) { emitChange(id, VectorSlotValue::fromScalar(v)); });
            row.widget = r;
            row.set = [r](const VectorSlotValue &v) { r->setValue(v.scalar); };
            break;
        }
        case Type::Enum: {
            auto *c = new QComboBox(this);
            for (const QString &v : spec.enumValues)
                c->addItem(prettyId(v), v);
            const auto set = [c](const QString &t) {
                const int i = c->findData(t);
                if (i >= 0) {
                    const QSignalBlocker b(c);
                    c->setCurrentIndex(i);
                }
            };
            set(val.text);
            connect(c, &QComboBox::activated, this,
                    [this, id, c](int i) { emitChange(id, VectorSlotValue::fromText(c->itemData(i).toString())); });
            row.widget = style::labeledRow(spec.label, c, this);
            row.set = [set](const VectorSlotValue &v) { set(v.text); };
            break;
        }
        case Type::Color: {
            auto *b = new ColorButton(this);
            b->setColor(val.color.isValid() ? val.color : QColor(Qt::white));
            connect(b, &ColorButton::colorChanged, this,
                    [this, id](const QColor &c) { emitChange(id, VectorSlotValue::fromColor(c)); });
            row.widget = style::labeledRow(spec.label, b, this);
            row.set = [b](const VectorSlotValue &v) { b->setColor(v.color.isValid() ? v.color : QColor(Qt::white)); };
            break;
        }
        case Type::Bool: {
            auto *c = new QCheckBox(spec.label, this);
            c->setChecked(val.scalar != 0.0);
            connect(c, &QCheckBox::clicked, this,
                    [this, id](bool on) { emitChange(id, VectorSlotValue::fromScalar(on ? 1.0 : 0.0)); });
            row.widget = c;
            row.set = [c](const VectorSlotValue &v) {
                const QSignalBlocker b(c);
                c->setChecked(v.scalar != 0.0);
            };
            break;
        }
        case Type::Text: {
            auto *e = new QLineEdit(val.text, this);
            connect(e, &QLineEdit::textEdited, this,
                    [this, id](const QString &t) { emitChange(id, VectorSlotValue::fromText(t)); });
            row.widget = style::labeledRow(spec.label, e, this);
            row.set = [e](const VectorSlotValue &v) {
                const QSignalBlocker b(e);
                e->setText(v.text);
            };
            break;
        }
        case Type::Vec2: {
            auto *host = new QWidget(this);
            auto *hl = new QHBoxLayout(host);
            hl->setContentsMargins(0, 0, 0, 0);
            auto *x = new QDoubleSpinBox(host);
            auto *y = new QDoubleSpinBox(host);
            for (auto *s : {x, y}) {
                s->setRange(spec.min < spec.max ? spec.min : -10000.0, spec.min < spec.max ? spec.max : 10000.0);
                s->setSingleStep(spec.step > 0 ? spec.step : 1.0);
                s->setKeyboardTracking(false);
                hl->addWidget(s);
            }
            x->setValue(val.vec2.x());
            y->setValue(val.vec2.y());
            const auto emitVec = [this, id, x, y] {
                emitChange(id, VectorSlotValue::fromVec2(QPointF(x->value(), y->value())));
            };
            connect(x, &QDoubleSpinBox::valueChanged, this, emitVec);
            connect(y, &QDoubleSpinBox::valueChanged, this, emitVec);
            row.widget = style::labeledRow(spec.label, host, this);
            row.set = [x, y](const VectorSlotValue &v) {
                const QSignalBlocker bx(x), by(y);
                x->setValue(v.vec2.x());
                y->setValue(v.vec2.y());
            };
            break;
        }
        }
        m_lay->addWidget(row.widget);
        m_rows.append(row);
    }

    QVBoxLayout *m_lay;
    QList<Row> m_rows;
};

TextEditDialog::TextEditDialog(const SourceDescriptor &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Text Source"));
    setMinimumSize(1120, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    root->addLayout(row, 1);

    auto *center = new QVBoxLayout;
    center->setSpacing(6);
    row->addLayout(center, 1);

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(320, 220);
    m_preview->setProperty("role", "panel");
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    center->addWidget(m_preview, 1);

    auto *playRow = new QHBoxLayout;
    m_playBtn = new QPushButton(tr("Play"), this);
    m_playBtn->setCheckable(true);
    m_playBtn->setToolTip(tr("Preview In, hold, Out on a loop"));
    playRow->addWidget(m_playBtn);
    playRow->addStretch(1);
    center->addLayout(playRow);

    center->addWidget(sectionHeader(tr("TEMPLATE"), this));
    m_templateEdit = new QPlainTextEdit(this);
    m_templateEdit->setPlaceholderText(QStringLiteral("Hello {name}"));
    m_templateEdit->setTabChangesFocus(true);
    m_templateEdit->setMinimumHeight(84);
    m_templateEdit->setMaximumHeight(130);
    center->addWidget(m_templateEdit);

    auto *varsRow = new QHBoxLayout;
    varsRow->addWidget(sectionHeader(tr("SCRIPT VARIABLES"), this));
    varsRow->addStretch(1);
    m_runScriptBtn = new QToolButton(this);
    m_runScriptBtn->setText(tr("Run script"));
    m_runScriptBtn->setToolTip(tr("Execute the connected script now to refresh values"));
    varsRow->addWidget(m_runScriptBtn);
    center->addLayout(varsRow);

    m_varsHint = new QLabel(tr("Wire a Script node's ScriptOut port to this clip's DataIn port, then the "
                               "script's variables appear here as blocks. Click or drag a block into the "
                               "template; {placeholders} are replaced with live values."), this);
    m_varsHint->setWordWrap(true);
    m_varsHint->setProperty("role", "secondary");
    m_varsHint->setStyleSheet(QStringLiteral("font-size:11px; padding:8px;"));
    center->addWidget(m_varsHint);

    m_chipsScroll = new QScrollArea(this);
    m_chipsScroll->setWidgetResizable(true);
    m_chipsScroll->setFrameShape(QFrame::NoFrame);
    m_chipsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chipsScroll->setFixedHeight(46);
    m_chipsHost = new QWidget;
    auto *chipsLayout = new QHBoxLayout(m_chipsHost);
    chipsLayout->setContentsMargins(0, 4, 0, 4);
    chipsLayout->setSpacing(6);
    m_chipsScroll->setWidget(m_chipsHost);
    center->addWidget(m_chipsScroll);

    m_runScriptBtn->hide();
    m_chipsScroll->hide();

    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(1);
    connect(m_thumbTimer, &QTimer::timeout, this, &TextEditDialog::runThumbBatch);

    m_tabs = new QTabWidget(this);
    m_tabs->setFixedWidth(470);
    auto addTab = [&](const QString &title, void (TextEditDialog::*build)(QWidget *)) {
        auto *page = new QWidget;
        (this->*build)(page);
        m_tabs->addTab(scrollPage(page, m_tabs), title);
    };
    addTab(tr("Type"), &TextEditDialog::buildTypeTab);
    addTab(tr("Look"), &TextEditDialog::buildLookTab);
    addTab(tr("Animate"), &TextEditDialog::buildAnimateTab);
    row->addWidget(m_tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &TextEditDialog::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *acceptShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(acceptShortcut, &QShortcut::activated, this, &TextEditDialog::tryAccept);
    buttons->button(QDialogButtonBox::Ok)->setToolTip(tr("Ctrl+Enter"));

    m_highlighter = new Highlighter(m_templateEdit->document());

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(25);
    connect(m_renderTimer, &QTimer::timeout, this, &TextEditDialog::updatePreview);
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(33);
    connect(m_playTimer, &QTimer::timeout, this, &TextEditDialog::renderPlayFrame);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout, this, &TextEditDialog::onPollScriptOutput);

    connect(m_templateEdit, &QPlainTextEdit::textChanged, this, [this] {
        m_durDirty = true;
        scheduleRender();
    });
    connect(m_runScriptBtn, &QToolButton::clicked, this, [this] {
        if (m_binding.requestRun)
            m_binding.requestRun();
    });
    connect(m_playBtn, &QPushButton::clicked, this, [this](bool on) { on ? startPlay() : stopPlay(); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            fillThumbs(m_userPacks, [](const QString &id) { return QStringLiteral("pack|") + id; },
                       [](const QString &id) {
                           const auto p = prism::textPresetForId(id);
                           return p ? packThumb(*p, QSize(120, 54)) : QPixmap();
                       });
            fillThumbs(m_builtinPacks, [](const QString &id) { return QStringLiteral("pack|") + id; },
                       [](const QString &id) {
                           const auto p = prism::textPresetForId(id);
                           return p ? packThumb(*p, QSize(120, 54)) : QPixmap();
                       });
            const QString family = m_style.fontFamily;
            const int weight = m_style.fontWeight;
            const bool italic = m_style.italic;
            fillThumbs(m_lookList,
                       [=](const QString &id) {
                           return QStringLiteral("look|%1|%2|%3|%4").arg(id, family).arg(weight).arg(italic);
                       },
                       [=](const QString &id) { return lookThumb(id, family, weight, italic, QSize(72, 40)); });
        } else if (index == 2) {
            rebuildAnimList();
        }
    });

    if (initial.kind == SourceDescriptor::Kind::Text)
        setFromDescriptor(initial);
    else
        m_templateEdit->setPlainText(QStringLiteral("Hello"));

    syncControlsFromStyle();
}

TextEditDialog::~TextEditDialog() = default;

void TextEditDialog::buildTypeTab(QWidget *page) {
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 8, 8, 0);
    lay->setSpacing(8);

    lay->addWidget(sectionHeader(tr("FONT"), page));
    m_font = new QFontComboBox(page);
    lay->addWidget(style::labeledRow(tr("Font"), m_font, page));

    m_weight = new QComboBox(page);
    const struct { int w; const char *name; } weights[] = {
        {100, "Thin"}, {200, "ExtraLight"}, {300, "Light"}, {400, "Regular"}, {500, "Medium"},
        {600, "SemiBold"}, {700, "Bold"}, {800, "ExtraBold"}, {900, "Black"}};
    for (const auto &w : weights)
        m_weight->addItem(tr(w.name), w.w);
    m_italic = new QCheckBox(tr("Italic"), page);
    auto *weightHost = new QWidget(page);
    auto *wl = new QHBoxLayout(weightHost);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->addWidget(m_weight, 1);
    wl->addWidget(m_italic);
    lay->addWidget(style::labeledRow(tr("Weight"), weightHost, page));

    m_size = new SliderSpinRow(tr("Size"), 8, 600, 1, 0, QStringLiteral("px"), page);
    lay->addWidget(m_size);

    m_align = new QComboBox(page);
    m_align->addItem(tr("Left"), int(prism::TextAlign::Left));
    m_align->addItem(tr("Center"), int(prism::TextAlign::Center));
    m_align->addItem(tr("Right"), int(prism::TextAlign::Right));
    m_valign = new QComboBox(page);
    m_valign->addItem(tr("Top"), int(prism::TextVAlign::Top));
    m_valign->addItem(tr("Middle"), int(prism::TextVAlign::Middle));
    m_valign->addItem(tr("Bottom"), int(prism::TextVAlign::Bottom));
    auto *alignHost = new QWidget(page);
    auto *al = new QHBoxLayout(alignHost);
    al->setContentsMargins(0, 0, 0, 0);
    al->addWidget(m_align);
    al->addWidget(m_valign);
    lay->addWidget(style::labeledRow(tr("Align"), alignHost, page));

    lay->addWidget(sectionHeader(tr("SPACING"), page));
    m_wrap = new QCheckBox(tr("Word wrap"), page);
    lay->addWidget(m_wrap);
    m_lineHeight = new SliderSpinRow(tr("Line height"), 0.5, 3.0, 0.05, 2, QString(), page);
    m_letterSpacing = new SliderSpinRow(tr("Letter spacing"), -20, 100, 0.5, 1, QStringLiteral("px"), page);
    m_bend = new SliderSpinRow(tr("Bend"), -100, 100, 1, 0, QString(), page);
    lay->addWidget(m_lineHeight);
    lay->addWidget(m_letterSpacing);
    lay->addWidget(m_bend);

    lay->addWidget(sectionHeader(tr("DECORATIONS"), page));
    const auto makeGroup = [&](const QString &title, QGroupBox *&group, QWidget *host) {
        group = new QGroupBox(title, host);
        group->setCheckable(true);
        auto *gl = new QVBoxLayout(group);
        gl->setSpacing(6);
        return gl;
    };

    auto *gl = makeGroup(tr("Background"), m_boxGroup, page);
    m_boxColor = new ColorButton(m_boxGroup);
    m_boxPadding = new SliderSpinRow(tr("Padding"), 0, 500, 1, 0, QStringLiteral("px"), m_boxGroup);
    m_boxRadius = new SliderSpinRow(tr("Corner radius"), 0, 500, 1, 0, QStringLiteral("px"), m_boxGroup);
    gl->addWidget(style::labeledRow(tr("Colour"), m_boxColor, m_boxGroup));
    gl->addWidget(m_boxPadding);
    gl->addWidget(m_boxRadius);
    lay->addWidget(m_boxGroup);

    gl = makeGroup(tr("Word highlight"), m_hlGroup, page);
    m_hlColor = new ColorButton(m_hlGroup);
    m_hlPadding = new SliderSpinRow(tr("Thickness"), 0, 200, 1, 0, QStringLiteral("px"), m_hlGroup);
    m_hlRadius = new SliderSpinRow(tr("Corner radius"), 0, 200, 1, 0, QStringLiteral("px"), m_hlGroup);
    gl->addWidget(style::labeledRow(tr("Colour"), m_hlColor, m_hlGroup));
    gl->addWidget(m_hlPadding);
    gl->addWidget(m_hlRadius);
    lay->addWidget(m_hlGroup);

    gl = makeGroup(tr("Underline"), m_underlineGroup, page);
    m_ulColor = new ColorButton(m_underlineGroup);
    m_ulWidth = new SliderSpinRow(tr("Thickness"), 0, 100, 1, 0, QStringLiteral("px"), m_underlineGroup);
    m_ulOffset = new SliderSpinRow(tr("Offset"), -200, 200, 1, 0, QStringLiteral("px"), m_underlineGroup);
    gl->addWidget(style::labeledRow(tr("Colour"), m_ulColor, m_underlineGroup));
    gl->addWidget(m_ulWidth);
    gl->addWidget(m_ulOffset);
    lay->addWidget(m_underlineGroup);

    lay->addWidget(sectionHeader(tr("WORD ACCENT"), page));
    m_accentRule = new QComboBox(page);
    const struct { prism::WordAccentRule rule; const char *name; } rules[] = {
        {prism::WordAccentRule::None, "None"}, {prism::WordAccentRule::FirstWord, "First word"},
        {prism::WordAccentRule::LastWord, "Last word"}, {prism::WordAccentRule::EveryOther, "Every other word"},
        {prism::WordAccentRule::EveryNth, "Every Nth word"}, {prism::WordAccentRule::LongestWord, "Longest word"},
        {prism::WordAccentRule::RandomStable, "Random words"}};
    for (const auto &r : rules)
        m_accentRule->addItem(tr(r.name), int(r.rule));
    lay->addWidget(style::labeledRow(tr("Accent"), m_accentRule, page));
    m_accentN = new SliderSpinRow(tr("Every N"), 1, 16, 1, 0, QString(), page);
    lay->addWidget(m_accentN);

    m_accentBody = new QWidget(page);
    auto *bl = new QVBoxLayout(m_accentBody);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    gl = makeGroup(tr("Accent colour"), m_accentColorGroup, m_accentBody);
    m_accentColor = new ColorButton(m_accentColorGroup);
    m_accentSize = new SliderSpinRow(tr("Accent size"), 0.25, 4, 0.05, 2, QStringLiteral("x"), m_accentBody);
    gl->addWidget(style::labeledRow(tr("Colour"), m_accentColor, m_accentColorGroup));
    bl->addWidget(m_accentColorGroup);
    bl->addWidget(m_accentSize);
    gl = makeGroup(tr("Accent outline"), m_accentOutlineGroup, m_accentBody);
    m_accentOutlineColor = new ColorButton(m_accentOutlineGroup);
    m_accentOutlineWidth = new SliderSpinRow(tr("Width"), 0, 100, 1, 0, QStringLiteral("px"), m_accentOutlineGroup);
    gl->addWidget(style::labeledRow(tr("Colour"), m_accentOutlineColor, m_accentOutlineGroup));
    gl->addWidget(m_accentOutlineWidth);
    bl->addWidget(m_accentOutlineGroup);
    gl = makeGroup(tr("Accent pill"), m_accentPillGroup, m_accentBody);
    m_accentPillColor = new ColorButton(m_accentPillGroup);
    gl->addWidget(style::labeledRow(tr("Colour"), m_accentPillColor, m_accentPillGroup));
    bl->addWidget(m_accentPillGroup);
    lay->addWidget(m_accentBody);
    lay->addStretch(1);

    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        m_style.fontFamily = f.family();
        styleEdited();
    });
    connect(m_weight, &QComboBox::activated, this, [this](int i) {
        m_style.fontWeight = m_weight->itemData(i).toInt();
        styleEdited();
    });
    connect(m_italic, &QCheckBox::clicked, this, [this](bool on) { m_style.italic = on; styleEdited(); });
    connect(m_size, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.pixelSize = int(v); styleEdited(); });
    connect(m_align, &QComboBox::activated, this, [this](int i) {
        m_style.align = prism::TextAlign(m_align->itemData(i).toInt());
        styleEdited();
    });
    connect(m_valign, &QComboBox::activated, this, [this](int i) {
        m_style.valign = prism::TextVAlign(m_valign->itemData(i).toInt());
        styleEdited();
    });
    connect(m_wrap, &QCheckBox::clicked, this, [this](bool on) { m_style.wordWrap = on; styleEdited(); });
    connect(m_lineHeight, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.lineHeight = v; styleEdited(); });
    connect(m_letterSpacing, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.letterSpacing = v; styleEdited(); });
    connect(m_bend, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.pathBend = v; styleEdited(); });

    connect(m_boxGroup, &QGroupBox::clicked, this, [this](bool on) { m_style.boxEnabled = on; styleEdited(); });
    connect(m_boxColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_style.boxColor = c; styleEdited(); });
    connect(m_boxPadding, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.boxPadding = v; styleEdited(); });
    connect(m_boxRadius, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.boxRadius = v; styleEdited(); });

    connect(m_hlGroup, &QGroupBox::clicked, this, [this](bool on) { m_style.wordHighlight.enabled = on; styleEdited(); });
    connect(m_hlColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_style.wordHighlight.color = c; styleEdited(); });
    connect(m_hlPadding, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.wordHighlight.padding = v; styleEdited(); });
    connect(m_hlRadius, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.wordHighlight.radius = v; styleEdited(); });

    connect(m_underlineGroup, &QGroupBox::clicked, this, [this](bool on) { m_style.underlineEnabled = on; styleEdited(); });
    connect(m_ulColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_style.underlineColor = c; styleEdited(); });
    connect(m_ulWidth, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.underlineWidth = v; styleEdited(); });
    connect(m_ulOffset, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.underlineOffset = v; styleEdited(); });

    connect(m_accentRule, &QComboBox::activated, this, [this](int i) {
        m_style.accent.rule = prism::WordAccentRule(m_accentRule->itemData(i).toInt());
        m_accentN->setVisible(m_style.accent.rule == prism::WordAccentRule::EveryNth);
        m_accentBody->setVisible(m_style.accent.rule != prism::WordAccentRule::None);
        styleEdited();
    });
    connect(m_accentN, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.accent.n = int(v); styleEdited(); });
    connect(m_accentColorGroup, &QGroupBox::clicked, this, [this](bool on) { m_style.accent.colorEnabled = on; styleEdited(); });
    connect(m_accentColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_style.accent.color = c; styleEdited(); });
    connect(m_accentSize, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.accent.sizeScale = v; styleEdited(); });
    connect(m_accentOutlineGroup, &QGroupBox::clicked, this, [this](bool on) { m_style.accent.outlineEnabled = on; styleEdited(); });
    connect(m_accentOutlineColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_style.accent.outlineColor = c; styleEdited(); });
    connect(m_accentOutlineWidth, &SliderSpinRow::valueChanged, this, [this](double v) { m_style.accent.outlineWidth = v; styleEdited(); });
    connect(m_accentPillGroup, &QGroupBox::clicked, this, [this](bool on) { m_style.accent.highlight.enabled = on; styleEdited(); });
    connect(m_accentPillColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_style.accent.highlight.color = c; styleEdited(); });
}

void TextEditDialog::buildLookTab(QWidget *page) {
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 8, 8, 0);
    lay->setSpacing(8);

    lay->addWidget(sectionHeader(tr("STYLE PACK"), page));
    const QSize packIcon(120, 54), packGrid(134, 84);
    m_myStylesHeader = new QLabel(tr("My styles"), page);
    m_myStylesHeader->setProperty("role", "secondary");
    m_userPacks = makeIconList(page, packIcon, packGrid);
    m_userPacks->setContextMenuPolicy(Qt::CustomContextMenu);
    m_builtinPacks = makeIconList(page, packIcon, packGrid);
    auto *builtinLabel = new QLabel(tr("Built-in"), page);
    builtinLabel->setProperty("role", "secondary");
    lay->addWidget(m_myStylesHeader);
    lay->addWidget(m_userPacks);
    lay->addWidget(builtinLabel);
    lay->addWidget(m_builtinPacks);
    auto *saveBtn = new QPushButton(tr("Save style…"), page);
    saveBtn->setToolTip(tr("Keep the current style in My styles"));
    lay->addWidget(saveBtn, 0, Qt::AlignLeft);

    lay->addWidget(sectionHeader(tr("LOOK"), page));
    m_lookList = makeIconList(page, QSize(72, 40), QSize(84, 68));
    for (const prism::TextLook &look : prism::textLooks()) {
        auto *item = new QListWidgetItem(look.label, m_lookList);
        item->setData(Qt::UserRole, look.id);
        item->setTextAlignment(Qt::AlignHCenter);
    }
    m_lookList->setFixedHeight(3 * 68 + 8);
    lay->addWidget(m_lookList);
    m_lookParams = new ParamForm(page);
    lay->addWidget(m_lookParams);

    lay->addWidget(sectionHeader(tr("LAYERS"), page));
    m_layers = new style::ShadingLayerStackEditor(page);
    m_layers->setScopeVisible(true);
    m_layers->setGradientSpaceVisible(true);
    lay->addWidget(m_layers);
    lay->addStretch(1);

    rebuildPackLists();

    const auto onPack = [this](QListWidgetItem *item) { applyStylePack(item->data(Qt::UserRole).toString()); };
    connect(m_userPacks, &QListWidget::itemClicked, this, onPack);
    connect(m_builtinPacks, &QListWidget::itemClicked, this, onPack);
    connect(saveBtn, &QPushButton::clicked, this, &TextEditDialog::saveStylePreset);
    connect(m_userPacks, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_userPacks->itemAt(pos);
        if (!item)
            return;
        const QString id = item->data(Qt::UserRole).toString();
        QMenu menu(this);
        QAction *rename = menu.addAction(tr("Rename…"));
        QAction *remove = menu.addAction(tr("Delete"));
        QAction *picked = menu.exec(m_userPacks->viewport()->mapToGlobal(pos));
        if (picked == rename) {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Rename style"), tr("Name"), QLineEdit::Normal,
                                                       item->text(), &ok).trimmed();
            if (ok && !name.isEmpty() && prism::TextPresetStore::instance().rename(id, name))
                item->setText(name);
        } else if (picked == remove) {
            prism::TextPresetStore::instance().remove(id);
            if (m_style.packId == id)
                m_style.packId.clear();
            rebuildPackLists();
        }
    });

    connect(m_lookList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!prism::applyTextLook(m_style, item->data(Qt::UserRole).toString(), {}))
            return;
        m_style.packId.clear();
        syncControlsFromStyle();
        m_durDirty = true;
        scheduleRender();
    });
    m_lookParams->onChanged = [this](const QString &id, const VectorSlotValue &v) {
        if (m_style.lookId.isEmpty())
            return;
        QMap<QString, VectorSlotValue> params = m_style.lookParams;
        params.insert(id, v);
        prism::applyTextLook(m_style, m_style.lookId, params);
        m_style.packId.clear();
        m_layers->setLayers(m_style.layers);
        m_loading = true;
        syncPackSelection();
        m_loading = false;
        m_durDirty = true;
        scheduleRender();
    };
    connect(m_layers, &style::ShadingLayerStackEditor::changed, this, &TextEditDialog::layersEdited);
}

void TextEditDialog::buildAnimateTab(QWidget *page) {
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 8, 8, 0);
    lay->setSpacing(8);

    m_slotBar = new QTabBar(page);
    m_slotBar->addTab(tr("In"));
    m_slotBar->addTab(tr("Out"));
    m_slotBar->addTab(tr("Loop"));
    m_slotBar->setExpanding(true);
    lay->addWidget(m_slotBar);

    m_animCategory = new QComboBox(page);
    lay->addWidget(style::labeledRow(tr("Category"), m_animCategory, page));

    m_animList = makeIconList(page, QSize(120, 68), QSize(134, 98));
    m_animList->setObjectName(QStringLiteral("animList"));
    m_animList->setFixedHeight(3 * 98 + 8);
    lay->addWidget(m_animList);

    m_animNote = new QLabel(page);
    m_animNote->setWordWrap(true);
    m_animNote->setProperty("role", "secondary");
    lay->addWidget(m_animNote);
    m_animRevert = new QPushButton(tr("Revert to preset"), page);
    lay->addWidget(m_animRevert, 0, Qt::AlignLeft);
    m_animParams = new ParamForm(page);
    lay->addWidget(m_animParams);
    lay->addStretch(1);

    connect(m_slotBar, &QTabBar::currentChanged, this, [this] { rebuildAnimList(); });
    connect(m_animCategory, &QComboBox::activated, this, [this] { rebuildAnimList(); });
    connect(m_animList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        applyAnimationPreset(m_slotBar->currentIndex(), item->data(Qt::UserRole).toString());
        if (!item->data(Qt::UserRole).toString().isEmpty())
            startPlay();
    });
    connect(m_animRevert, &QPushButton::clicked, this, [this] {
        applyAnimationPreset(m_slotBar->currentIndex(), QString());
    });
    m_animParams->onChanged = [this](const QString &id, const VectorSlotValue &v) {
        prism::TextAnimationSlot &slot = slotRef(m_slotBar->currentIndex());
        slot.params.insert(id, v);
        styleEdited();
        syncAnimTab();
    };

    rebuildAnimList();
}

prism::TextAnimationSlot &TextEditDialog::slotRef(int slot) {
    return slot == 0 ? m_style.animation.in : slot == 1 ? m_style.animation.out : m_style.animation.loop;
}

void TextEditDialog::applyStylePack(const QString &id) {
    const auto st = prism::textStyleForPresetId(id);
    if (!st)
        return;
    m_style = *st;
    m_style.packId = id;
    syncControlsFromStyle();
    m_durDirty = true;
    scheduleRender();
}

void TextEditDialog::applyAnimationPreset(int slot, const QString &presetId) {
    prism::TextAnimationSlot fresh;
    fresh.presetId = presetId;
    slotRef(slot) = fresh;
    styleEdited();
    if (m_slotBar && m_slotBar->currentIndex() == slot)
        syncAnimTab();
}

void TextEditDialog::rebuildAnimList() {
    using prism::TextAnimationPresetCatalog;
    const int slot = m_slotBar->currentIndex();
    const auto kind = slot == 0 ? prism::TextAnimSlotKind::In
                    : slot == 1 ? prism::TextAnimSlotKind::Out : prism::TextAnimSlotKind::Loop;
    const QList<prism::TextAnimationPreset> presets = TextAnimationPresetCatalog::instance().presetsFor(kind);

    const QString wanted = m_animCategory->currentData().toString();
    {
        const QSignalBlocker block(m_animCategory);
        m_animCategory->clear();
        m_animCategory->addItem(tr("All"), QString());
        QStringList seen;
        for (const auto &p : presets) {
            if (!p.category.isEmpty() && !seen.contains(p.category)) {
                seen << p.category;
                m_animCategory->addItem(prettyId(p.category), p.category);
            }
        }
        const int idx = m_animCategory->findData(wanted);
        m_animCategory->setCurrentIndex(idx >= 0 ? idx : 0);
        m_animCategory->parentWidget()->setVisible(seen.size() > 1);
    }
    const QString category = m_animCategory->currentData().toString();

    m_animList->clear();
    auto *none = new QListWidgetItem(tr("None"), m_animList);
    none->setData(Qt::UserRole, QString());
    none->setTextAlignment(Qt::AlignHCenter);
    for (const auto &p : presets) {
        if (!category.isEmpty() && p.category != category)
            continue;
        auto *item = new QListWidgetItem(p.label, m_animList);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(p.label);
        item->setTextAlignment(Qt::AlignHCenter);
    }
    if (m_tabs && m_tabs->currentIndex() == 2)
        fillThumbs(m_animList, [slot](const QString &id) { return QStringLiteral("anim|%1|%2").arg(slot).arg(id); },
                   [slot](const QString &id) {
                       const auto p = TextAnimationPresetCatalog::instance().presetForId(id);
                       return p ? animThumb(slot, *p, QSize(120, 68)) : QPixmap();
                   });
    syncAnimTab();
}

void TextEditDialog::syncAnimTab() {
    const int slotIndex = m_slotBar->currentIndex();
    const prism::TextAnimationSlot &slot = slotRef(slotIndex);
    selectById(m_animList, slot.presetId);

    const auto preset = prism::TextAnimationPresetCatalog::instance().presetForId(slot.presetId);
    const bool custom = slot.presetId.isEmpty() && !slot.animators.isEmpty();
    m_animNote->setVisible(custom);
    m_animRevert->setVisible(custom);
    if (custom)
        m_animNote->setText(tr("Custom animation set outside the editor. Preset controls are disabled."));

    if (!preset) {
        m_animParams->setSpecs({}, {});
        m_animParams->hide();
        return;
    }
    QMap<QString, VectorSlotValue> values = preset->defaultParams();
    for (auto it = slot.params.cbegin(); it != slot.params.cend(); ++it)
        values.insert(it.key(), it.value());

    const QStringList current = [&] {
        QStringList ids;
        for (const auto &s : preset->params)
            ids << s.id;
        return ids;
    }();
    if (m_animParams->property("presetKey").toString() != QStringLiteral("%1|%2").arg(slotIndex).arg(preset->id)
        || m_animParams->property("paramIds").toStringList() != current) {
        QList<TextAnimParamSpec> specs;
        for (const TextAnimParamSpec &s : preset->params) {
            if ((s.id == QLatin1String("unit") && preset->flag(QStringLiteral("unitLocked")))
                || (s.id == QLatin1String("order") && preset->flag(QStringLiteral("orderLocked")))
                || (s.id == QLatin1String("duration") && preset->flag(QStringLiteral("durationLocked")))
                || (s.id == QLatin1String("ease") && preset->flag(QStringLiteral("easeLocked"))))
                continue;
            specs.append(s);
        }
        m_animParams->setSpecs(specs, values);
        m_animParams->setProperty("presetKey", QStringLiteral("%1|%2").arg(slotIndex).arg(preset->id));
        m_animParams->setProperty("paramIds", current);
    } else {
        m_animParams->setValues(values);
    }
    const bool block = values.value(QStringLiteral("unit"), VectorSlotValue::fromText(QStringLiteral("block")))
                           .text == QLatin1String("block");
    m_animParams->setRowEnabled(QStringLiteral("stagger"), !block);
    m_animParams->setRowEnabled(QStringLiteral("order"), !block);
    m_animParams->setVisible(true);
}

void TextEditDialog::rebuildPackLists() {
    m_userPacks->clear();
    m_builtinPacks->clear();
    for (const prism::TextPreset &p : prism::TextPresetStore::instance().presets()) {
        auto *item = new QListWidgetItem(p.label, m_userPacks);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(p.label);
        item->setTextAlignment(Qt::AlignHCenter);
    }
    for (const prism::TextPreset &p : prism::textPresets()) {
        auto *item = new QListWidgetItem(p.label, m_builtinPacks);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(p.label);
        item->setTextAlignment(Qt::AlignHCenter);
    }
    m_myStylesHeader->setVisible(m_userPacks->count() > 0);
    m_userPacks->setVisible(m_userPacks->count() > 0);
    fitListHeight(m_userPacks, 3, 2);
    fitListHeight(m_builtinPacks, 3, 4);
    syncPackSelection();
    if (m_tabs && m_tabs->currentIndex() == 1)
        emit m_tabs->currentChanged(1);
}

void TextEditDialog::saveStylePreset() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save style"), tr("Name"), QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QString sample = m_templateEdit->toPlainText().simplified().left(40);
    if (sample.isEmpty())
        sample = tr("Your text here");
    const QString id = prism::TextPresetStore::instance().add(name, m_style, sample);
    if (id.isEmpty())
        return;
    m_style.packId = id;
    rebuildPackLists();
}

void TextEditDialog::fillThumbs(QListWidget *list, const std::function<QString(const QString &)> &keyFor,
                                const std::function<QPixmap(const QString &)> &render) {
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty())
            continue;
        const QString key = keyFor(id);
        const auto hit = m_thumbCache.constFind(key);
        if (hit != m_thumbCache.constEnd()) {
            item->setIcon(QIcon(*hit));
            continue;
        }
        const bool queued = std::any_of(m_thumbJobs.cbegin(), m_thumbJobs.cend(), [&](const ThumbJob &j) {
            return j.list == list && j.key == key;
        });
        if (!queued)
            m_thumbJobs.append({list, id, key, [render, id] { return render(id); }});
    }
    if (!m_thumbJobs.isEmpty() && !m_thumbTimer->isActive())
        m_thumbTimer->start();
}

void TextEditDialog::runThumbBatch() {
    for (int n = 0; n < 3 && !m_thumbJobs.isEmpty(); ++n) {
        const ThumbJob job = m_thumbJobs.takeFirst();
        if (!job.list)
            continue;
        QPixmap pm = m_thumbCache.value(job.key);
        if (pm.isNull()) {
            pm = job.render();
            m_thumbCache.insert(job.key, pm);
        }
        for (int i = 0; i < job.list->count(); ++i) {
            if (job.list->item(i)->data(Qt::UserRole).toString() == job.id) {
                job.list->item(i)->setIcon(QIcon(pm));
                break;
            }
        }
    }
    if (!m_thumbJobs.isEmpty())
        m_thumbTimer->start();
}

void TextEditDialog::syncPackSelection() {
    if (!m_userPacks)
        return;
    selectById(m_userPacks, m_style.packId);
    selectById(m_builtinPacks, m_style.packId);
}

void TextEditDialog::syncLookParams() {
    const prism::TextLook *look = prism::textLookForId(m_style.lookId);
    selectById(m_lookList, m_style.lookId);
    if (!look || look->params.isEmpty()) {
        m_lookParams->setSpecs({}, {});
        m_lookParams->hide();
        return;
    }
    m_lookParams->setSpecs(look->params, m_style.lookParams);
    m_lookParams->show();
}

void TextEditDialog::syncControlsFromStyle() {
    const prism::TextStyle &s = m_style;
    const auto setCombo = [](QComboBox *c, int data) {
        const QSignalBlocker b(c);
        const int i = c->findData(data);
        if (i >= 0)
            c->setCurrentIndex(i);
    };
    {
        const QSignalBlocker b(m_font);
        m_font->setCurrentFont(QFont(s.fontFamily));
    }
    setCombo(m_weight, qBound(100, int(std::lround(s.fontWeight / 100.0)) * 100, 900));
    m_italic->setChecked(s.italic);
    m_size->setValue(s.pixelSize);
    setCombo(m_align, int(s.align));
    setCombo(m_valign, int(s.valign));
    m_wrap->setChecked(s.wordWrap);
    m_lineHeight->setValue(s.lineHeight);
    m_letterSpacing->setValue(s.letterSpacing);
    m_bend->setValue(s.pathBend);

    const auto setGroup = [](QGroupBox *g, bool on) {
        const QSignalBlocker b(g);
        g->setChecked(on);
    };
    setGroup(m_boxGroup, s.boxEnabled);
    m_boxColor->setColor(s.boxColor);
    m_boxPadding->setValue(s.boxPadding);
    m_boxRadius->setValue(s.boxRadius);
    setGroup(m_hlGroup, s.wordHighlight.enabled);
    m_hlColor->setColor(s.wordHighlight.color);
    m_hlPadding->setValue(s.wordHighlight.padding);
    m_hlRadius->setValue(s.wordHighlight.radius);
    setGroup(m_underlineGroup, s.underlineEnabled);
    m_ulColor->setColor(s.underlineColor);
    m_ulWidth->setValue(s.underlineWidth);
    m_ulOffset->setValue(s.underlineOffset);

    setCombo(m_accentRule, int(s.accent.rule));
    m_accentN->setValue(s.accent.n);
    m_accentN->setVisible(s.accent.rule == prism::WordAccentRule::EveryNth);
    m_accentBody->setVisible(s.accent.rule != prism::WordAccentRule::None);
    setGroup(m_accentColorGroup, s.accent.colorEnabled);
    m_accentColor->setColor(s.accent.color);
    m_accentSize->setValue(s.accent.sizeScale);
    setGroup(m_accentOutlineGroup, s.accent.outlineEnabled);
    m_accentOutlineColor->setColor(s.accent.outlineColor);
    m_accentOutlineWidth->setValue(s.accent.outlineWidth);
    setGroup(m_accentPillGroup, s.accent.highlight.enabled);
    m_accentPillColor->setColor(s.accent.highlight.color);

    m_layers->setLayers(s.layers);
    syncLookParams();
    syncPackSelection();
    syncAnimTab();
}

void TextEditDialog::styleEdited() {
    m_style.packId.clear();
    m_durDirty = true;
    syncPackSelection();
    scheduleRender();
}

void TextEditDialog::layersEdited() {
    m_style.layers = m_layers->layers();
    m_style.lookId.clear();
    m_style.lookParams.clear();
    syncLookParams();
    styleEdited();
}

void TextEditDialog::scheduleRender() {
    if (m_playing)
        return;
    if (!m_renderTimer->isActive())
        m_renderTimer->start();
}

QString TextEditDialog::resolvedText() const {
    const QString tmpl = m_templateEdit->toPlainText().trimmed();
    return m_binding.connected() ? TextSource::substitutePlaceholders(tmpl, m_vars) : tmpl;
}

void TextEditDialog::refreshDurations() {
    const QString text = resolvedText();
    const QSize canvas(m_canvasW, m_canvasH);
    m_inSec = prism::textAnimationInSeconds(m_style, text, canvas);
    m_outSec = prism::textAnimationOutSeconds(m_style, text, canvas);
    m_durDirty = false;
}

void TextEditDialog::startPlay() {
    m_playing = true;
    m_playBtn->setChecked(true);
    m_playBtn->setText(tr("Stop"));
    refreshDurations();
    m_playClock.restart();
    m_playTimer->start();
    renderPlayFrame();
}

void TextEditDialog::stopPlay() {
    m_playing = false;
    m_playBtn->setChecked(false);
    m_playBtn->setText(tr("Play"));
    m_playTimer->stop();
    renderHeld();
}

void TextEditDialog::renderPlayFrame() {
    using Phase = prism::TextAnimClock::Phase;
    if (m_durDirty)
        refreshDurations();
    constexpr double kHold = 1.0;
    constexpr double kGap = 0.3;
    const double cycle = m_inSec + kHold + m_outSec + (m_outSec > 0 ? kGap : 0.0);
    const double t = std::fmod(m_playClock.elapsed() / 1000.0, cycle);

    prism::TextAnimClock clock;
    clock.totalSec = t;
    if (t < m_inSec) {
        clock.phase = Phase::In;
        clock.phaseElapsedSec = t;
    } else if (t >= m_inSec + kHold && m_outSec > 0) {
        clock.phase = Phase::Out;
        clock.phaseElapsedSec = qMin(t - m_inSec - kHold, m_outSec);
        clock.outWindowSec = m_outSec;
    }
    const QImage frame = prism::renderText(m_style, resolvedText(), QSize(m_canvasW, m_canvasH), clock)
                             .convertToFormat(QImage::Format_RGBA8888);
    const QPixmap pm = composePreview(frame, m_preview->size());
    if (!pm.isNull())
        m_preview->setPixmap(pm);
}

void TextEditDialog::renderHeld() {
    const QImage frame = TextSource::renderDescriptor(currentDescriptor(), resolvedText());
    const QPixmap pm = composePreview(frame, m_preview->size());
    if (!pm.isNull())
        m_preview->setPixmap(pm);
}

void TextEditDialog::updatePreview() {
    if (m_playing)
        renderPlayFrame();
    else
        renderHeld();
}

void TextEditDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    updatePreview();
}

void TextEditDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    scheduleRender();
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

    m_runScriptBtn->setVisible(bool(m_binding.requestRun));
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
        m_chipsScroll->hide();
        m_varsHint->setText(tr("The connected script hasn't produced any variables yet. "
                               "Press \"Run script\" or wait for its next run."));
        m_varsHint->show();
        return;
    }

    m_varsHint->hide();
    auto *lay = static_cast<QHBoxLayout *>(m_chipsHost->layout());
    if (!m_chipStretchAdded) {
        lay->addStretch(1);
        m_chipStretchAdded = true;
    }
    for (const QString &name : names) {
        auto *chip = new VariableChip(name, m_chipsHost);
        chip->onInsert = [this](const QString &token) { insertToken(token); };
        lay->insertWidget(lay->count() - 1, chip);
        m_chipWidgets << chip;
        chip->setValue(displayValue(m_vars.value(name)));
    }
    m_chipsScroll->show();
}

void TextEditDialog::insertToken(const QString &token) {
    m_templateEdit->textCursor().insertText(token);
    m_templateEdit->setFocus();
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
    m_durDirty = true;
    updatePreview();
}

void TextEditDialog::setFromDescriptor(const SourceDescriptor &desc) {
    m_templateEdit->setPlainText(desc.textTemplate);
    m_style = TextSource::styleFromDescriptor(desc);
    if (desc.canvasWidth > 0)  m_canvasW = desc.canvasWidth;
    if (desc.canvasHeight > 0) m_canvasH = desc.canvasHeight;
}

SourceDescriptor TextEditDialog::currentDescriptor() const {
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Text;
    desc.textTemplate = m_templateEdit->toPlainText().trimmed();
    desc.textStyleJson = TextSource::styleToJson(m_style);
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
    if (m_templateEdit->toPlainText().trimmed().isEmpty()) {
        m_templateEdit->setFocus();
        return;
    }
    accept();
}
