#pragma once

#include "Pico/Object/ObjectTypes.h"

#include <functional>

namespace Pico
{
class FEditorCommandQueue;
class FEditorCommandService;
class FEditorSelection;
class PActor;
class PActorComponent;
class PLevel;
class PObject;
class PWorld;
struct FEditorCommandResult;

class FSceneOutlinerPanel
{
public:
    using FSelectObject = std::function<void(PObject*)>;
    using FBeginRename = std::function<void(PObject*)>;
    using FApplyResult = std::function<void(FEditorCommandResult)>;

    void Draw(
        PWorld* World,
        FEditorSelection& Selection,
        FEditorCommandService& Commands,
        FEditorCommandQueue& Queue,
        FSelectObject SelectObject,
        FBeginRename BeginRename,
        FApplyResult ApplyResult);

private:
    void DrawLevelNode(PLevel* Level);
    void DrawActorNode(PActor* Actor);
    void DrawComponentNode(PActorComponent* Component, PActor* Owner);
    void DrawActorContextMenu(PActor* Actor);
    void DrawComponentContextMenu(PActorComponent* Component);
    PObject* GetSelectedObject() const;
    void Select(PObject* Object);
    void SpawnEmptyActor();
    void SpawnCubeActor();
    void AddSceneComponentToSelection();
    void AddCubeComponentToSelection();
    void DestroySelectedObject();
    void QueueDestroy(PObject* Object);
    void CopySelectedObject();
    void PasteClipboard();
    bool CanPasteClipboard() const;
    void BeginRenameObject(PObject* Object);

    PWorld* World = nullptr;
    FEditorSelection* Selection = nullptr;
    FEditorCommandService* Commands = nullptr;
    FEditorCommandQueue* Queue = nullptr;
    FSelectObject SelectObject;
    FBeginRename RequestRename;
    FApplyResult ApplyResult;
};
}
