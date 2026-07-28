#pragma once

#include "Pico/Core/Math/Vector3.h"
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
class FSceneViewportRenderer;

class FPicoEditorApp
{
public:
    FPicoEditorApp(PWorld* World, FSceneViewportRenderer* ViewportRenderer);

    void Draw();

private:
    PWorld* GetWorld() const;
    PObject* GetSelectedObject() const;

    void DrawToolbar();
    void DrawSceneOutliner();
    void DrawViewport(float Width, float Height);
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
    void AddChildToSelectedComponent();
    void SetSelectedComponentAsRoot();
    void DestroySelectedObject();
    void Select(PObject* Object);
    void SetStatus(std::string Message, bool bIsError = false);

    FObjectHandle WorldHandle;
    FObjectHandle SelectedObjectHandle;
    FSceneViewportRenderer* ViewportRenderer = nullptr;
    FVector3 CameraTarget = FVector3::ZeroVector;
    float CameraYawDegrees = -45.0f;
    float CameraPitchDegrees = 28.0f;
    float CameraDistance = 850.0f;
    unsigned int NextActorNumber = 1;
    unsigned int NextComponentNumber = 1;
    std::string Status;
    bool bStatusIsError = false;
};
}
