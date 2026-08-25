#include "ui/mcp/McpAccessDialog.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpServer.h"
#include "ui/mainwindow/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

McpAccessDialog::McpAccessDialog(MainWindow *window, QWidget *parent)
    : QDialog(parent)
    , m_window(window)
{
    setWindowTitle(tr("Agent Access"));
    setMinimumWidth(560);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto *warning = new QLabel(
        tr("Starts a localhost MCP server so Cursor, Claude Code, or other agents can drive "
           "this mixer. Not saved. Stops when Prism quits or you turn this off. Any local "
           "process with the session token has full mixer access."),
        this);
    warning->setWordWrap(true);
    warning->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    layout->addWidget(warning);

    m_enable = new QCheckBox(tr("Enable agent access for this session"), this);
    layout->addWidget(m_enable);

    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_error->setStyleSheet(QStringLiteral("color: #e04545;"));
    m_error->hide();
    layout->addWidget(m_error);

    m_runningBox = new QWidget(this);
    auto *runLayout = new QVBoxLayout(m_runningBox);
    runLayout->setContentsMargins(0, 4, 0, 0);
    runLayout->setSpacing(8);

    m_status = new QLabel(m_runningBox);
    m_status->setWordWrap(true);
    m_status->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    runLayout->addWidget(m_status);

    m_url = new QLineEdit(m_runningBox);
    m_url->setReadOnly(true);
    m_url->setFocusPolicy(Qt::ClickFocus);
    runLayout->addWidget(m_url);

    m_token = new QLineEdit(m_runningBox);
    m_token->setReadOnly(true);
    m_token->setFocusPolicy(Qt::ClickFocus);
    runLayout->addWidget(m_token);

    auto *btnGrid = new QGridLayout();
    btnGrid->setHorizontalSpacing(8);
    btnGrid->setVerticalSpacing(8);
    auto *copyCursorBtn = new QPushButton(tr("Copy Cursor snippet"), m_runningBox);
    auto *copyClaudeBtn = new QPushButton(tr("Copy Claude command"), m_runningBox);
    auto *copyStdioBtn = new QPushButton(tr("Copy stdio snippet"), m_runningBox);
    auto *copyGuideBtn = new QPushButton(tr("Copy agent guide"), m_runningBox);
    btnGrid->addWidget(copyCursorBtn, 0, 0);
    btnGrid->addWidget(copyClaudeBtn, 0, 1);
    btnGrid->addWidget(copyStdioBtn, 1, 0);
    btnGrid->addWidget(copyGuideBtn, 1, 1);
    runLayout->addLayout(btnGrid);

    auto *hint = new QLabel(
        tr("Pinned endpoints: /mcp/sources, /mcp/decks, /mcp/transition, "
           "/mcp/panic, /mcp/output, /mcp/session.\n"
           "Workflow: catalog → toolbox({name}) → apply({ops})."),
        m_runningBox);
    hint->setWordWrap(true);
    hint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    hint->setStyleSheet(QStringLiteral("color: #888;"));
    runLayout->addWidget(hint);

    m_runningBox->hide();
    layout->addWidget(m_runningBox);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch();
    closeRow->addWidget(closeBtn);
    layout->addLayout(closeRow);

    connect(m_enable, &QCheckBox::toggled, this, &McpAccessDialog::onToggled);
    connect(copyCursorBtn, &QPushButton::clicked, this, &McpAccessDialog::copyCursor);
    connect(copyClaudeBtn, &QPushButton::clicked, this, &McpAccessDialog::copyClaude);
    connect(copyStdioBtn, &QPushButton::clicked, this, &McpAccessDialog::copyStdio);
    connect(copyGuideBtn, &QPushButton::clicked, this, &McpAccessDialog::copyGuide);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    if (m_window && m_window->mcpServer()) {
        connect(m_window->mcpServer(), &prism::mcp::McpServer::runningChanged,
                this, &McpAccessDialog::refresh);
        connect(m_window->mcpServer(), &prism::mcp::McpServer::errorChanged,
                this, &McpAccessDialog::refresh);
    }

    refresh();
}

void McpAccessDialog::refresh()
{
    if (!m_window)
        return;
    auto *server = m_window->mcpServer();
    const bool running = server && server->running();

    m_enable->blockSignals(true);
    m_enable->setChecked(running);
    m_enable->blockSignals(false);

    const QString error = server ? server->error() : QString();
    m_error->setVisible(!error.isEmpty());
    m_error->setText(error);

    m_runningBox->setVisible(running);
    if (running) {
        m_status->setText(tr("Listening on %1").arg(server->url()));
        m_url->setText(server->url());
        m_token->setText(server->token());
    }
    adjustSize();
}

void McpAccessDialog::onToggled(bool on)
{
    if (m_window)
        m_window->setMcpEnabled(on);
    refresh();
}

void McpAccessDialog::copyCursor()
{
    if (auto *s = m_window ? m_window->mcpServer() : nullptr)
        QApplication::clipboard()->setText(s->cursorSnippet());
}

void McpAccessDialog::copyClaude()
{
    if (auto *s = m_window ? m_window->mcpServer() : nullptr)
        QApplication::clipboard()->setText(s->claudeCommand());
}

void McpAccessDialog::copyStdio()
{
    if (auto *s = m_window ? m_window->mcpServer() : nullptr)
        QApplication::clipboard()->setText(s->stdioSnippet());
}

void McpAccessDialog::copyGuide()
{
    QString guide = prism::mcp::agentGuideText();
    if (auto *s = m_window ? m_window->mcpServer() : nullptr) {
        if (s->running()) {
            guide += QStringLiteral("\nThis session: %1\nAuthorization: Bearer %2\n")
                         .arg(s->url(), s->token());
        }
    }
    QApplication::clipboard()->setText(guide);
}
