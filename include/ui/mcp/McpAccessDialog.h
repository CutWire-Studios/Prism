#pragma once

#include <QDialog>
#include <QPointer>

class MainWindow;
class QCheckBox;
class QLabel;
class QLineEdit;
class QWidget;

/// Session-only localhost MCP for agents. Off at every launch; never persisted.
class McpAccessDialog : public QDialog {
    Q_OBJECT
public:
    explicit McpAccessDialog(MainWindow *window, QWidget *parent = nullptr);

private slots:
    void refresh();
    void onToggled(bool on);
    void copyCursor();
    void copyClaude();
    void copyStdio();
    void copyGuide();

private:
    QPointer<MainWindow> m_window;
    QCheckBox *m_enable = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_error = nullptr;
    QLineEdit *m_url = nullptr;
    QLineEdit *m_token = nullptr;
    QWidget *m_runningBox = nullptr;
};
