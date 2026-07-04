#include "ui/hotkeys/HotkeyEditorDialog.h"
#include "ui/hotkeys/HotkeyManager.h"
#include "ui/nodes/ClipNodeEditor.h"

#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QFile>
#include <QBrush>
#include <QColor>

namespace {

class HotkeyCaptureEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit HotkeyCaptureEdit(QWidget *parent = nullptr)
        : QLineEdit(parent)
    {
        setReadOnly(true);
        setPlaceholderText(QObject::tr("Click, then press a key…"));
        setAlignment(Qt::AlignCenter);
    }

    Qt::Key capturedKey() const { return m_key; }

    void setCapturedKey(Qt::Key key) {
        m_key = key;
        setText(key == Qt::Key_unknown ? QString() : QKeySequence(key).toString());
    }

protected:
    void focusInEvent(QFocusEvent *event) override {
        QLineEdit::focusInEvent(event);
        setStyleSheet(QStringLiteral("background-color: #2a3a55;"));
        setPlaceholderText(QObject::tr("Press a key… (Esc to cancel, Del to clear)"));
    }

    void focusOutEvent(QFocusEvent *event) override {
        QLineEdit::focusOutEvent(event);
        setStyleSheet({});
        setPlaceholderText(QObject::tr("Click, then press a key…"));
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            clearFocus();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
            setCapturedKey(Qt::Key_unknown);
            emit keyChanged(Qt::Key_unknown);
            event->accept();
            return;
        }
        if (!HotkeyManager::isBindableKey(static_cast<Qt::Key>(event->key()))) {
            event->ignore();
            return;
        }

        const Qt::Key key = static_cast<Qt::Key>(event->key());
        setCapturedKey(key);
        emit keyChanged(key);
        event->accept();
    }

signals:
    void keyChanged(Qt::Key key);

private:
    Qt::Key m_key = Qt::Key_unknown;
};

} // namespace

HotkeyEditorDialog::HotkeyEditorDialog(HotkeyManager *manager, ClipNodeEditor *editor,
                                       QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_editor(editor)
{
    setWindowTitle(tr("Edit Hotkeys"));
    setMinimumSize(720, 420);
    resize(780, 480);

    auto *intro = new QLabel(
        tr("Assign trigger keys for each A/B switcher input. The key sends the "
           "input to Deck A; Shift+the same key sends it to Deck B."),
        this);
    intro->setWordWrap(true);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({
        tr("Switcher Input"),
        tr("Hotkey"),
        tr("Deck A"),
        tr("Deck B"),
    });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    populateTable();

    auto *importBtn = new QPushButton(tr("Import Profile…"), this);
    auto *exportBtn = new QPushButton(tr("Export Profile…"), this);
    auto *clearBtn  = new QPushButton(tr("Clear All"), this);
    connect(importBtn, &QPushButton::clicked, this, &HotkeyEditorDialog::onImportProfile);
    connect(exportBtn, &QPushButton::clicked, this, &HotkeyEditorDialog::onExportProfile);
    connect(clearBtn,  &QPushButton::clicked, this, &HotkeyEditorDialog::onClearAll);

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(importBtn);
    btnRow->addWidget(exportBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();

    auto *buttons = new QHBoxLayout;
    auto *applyBtn = new QPushButton(tr("Apply"), this);
    applyBtn->setDefault(true);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(applyBtn, &QPushButton::clicked, this, &HotkeyEditorDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addStretch();
    buttons->addWidget(applyBtn);
    buttons->addWidget(closeBtn);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_table, 1);
    layout->addLayout(btnRow);
    layout->addLayout(buttons);
}

void HotkeyEditorDialog::populateTable() {
    m_table->setRowCount(0);

    const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();

    // Count distinct switchers so labels only mention the node when ambiguous.
    QList<NodeId> abNodes;
    for (const AbSlotInfo &info : inputs)
        if (!abNodes.contains(info.ref.abNodeId))
            abNodes.append(info.ref.abNodeId);

    int row = 0;
    for (const AbSlotInfo &info : inputs) {
        m_table->insertRow(row);

        QString label = info.name.isEmpty() ? tr("In %1").arg(info.ref.slot + 1)
                                            : info.name;
        if (!info.sourceName.isEmpty())
            label += QStringLiteral(" — %1").arg(info.sourceName);
        if (abNodes.size() > 1)
            label = tr("A/B #%1 · %2").arg(abNodes.indexOf(info.ref.abNodeId) + 1).arg(label);

        auto *nameItem = new QTableWidgetItem(label);
        nameItem->setData(Qt::UserRole,     QVariant::fromValue<qulonglong>(info.ref.abNodeId));
        nameItem->setData(Qt::UserRole + 1, info.ref.slot);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, nameItem);

        auto *capture = new HotkeyCaptureEdit(m_table);
        capture->setCapturedKey(m_manager->bindingForSlot(info.ref));
        connect(capture, &HotkeyCaptureEdit::keyChanged, this, [this](Qt::Key) {
            refreshConflictHighlights();
        });
        m_table->setCellWidget(row, 1, capture);

        const Qt::Key key = m_manager->bindingForSlot(info.ref);
        const QString keyText = key == Qt::Key_unknown ? tr("—") : QKeySequence(key).toString();
        auto *deckA = new QTableWidgetItem(keyText);
        auto *deckB = new QTableWidgetItem(
            key == Qt::Key_unknown ? tr("—")
                                   : QKeySequence(Qt::SHIFT | key).toString());
        deckA->setFlags(deckA->flags() & ~Qt::ItemIsEditable);
        deckB->setFlags(deckB->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 2, deckA);
        m_table->setItem(row, 3, deckB);

        connect(capture, &HotkeyCaptureEdit::keyChanged, this, [deckA, deckB](Qt::Key k) {
            if (k == Qt::Key_unknown) {
                deckA->setText(QObject::tr("—"));
                deckB->setText(QObject::tr("—"));
            } else {
                deckA->setText(QKeySequence(k).toString());
                deckB->setText(QKeySequence(Qt::SHIFT | k).toString());
            }
        });

        ++row;
    }

    refreshConflictHighlights();
}

AbSlotRef HotkeyEditorDialog::slotRefForRow(int row) const {
    const QTableWidgetItem *item = m_table->item(row, 0);
    if (!item)
        return {};
    return {item->data(Qt::UserRole).toULongLong(),
            item->data(Qt::UserRole + 1).toInt()};
}

QMap<AbSlotRef, Qt::Key> HotkeyEditorDialog::collectBindings(bool *hasConflicts) const {
    QMap<AbSlotRef, Qt::Key> bindings;
    QList<Qt::Key> usedKeys;
    if (hasConflicts)
        *hasConflicts = false;

    for (int row = 0; row < m_table->rowCount(); ++row) {
        const AbSlotRef ref = slotRefForRow(row);
        const auto *capture = qobject_cast<const HotkeyCaptureEdit *>(m_table->cellWidget(row, 1));
        if (!ref.isValid() || !capture)
            continue;

        const Qt::Key key = capture->capturedKey();
        if (key == Qt::Key_unknown)
            continue;

        if (usedKeys.contains(key)) {
            if (hasConflicts)
                *hasConflicts = true;
        } else {
            usedKeys.append(key);
            bindings.insert(ref, key);
        }
    }
    return bindings;
}

void HotkeyEditorDialog::refreshConflictHighlights() {
    QMap<Qt::Key, int> keyCounts;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto *capture = qobject_cast<const HotkeyCaptureEdit *>(m_table->cellWidget(row, 1));
        if (!capture)
            continue;
        const Qt::Key key = capture->capturedKey();
        if (key != Qt::Key_unknown)
            ++keyCounts[key];
    }

    const QBrush okBrush;
    const QBrush conflictBrush(QColor(120, 32, 32));

    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *capture = qobject_cast<HotkeyCaptureEdit *>(m_table->cellWidget(row, 1));
        if (!capture)
            continue;

        const Qt::Key key = capture->capturedKey();
        const bool conflict = key != Qt::Key_unknown && keyCounts.value(key) > 1;
        capture->setStyleSheet(conflict
            ? QStringLiteral("background-color: #5a2020; color: #fff;")
            : (capture->hasFocus() ? QStringLiteral("background-color: #2a3a55;") : QString()));
        for (int col = 0; col < 4; ++col) {
            if (QTableWidgetItem *item = m_table->item(row, col))
                item->setBackground(conflict ? conflictBrush : okBrush);
        }
    }
}

void HotkeyEditorDialog::onImportProfile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Hotkey Profile"), {},
        tr("Hotkey Profiles (*.json);;All Files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Hotkeys"),
                             tr("Could not open file:\n%1").arg(path));
        return;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("Import Hotkeys"),
                             tr("Invalid profile file:\n%1").arg(err.errorString()));
        return;
    }

    QStringList warnings;
    if (!m_manager->importProfile(doc.object(), &warnings)) {
        QMessageBox::warning(this, tr("Import Hotkeys"),
                             warnings.isEmpty() ? tr("Import failed.") : warnings.join('\n'));
        return;
    }

    populateTable();
    if (!warnings.isEmpty()) {
        QMessageBox::information(this, tr("Import Hotkeys"),
                                 tr("Profile imported with notes:\n%1").arg(warnings.join('\n')));
    }
}

void HotkeyEditorDialog::onExportProfile() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Hotkey Profile"), {},
        tr("Hotkey Profiles (*.json);;All Files (*)"));
    if (path.isEmpty())
        return;

    QString outPath = path;
    if (!outPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        outPath += QStringLiteral(".json");

    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    QJsonArray bindings;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const AbSlotRef ref = slotRefForRow(row);
        const auto *capture = qobject_cast<const HotkeyCaptureEdit *>(m_table->cellWidget(row, 1));
        if (!ref.isValid() || !capture)
            continue;
        const Qt::Key key = capture->capturedKey();
        if (key == Qt::Key_unknown)
            continue;

        QJsonObject entry;
        entry.insert(QStringLiteral("slotKey"), HotkeyManager::slotSettingsKey(ref));
        entry.insert(QStringLiteral("key"), static_cast<int>(key));
        bindings.append(entry);
    }
    root.insert(QStringLiteral("bindings"), bindings);

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Export Hotkeys"),
                             tr("Could not write file:\n%1").arg(outPath));
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QMessageBox::information(this, tr("Export Hotkeys"),
                             tr("Hotkey profile saved to:\n%1").arg(outPath));
}

void HotkeyEditorDialog::onClearAll() {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (auto *capture = qobject_cast<HotkeyCaptureEdit *>(m_table->cellWidget(row, 1)))
            capture->setCapturedKey(Qt::Key_unknown);
    }
    refreshConflictHighlights();
}

void HotkeyEditorDialog::onApply() {
    bool hasConflicts = false;
    const QMap<AbSlotRef, Qt::Key> bindings = collectBindings(&hasConflicts);
    if (hasConflicts) {
        QMessageBox::warning(this, tr("Hotkey Conflict"),
                             tr("Two or more switcher inputs use the same key. "
                                "Resolve conflicts before applying."));
        return;
    }

    m_manager->applyBindings(bindings);
    QMessageBox::information(this, tr("Hotkeys Applied"),
                             tr("Hotkey bindings updated and saved."));
    accept();
}

#include "ui/HotkeyEditorDialog.moc"
