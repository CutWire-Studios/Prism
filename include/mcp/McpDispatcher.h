#pragma once

#include "core/Deck.h"

#include <QJsonObject>
#include <QString>

class AppController;

namespace prism::mcp {

class McpDispatcher
{
public:
    explicit McpDispatcher(AppController *app);

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
    QJsonObject opSetSceneSource(const QJsonObject &args);
    static bool parseDeck(const QJsonObject &args, Deck &deck, QJsonObject *error);
    QJsonObject opListScenes() const;
    QJsonObject opStageScene(const QJsonObject &args);
    QJsonObject opAddScene(const QJsonObject &args);
    QJsonObject opEnterScene(const QJsonObject &args);
    QJsonObject opTakeScene(const QJsonObject &args);
    QJsonObject opRemoveScene(const QJsonObject &args);
    QJsonObject opReorderScene(const QJsonObject &args);
    QJsonObject opDuplicateScene(const QJsonObject &args);
    QJsonObject opExitScene();
    QJsonObject opSetSceneProps(const QJsonObject &args);
    QJsonObject opSetDefaultTransition(const QJsonObject &args);
    QJsonObject opPreflight() const;
    QJsonObject opPlayDeck(const QJsonObject &args, bool play);
    QJsonObject opSeekDeck(const QJsonObject &args);
    QJsonObject opSetSpeed(const QJsonObject &args);
    QJsonObject opSetFader(const QJsonObject &args);
    QJsonObject opCut();
    QJsonObject opTake();
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
    QJsonObject opConnect(const QJsonObject &args);
    QJsonObject opDisconnect(const QJsonObject &args);
    QJsonObject opSetProcessParams(const QJsonObject &args);
    QJsonObject opRemoveNode(const QJsonObject &args);

    AppController *m_window = nullptr;
};

} // namespace prism::mcp
