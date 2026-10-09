#include "ui/editors/SvgTemplateDialog.h"
#include "ui_SvgTemplateDialog.h"

#include "core/sources/SvgTemplateSource.h"
#include "ui/common/Theme.h"

#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>

namespace {

constexpr int kIdRole = Qt::UserRole;
const QString kScaleName = QStringLiteral("scale");

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

void styleColorButton(QPushButton *btn, const QString &value) {
    const QColor c(value);
    if (!c.isValid()) {
        btn->setStyleSheet(QString());
        btn->setText(value.isEmpty() ? QObject::tr("Pick…") : value);
        return;
    }
    btn->setStyleSheet(QStringLiteral(
        "background-color:%1; color:%2; border:1px solid %3; border-radius:3px; padding:2px 8px;")
        .arg(c.name(QColor::HexArgb),
             c.lightness() > 128 ? QStringLiteral("#111") : QStringLiteral("#eee"),
             Theme::instance().tokens().textSecondary.name(QColor::HexArgb)));
    btn->setText(c.name(QColor::HexArgb).toUpper());
}

} // namespace

SvgTemplateDialog::SvgTemplateDialog(const SourceDescriptor &initial, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SvgTemplateDialog)
{
    ui->setupUi(this);

    ui->paramsTable->horizontalHeader()->setStretchLastSection(true);
    ui->paramsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->paramsTable->verticalHeader()->hide();
    ui->paramsTable->setSelectionMode(QAbstractItemView::NoSelection);

    for (const prism::SvgTemplateInfo &info : prism::builtinSvgTemplates()) {
        SourceDescriptor d;
        d.kind = SourceDescriptor::Kind::SvgTemplate;
        d.svgTemplateId = info.id;
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(
                                             SvgTemplateSource::renderDescriptor(d)
                                                 .scaled(ui->templateList->iconSize() * 2,
                                                         Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                         info.name, ui->templateList);
        item->setData(kIdRole, info.id);
    }

    connect(ui->templateList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && !m_building)
            selectTemplate(item->data(kIdRole).toString());
    });
    connect(ui->openFileBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open SVG Template"), QString(),
                                                          tr("SVG files (*.svg)"));
        if (!path.isEmpty())
            addCustomItem(path);
    });
    connect(ui->paramsTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!m_building && item->column() == 1)
            setValue(ui->paramsTable->item(item->row(), 0)->data(kIdRole).toString(), item->text());
    });
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (!m_info.id.isEmpty())
            accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (initial.kind == SourceDescriptor::Kind::SvgTemplate && !initial.svgTemplateId.isEmpty()) {
        m_overrides = SvgTemplateSource::paramsFromDescriptor(initial);
        QListWidgetItem *match = nullptr;
        for (int i = 0; i < ui->templateList->count(); ++i) {
            if (ui->templateList->item(i)->data(kIdRole).toString() == initial.svgTemplateId)
                match = ui->templateList->item(i);
        }
        if (!match) {
            addCustomItem(initial.svgTemplateId);
        } else {
            m_building = true;
            ui->templateList->setCurrentItem(match);
            m_building = false;
            selectTemplate(initial.svgTemplateId);
        }
    } else if (ui->templateList->count() > 0) {
        ui->templateList->setCurrentRow(0);
    }
}

SvgTemplateDialog::~SvgTemplateDialog() {
    delete ui;
}

void SvgTemplateDialog::addCustomItem(const QString &path) {
    const QByteArray svg = prism::svgTemplateBytes(path);
    if (svg.isEmpty())
        return;
    SourceDescriptor d;
    d.kind = SourceDescriptor::Kind::SvgTemplate;
    d.svgTemplateId = path;
    const QImage thumb = SvgTemplateSource::renderDescriptor(d);
    auto *item = new QListWidgetItem(
        QIcon(QPixmap::fromImage(thumb.scaled(ui->templateList->iconSize() * 2, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation))),
        QFileInfo(path).fileName(), ui->templateList);
    item->setData(kIdRole, path);
    ui->templateList->setCurrentItem(item);
}

void SvgTemplateDialog::selectTemplate(const QString &id) {
    const QByteArray svg = prism::svgTemplateBytes(id);
    if (!m_info.id.isEmpty() && m_info.id != id)
        m_overrides = {};
    m_info = prism::svgTemplateInfo(id, svg);
    if (m_info.name.isEmpty())
        m_info.name = QFileInfo(id).completeBaseName();
    rebuildParams();
    updatePreview();
}

void SvgTemplateDialog::rebuildParams() {
    m_building = true;
    QTableWidget *table = ui->paramsTable;
    table->setRowCount(0);

    QList<prism::SvgTemplateParam> rows;
    rows.append({kScaleName, tr("Scale"), QStringLiteral("number"), QStringLiteral("1")});
    rows.append(m_info.params);

    for (const prism::SvgTemplateParam &p : std::as_const(rows)) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto *nameItem = new QTableWidgetItem(p.label);
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setData(kIdRole, p.name);
        nameItem->setToolTip(QStringLiteral("{%1}").arg(p.name));
        table->setItem(row, 0, nameItem);

        const QJsonValue ov = m_overrides.value(p.name);
        const QString value = ov.isUndefined() ? p.defaultValue
                                               : (ov.isString() ? ov.toString() : QString::number(ov.toDouble()));
        if (p.type == QLatin1String("color")) {
            auto *valueItem = new QTableWidgetItem();
            valueItem->setFlags(Qt::ItemIsEnabled);
            table->setItem(row, 1, valueItem);
            auto *btn = new QPushButton(table);
            styleColorButton(btn, value);
            connect(btn, &QPushButton::clicked, this, [this, name = p.name]() { pickColor(name); });
            table->setCellWidget(row, 1, btn);
        } else {
            table->setItem(row, 1, new QTableWidgetItem(value));
        }
    }
    m_building = false;
}

void SvgTemplateDialog::setValue(const QString &name, const QString &value) {
    QString def;
    if (name == kScaleName) {
        def = QStringLiteral("1");
    } else {
        for (const prism::SvgTemplateParam &p : std::as_const(m_info.params)) {
            if (p.name == name)
                def = p.defaultValue;
        }
    }
    if (value == def)
        m_overrides.remove(name);
    else
        m_overrides.insert(name, value);
    updatePreview();
}

void SvgTemplateDialog::pickColor(const QString &name) {
    QString current;
    for (const prism::SvgTemplateParam &p : std::as_const(m_info.params)) {
        if (p.name == name)
            current = p.defaultValue;
    }
    if (m_overrides.contains(name))
        current = m_overrides.value(name).toString();
    const QColor c = QColorDialog::getColor(QColor(current), this, tr("Pick Color"),
                                            QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return;
    const QString hex = c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
    setValue(name, hex);
    for (int row = 0; row < ui->paramsTable->rowCount(); ++row) {
        if (ui->paramsTable->item(row, 0)->data(kIdRole).toString() == name) {
            if (auto *btn = qobject_cast<QPushButton *>(ui->paramsTable->cellWidget(row, 1)))
                styleColorButton(btn, hex);
        }
    }
}

SourceDescriptor SvgTemplateDialog::currentDescriptor() const {
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::SvgTemplate;
    desc.svgTemplateId = m_info.id;
    desc.svgParamsJson = SvgTemplateSource::paramsToJson(m_overrides);
    desc.displayName = m_info.name;
    return desc;
}

SourceDescriptor SvgTemplateDialog::resultDescriptor() const {
    return currentDescriptor();
}

void SvgTemplateDialog::updatePreview() {
    if (m_info.id.isEmpty())
        return;
    const QPixmap pm = composePreview(SvgTemplateSource::renderDescriptor(currentDescriptor()),
                                      ui->previewLabel->size());
    if (!pm.isNull())
        ui->previewLabel->setPixmap(pm);
}

void SvgTemplateDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    updatePreview();
}

void SvgTemplateDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    updatePreview();
}
