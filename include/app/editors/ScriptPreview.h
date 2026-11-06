#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "core/scripting/ScriptOutput.h"

class ScriptRuntime;

// Lua script presets and a one-shot run of the script under edit, for the Script node editor.
class ScriptPreview : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)
    Q_PROPERTY(QString output READ output NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit ScriptPreview(QObject *parent = nullptr);
    ~ScriptPreview() override;

    QStringList presetNames() const;
    Q_INVOKABLE QString presetCode(int row) const;
    // -1 when the code matches no preset.
    Q_INVOKABLE int presetIndexFor(const QString &code) const;

    QString output() const { return m_output; }
    QString error() const { return m_error; }

    Q_INVOKABLE void run(const QString &code);
    Q_INVOKABLE void clear();

signals:
    void resultChanged();

private:
    std::shared_ptr<ScriptOutput> m_scriptOutput;
    std::unique_ptr<ScriptRuntime> m_runtime;
    QString m_output;
    QString m_error;
};
