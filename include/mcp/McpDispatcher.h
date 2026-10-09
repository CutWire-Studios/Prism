#pragma once

#include <QJsonObject>
#include <QString>

class MainWindow;

namespace prism::mcp {

class McpDispatcher
{
public:
    explicit McpDispatcher(MainWindow *window);

    QJsonObject inspect(const QJsonObject &args) const;
    QJsonObject apply(const QJsonObject &args);
    QJsonObject applyOne(const QString &tool, const QJsonObject &args);
    QJsonObject capture(const QJsonObject &args);

private:
    QJsonObject opListClips() const;
    QJsonObject opListSourceKinds() const;
    QJsonObject opListCameras() const;
    QJsonObject opListNdiSources() const;
    QJsonObject opAddClip(const QJsonObject &args);
    QJsonObject opAddSource(const QJsonObject &args);
    QJsonObject opRemoveClip(const QJsonObject &args);
    QJsonObject opRenameClip(const QJsonObject &args);
    QJsonObject opSetText(const QJsonObject &args);
    QJsonObject opSetSvgParams(const QJsonObject &args);
    QJsonObject opSetShader(const QJsonObject &args);
    QJsonObject opSelectDeck(const QJsonObject &args, bool deckA);
    QJsonObject opPlayDeck(bool deckA, bool play);
    QJsonObject opSeekDeck(const QJsonObject &args, bool deckA);
    QJsonObject opSetSpeed(const QJsonObject &args);
    QJsonObject opSetFader(const QJsonObject &args);
    QJsonObject opCut();
    QJsonObject opAuto();
    QJsonObject opListTransitions() const;
    QJsonObject opSetTransition(const QJsonObject &args);
    QJsonObject opSetDuration(const QJsonObject &args);
    QJsonObject opSetPanic(const QJsonObject &args);
    QJsonObject opStartRecording(const QJsonObject &args);
    QJsonObject opStopRecording();
    QJsonObject opRecordingStatus() const;
    QJsonObject opSetNdi(const QJsonObject &args);
    QJsonObject opSetVirtualCamera(const QJsonObject &args);
    QJsonObject opSaveSession(const QJsonObject &args);
    QJsonObject opLoadSession(const QJsonObject &args);
    QJsonObject opListProcessEffects() const;
    QJsonObject opListNodes() const;
    QJsonObject opAddProcessNode(const QJsonObject &args);
    QJsonObject opAddLayerNode(const QJsonObject &args);
    QJsonObject opAddAbSelect(const QJsonObject &args);
    QJsonObject opConnect(const QJsonObject &args);
    QJsonObject opDisconnect(const QJsonObject &args);
    QJsonObject opSetProcessParams(const QJsonObject &args);
    QJsonObject opRemoveNode(const QJsonObject &args);

    MainWindow *m_window = nullptr;
};

} // namespace prism::mcp
