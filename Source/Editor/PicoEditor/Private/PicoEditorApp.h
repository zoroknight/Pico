#pragma once

#include "Pico/Object/ObjectTypes.h"

#include <string>

namespace Pico
{
class PActor;
class PActorComponent;
class PLevel;
class PObject;
class PProperty;
class PSceneComponent;
class PWorld;

class FPicoEditorApp
{
public:
    explicit FPicoEditorApp(PWorld* World);

    void Draw();

private:
    PWorld* GetWorld() const;
    PObject* GetSelectedObject() const;

    void DrawToolbar();
    void DrawSceneOutliner();
    void DrawLevelNode(PLevel* Level);
    void DrawActorNode(PActor* Actor);
    void DrawComponentNode(PActorComponent* Component, PActor* Owner);
    void DrawDetails();
    void DrawObjectIdentity(PObject* Object);
    void DrawActorDetails(PActor* Actor);
    void DrawSceneComponentDetails(PSceneComponent* Component);
    void DrawReflectedProperties(PObject* Object);
    void DrawPropertyEditor(PObject* Object, const PProperty* Property);
    void DrawStatusBar();

    PActor* CreateActorWithRoot(std::string Name);
    PSceneComponent* AddSceneRoot(PActor* Actor);
    void SpawnActor();
    void AddRootToSelectedActor();
    void SetSelectedComponentAsRoot();
    void DestroySelectedObject();
    void Select(PObject* Object);
    void SetStatus(std::string Message, bool bIsError = false);

    FObjectHandle WorldHandle;
    FObjectHandle SelectedObjectHandle;
    unsigned int NextActorNumber = 1;
    std::string Status;
    bool bStatusIsError = false;
};
}
