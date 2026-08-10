#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/AssetDependencyService.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/DirectionalLightComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/PointLightComponent.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectName.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
FEditorCommandResult Success(std::string Message)
{
    return { true, std::move(Message) };
}

FEditorCommandResult Failure(std::string Message)
{
    return { false, std::move(Message) };
}

const char* GetComponentTypeLabel(EEditorSceneComponentType Type)
{
    switch (Type)
    {
    case EEditorSceneComponentType::Camera: return "Camera";
    case EEditorSceneComponentType::SpringArm: return "Spring Arm";
    case EEditorSceneComponentType::DirectionalLight: return "Directional Light";
    case EEditorSceneComponentType::PointLight: return "Point Light";
    }
    return "Component";
}
}

FEditorCommandService::FEditorCommandService(
    FEngineLoop* InEngineLoop,
    FEditorSelection* InSelection,
    FEditorTransactionManager* InTransactions,
    FEditorSceneClipboard* InClipboard,
    std::function<void()> InOnWorldChanged)
    : EngineLoop(InEngineLoop)
    , Selection(InSelection)
    , Transactions(InTransactions)
    , Clipboard(InClipboard)
    , OnWorldChanged(std::move(InOnWorldChanged))
{
}

PWorld* FEditorCommandService::GetWorld() const
{
    return EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
}

FEditorCommandResult FEditorCommandService::ValidateGameplayForPlay() const
{
    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        return Failure("Gameplay validation requires an active World");
    }

    std::unordered_set<int32> PlayerStartIds;
    std::size_t PlayerStartCount = 0;
    bool bHasDuplicateId = false;
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr || !Actor->IsA(PPlayerStart::StaticClass()))
            {
                continue;
            }
            const auto* PlayerStart = static_cast<const PPlayerStart*>(Actor);
            ++PlayerStartCount;
            if (PlayerStart->GetRootComponent() == nullptr)
            {
                return Failure(
                    "PlayerStart '" + PlayerStart->GetName().ToString()
                    + "' has no scene root");
            }
            if (!PlayerStartIds.insert(PlayerStart->GetPlayerStartId()).second)
            {
                bHasDuplicateId = true;
            }
        }
    }
    if (PlayerStartCount == 0)
    {
        return Success(
            "Gameplay warning: no PlayerStart; runtime will use the world origin");
    }
    if (bHasDuplicateId)
    {
        return Success(
            "Gameplay warning: duplicate PlayerStartId values; first valid start wins");
    }
    return Success(
        "Gameplay validation passed ("
        + std::to_string(PlayerStartCount) + " PlayerStart(s))");
}

PActor* FEditorCommandService::CreateActor(std::string Name, bool bCubeActor)
{
    PWorld* World = GetWorld();
    PActor* Actor = World != nullptr ? World->SpawnActor<PActor>(Name) : nullptr;
    if (Actor == nullptr)
    {
        return nullptr;
    }
    if (bCubeActor)
    {
        PCubeComponent* Cube = Actor->CreateComponent<PCubeComponent>("CubeComponent");
        if (Cube == nullptr || !Actor->SetRootComponent(Cube))
        {
            World->DestroyActor(Actor);
            return nullptr;
        }
    }
    else if (CreateSceneRoot(Actor) == nullptr)
    {
        World->DestroyActor(Actor);
        return nullptr;
    }
    return Actor;
}

PSceneComponent* FEditorCommandService::CreateSceneRoot(PActor* Actor)
{
    if (Actor == nullptr || Actor->GetRootComponent() != nullptr)
    {
        return nullptr;
    }
    PSceneComponent* Root = Actor->CreateComponent<PSceneComponent>("DefaultSceneRoot");
    if (Root == nullptr || !Actor->SetRootComponent(Root))
    {
        if (Root != nullptr)
        {
            Actor->DestroyComponent(Root);
        }
        return nullptr;
    }
    return Root;
}

PActor* FEditorCommandService::CreateStaticMeshActor(
    std::string Name,
    const FAssetPath& AssetPath)
{
    PWorld* World = GetWorld();
    PActor* Actor = World != nullptr ? World->SpawnActor<PActor>(Name) : nullptr;
    if (Actor == nullptr)
    {
        return nullptr;
    }
    PStaticMeshComponent* Component =
        Actor->CreateComponent<PStaticMeshComponent>("StaticMeshComponent");
    if (Component == nullptr || !Actor->SetRootComponent(Component))
    {
        World->DestroyActor(Actor);
        return nullptr;
    }
    Component->SetStaticMeshAsset(AssetPath);
    return Actor;
}

PSceneComponent* FEditorCommandService::CreateComponent(
    PActor* Actor,
    PSceneComponent* Parent,
    bool bCubeComponent)
{
    if (Actor == nullptr || (Parent != nullptr && Parent->GetOwner() != Actor))
    {
        return nullptr;
    }
    unsigned int& NextNumber = bCubeComponent ? NextCubeComponentNumber : NextComponentNumber;
    const char* Prefix = bCubeComponent ? "CubeComponent_" : "SceneComponent_";
    PSceneComponent* Component = nullptr;
    do
    {
        const std::string Name = Prefix + std::to_string(NextNumber++);
        Component = bCubeComponent
            ? static_cast<PSceneComponent*>(Actor->CreateComponent<PCubeComponent>(Name))
            : Actor->CreateComponent<PSceneComponent>(Name);
    }
    while (Component == nullptr && NextNumber < 10000);

    if (Component == nullptr)
    {
        return nullptr;
    }
    PSceneComponent* Root = Actor->GetRootComponent();
    const bool bConnected = Root == nullptr
        ? Actor->SetRootComponent(Component)
        : Component->AttachToComponent(
            Parent != nullptr ? Parent : Root,
            EAttachmentTransformRule::KeepRelative);
    if (!bConnected)
    {
        Actor->DestroyComponent(Component);
        return nullptr;
    }
    return Component;
}

PSceneComponent* FEditorCommandService::CreateComponent(
    PActor* Actor,
    PSceneComponent* Parent,
    EEditorSceneComponentType Type)
{
    if (Actor == nullptr || (Parent != nullptr && Parent->GetOwner() != Actor))
    {
        return nullptr;
    }

    const PClass* ComponentClass = nullptr;
    const char* Prefix = nullptr;
    unsigned int* NextNumber = nullptr;
    switch (Type)
    {
    case EEditorSceneComponentType::Camera:
        ComponentClass = PCameraComponent::StaticClass();
        Prefix = "CameraComponent_";
        NextNumber = &NextCameraComponentNumber;
        break;
    case EEditorSceneComponentType::SpringArm:
        ComponentClass = PSpringArmComponent::StaticClass();
        Prefix = "SpringArmComponent_";
        NextNumber = &NextSpringArmComponentNumber;
        break;
    case EEditorSceneComponentType::DirectionalLight:
        ComponentClass = PDirectionalLightComponent::StaticClass();
        Prefix = "DirectionalLightComponent_";
        NextNumber = &NextDirectionalLightComponentNumber;
        break;
    case EEditorSceneComponentType::PointLight:
        ComponentClass = PPointLightComponent::StaticClass();
        Prefix = "PointLightComponent_";
        NextNumber = &NextPointLightComponentNumber;
        break;
    }

    PSceneComponent* Component = nullptr;
    do
    {
        PActorComponent* Created = Actor->CreateComponent(
            ComponentClass,
            std::string(Prefix) + std::to_string((*NextNumber)++));
        Component = Created != nullptr
            ? static_cast<PSceneComponent*>(Created) : nullptr;
    }
    while (Component == nullptr && *NextNumber < 10000);

    if (Component == nullptr)
    {
        return nullptr;
    }
    PSceneComponent* Root = Actor->GetRootComponent();
    const bool bConnected = Root == nullptr
        ? Actor->SetRootComponent(Component)
        : Component->AttachToComponent(
            Parent != nullptr ? Parent : Root,
            EAttachmentTransformRule::KeepRelative,
            Type == EEditorSceneComponentType::Camera
                    && Parent != nullptr
                    && Parent->IsA(PSpringArmComponent::StaticClass())
                ? PSpringArmComponent::GetEndpointSocketName()
                : FName {});
    if (!bConnected)
    {
        Actor->DestroyComponent(Component);
        return nullptr;
    }
    return Component;
}

bool FEditorCommandService::BeginTransaction(
    std::string Description,
    EWorldSerializationError& OutError)
{
    PWorld* World = GetWorld();
    return Transactions != nullptr && Selection != nullptr && World != nullptr
        && Transactions->Begin(
            std::move(Description),
            *World,
            Selection->GetObjectPaths(),
            Selection->GetObjectPath(),
            &OutError);
}

bool FEditorCommandService::CommitTransaction(EWorldSerializationError& OutError)
{
    PWorld* World = GetWorld();
    if (Transactions != nullptr && Selection != nullptr && World != nullptr
        && Transactions->Commit(
            *World,
            Selection->GetObjectPaths(),
            Selection->GetObjectPath(),
            &OutError))
    {
        if (OnWorldChanged)
        {
            OnWorldChanged();
        }
        return true;
    }
    EWorldSerializationError RollbackError = EWorldSerializationError::None;
    RollbackTransaction(RollbackError);
    return false;
}

bool FEditorCommandService::RollbackTransaction(EWorldSerializationError& OutError)
{
    return Transactions != nullptr
        && Transactions->Rollback(
            [this](const FEditorWorldSnapshot& Snapshot, EWorldSerializationError* Error)
            {
                return RestoreSnapshot(Snapshot, Error);
            },
            &OutError);
}

bool FEditorCommandService::RestoreSnapshot(
    const FEditorWorldSnapshot& Snapshot,
    EWorldSerializationError* OutError)
{
    if (EngineLoop == nullptr || Selection == nullptr
        || !EngineLoop->ReplaceWorld(Snapshot.WorldData, OutError))
    {
        return false;
    }
    Selection->Restore(
        GetWorld(),
        Snapshot.SelectedObjectPaths,
        Snapshot.PrimaryObjectPath);
    return true;
}

FEditorCommandResult FEditorCommandService::SpawnActor(bool bCubeActor)
{
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(bCubeActor ? "Create Cube Actor" : "Create Empty Actor", Error))
    {
        return Failure("Could not begin create transaction: " + std::string(ToString(Error)));
    }
    unsigned int& Number = bCubeActor ? NextCubeNumber : NextActorNumber;
    const char* Prefix = bCubeActor ? "Cube_" : "Actor_";
    PActor* Actor = nullptr;
    do
    {
        Actor = CreateActor(Prefix + std::to_string(Number++), bCubeActor);
    }
    while (Actor == nullptr && Number < 10000);
    if (Actor == nullptr)
    {
        RollbackTransaction(Error);
        return Failure(bCubeActor ? "Failed to spawn a Cube" : "Failed to spawn an Actor");
    }
    const std::string Path = Actor->GetPathName();
    Selection->Set(Actor);
    if (!CommitTransaction(Error))
    {
        return Failure("Could not commit create transaction: " + std::string(ToString(Error)));
    }
    return Success("Spawned " + Path);
}

FEditorCommandResult FEditorCommandService::SpawnPlayerStart()
{
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Create Player Start", Error))
    {
        return Failure("Could not begin Player Start transaction");
    }

    PWorld* World = GetWorld();
    PPlayerStart* PlayerStart = nullptr;
    do
    {
        PlayerStart = World != nullptr
            ? World->SpawnActor<PPlayerStart>(
                "PlayerStart_" + std::to_string(NextPlayerStartNumber++))
            : nullptr;
    }
    while (PlayerStart == nullptr && NextPlayerStartNumber < 10000);

    if (PlayerStart == nullptr)
    {
        RollbackTransaction(Error);
        return Failure("Failed to spawn a Player Start");
    }
    const std::string Path = PlayerStart->GetPathName();
    Selection->Set(PlayerStart);
    return CommitTransaction(Error)
        ? Success("Spawned " + Path)
        : Failure("Could not commit Player Start transaction");
}

FEditorCommandResult FEditorCommandService::SpawnComponentActor(
    EEditorSceneComponentType Type)
{
    const char* Label = GetComponentTypeLabel(Type);
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(std::string("Create ") + Label, Error))
    {
        return Failure("Could not begin create transaction");
    }

    const char* Prefix = nullptr;
    unsigned int* NextNumber = nullptr;
    switch (Type)
    {
    case EEditorSceneComponentType::Camera:
        Prefix = "Camera_";
        NextNumber = &NextCameraNumber;
        break;
    case EEditorSceneComponentType::SpringArm:
        Prefix = "SpringArm_";
        NextNumber = &NextSpringArmNumber;
        break;
    case EEditorSceneComponentType::DirectionalLight:
        Prefix = "DirectionalLight_";
        NextNumber = &NextDirectionalLightNumber;
        break;
    case EEditorSceneComponentType::PointLight:
        Prefix = "PointLight_";
        NextNumber = &NextPointLightNumber;
        break;
    }

    PWorld* World = GetWorld();
    PActor* Actor = nullptr;
    do
    {
        Actor = World != nullptr
            ? World->SpawnActor<PActor>(
                std::string(Prefix) + std::to_string((*NextNumber)++))
            : nullptr;
    }
    while (Actor == nullptr && *NextNumber < 10000);

    PSceneComponent* Component = CreateComponent(Actor, nullptr, Type);
    if (Component == nullptr)
    {
        if (Actor != nullptr) World->DestroyActor(Actor);
        RollbackTransaction(Error);
        return Failure(std::string("Failed to spawn a ") + Label);
    }
    if (Type == EEditorSceneComponentType::Camera)
    {
        Actor->SetActorLocation(FVector3(-500.0f, 0.0f, 200.0f));
        Actor->SetActorRotation(FRotator(-20.0f, 0.0f, 0.0f));
    }
    else if (Type == EEditorSceneComponentType::PointLight)
    {
        Actor->SetActorLocation(FVector3(0.0f, -200.0f, 250.0f));
    }

    const std::string Path = Actor->GetPathName();
    Selection->Set(Actor);
    return CommitTransaction(Error) ? Success("Spawned " + Path)
                                    : Failure("Could not commit create transaction");
}

FEditorCommandResult FEditorCommandService::SpawnStaticMeshActor(
    const FAssetPath& AssetPath)
{
    if (!AssetPath.IsValid())
    {
        return Failure("Select a valid Static Mesh asset");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Create Static Mesh Actor", Error))
    {
        return Failure("Could not begin create transaction");
    }
    PActor* Actor = nullptr;
    do
    {
        Actor = CreateStaticMeshActor(
            "StaticMesh_" + std::to_string(NextStaticMeshNumber++),
            AssetPath);
    }
    while (Actor == nullptr && NextStaticMeshNumber < 10000);
    if (Actor == nullptr)
    {
        RollbackTransaction(Error);
        return Failure("Failed to spawn a Static Mesh Actor");
    }
    const std::string Path = Actor->GetPathName();
    Selection->Set(Actor);
    return CommitTransaction(Error) ? Success("Spawned " + Path)
                                    : Failure("Could not commit create transaction");
}

FEditorCommandResult FEditorCommandService::AddSceneRoot()
{
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    if (Actor == nullptr)
    {
        return Failure("Select an Actor without a root component");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Add Scene Root", Error))
    {
        return Failure("Could not begin add-root transaction");
    }
    PSceneComponent* Root = CreateSceneRoot(Actor);
    if (Root == nullptr)
    {
        RollbackTransaction(Error);
        return Failure("Selected Actor could not create a scene root");
    }
    const std::string Path = Root->GetPathName();
    Selection->Set(Root);
    return CommitTransaction(Error) ? Success("Added " + Path)
                                    : Failure("Could not commit add-root transaction");
}

FEditorCommandResult FEditorCommandService::AddComponent(bool bCubeComponent)
{
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    PSceneComponent* Parent = Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object) : nullptr;
    if (Actor == nullptr && Parent != nullptr)
    {
        Actor = Parent->GetOwner();
    }
    if (Actor == nullptr)
    {
        return Failure("Select an Actor or SceneComponent before adding a component");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(bCubeComponent ? "Add Cube Component" : "Add Scene Component", Error))
    {
        return Failure("Could not begin add-component transaction");
    }
    PSceneComponent* Component = CreateComponent(Actor, Parent, bCubeComponent);
    if (Component == nullptr)
    {
        RollbackTransaction(Error);
        return Failure("Could not add the selected component type");
    }
    const std::string Path = Component->GetPathName();
    Selection->Set(Component);
    return CommitTransaction(Error) ? Success("Added " + Path)
                                    : Failure("Could not commit add-component transaction");
}

FEditorCommandResult FEditorCommandService::AddComponent(
    EEditorSceneComponentType Type)
{
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    PSceneComponent* Parent = Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object) : nullptr;
    if (Actor == nullptr && Parent != nullptr)
    {
        Actor = Parent->GetOwner();
    }
    if (Actor == nullptr)
    {
        return Failure("Select an Actor or SceneComponent before adding a component");
    }

    const char* Label = GetComponentTypeLabel(Type);
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(std::string("Add ") + Label + " Component", Error))
    {
        return Failure("Could not begin add-component transaction");
    }
    PSceneComponent* Component = CreateComponent(Actor, Parent, Type);
    if (Component == nullptr)
    {
        RollbackTransaction(Error);
        return Failure(std::string("Could not add a ") + Label + " Component");
    }
    const std::string Path = Component->GetPathName();
    Selection->Set(Component);
    return CommitTransaction(Error) ? Success("Added " + Path)
                                    : Failure("Could not commit add-component transaction");
}

FEditorCommandResult FEditorCommandService::AddStaticMeshComponent(
    const FAssetPath& AssetPath)
{
    if (!AssetPath.IsValid())
    {
        return Failure("Select a valid Static Mesh asset");
    }
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    PSceneComponent* Parent = Object != nullptr
        && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object) : nullptr;
    if (Actor == nullptr && Parent != nullptr)
    {
        Actor = Parent->GetOwner();
    }
    if (Actor == nullptr)
    {
        return Failure("Select an Actor or SceneComponent before adding a component");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Add Static Mesh Component", Error))
    {
        return Failure("Could not begin add-component transaction");
    }
    PStaticMeshComponent* Component = nullptr;
    do
    {
        Component = Actor->CreateComponent<PStaticMeshComponent>(
            "StaticMeshComponent_"
            + std::to_string(NextStaticMeshComponentNumber++));
    }
    while (Component == nullptr && NextStaticMeshComponentNumber < 10000);
    const bool bConnected = Component != nullptr
        && (Actor->GetRootComponent() == nullptr
            ? Actor->SetRootComponent(Component)
            : Component->AttachToComponent(
                Parent != nullptr ? Parent : Actor->GetRootComponent(),
                EAttachmentTransformRule::KeepRelative));
    if (!bConnected)
    {
        if (Component != nullptr)
        {
            Actor->DestroyComponent(Component);
        }
        RollbackTransaction(Error);
        return Failure("Could not add a Static Mesh Component");
    }
    Component->SetStaticMeshAsset(AssetPath);
    const std::string Path = Component->GetPathName();
    Selection->Set(Component);
    return CommitTransaction(Error) ? Success("Added " + Path)
                                    : Failure("Could not commit add-component transaction");
}

FEditorCommandResult FEditorCommandService::AssignStaticMeshAsset(
    const FAssetPath& AssetPath)
{
    return SetSelectedStaticMeshAsset(AssetPath, false);
}

FEditorCommandResult FEditorCommandService::ClearStaticMeshAsset()
{
    return SetSelectedStaticMeshAsset({}, true);
}

FEditorCommandResult FEditorCommandService::AssignMaterialAsset(
    const FAssetPath& AssetPath)
{
    return SetSelectedMaterialAsset(AssetPath, false);
}

FEditorCommandResult FEditorCommandService::ClearMaterialAsset()
{
    return SetSelectedMaterialAsset({}, true);
}

FEditorCommandResult FEditorCommandService::ReplaceAssetReferences(
    const FAssetPath& OldAssetPath,
    const FAssetPath& NewAssetPath,
    EAssetType AssetType)
{
    PWorld* World = GetWorld();
    if (World == nullptr || !OldAssetPath.IsValid() || !NewAssetPath.IsValid())
    {
        return Failure("Could not update scene asset references");
    }
    if (AssetType != EAssetType::StaticMesh
        && AssetType != EAssetType::Material)
    {
        return Success("Asset type has no direct scene references");
    }

    const std::size_t UpdatedCount =
        FAssetDependencyService::ReplaceWorldReferences(
            World, OldAssetPath, NewAssetPath);
    if (Transactions != nullptr)
    {
        Transactions->Clear();
    }
    return Success(
        "Updated " + std::to_string(UpdatedCount) + " scene reference(s)");
}

FEditorCommandResult FEditorCommandService::ClearStaticMeshAssetReferences(
    const FAssetPath& AssetPath)
{
    return ClearStaticMeshAssetReferences(std::vector<FAssetPath> {AssetPath});
}

FEditorCommandResult FEditorCommandService::ClearStaticMeshAssetReferences(
    const std::vector<FAssetPath>& AssetPaths)
{
    return ClearAssetReferences(AssetPaths);
}

FEditorCommandResult FEditorCommandService::ClearAssetReferences(
    const std::vector<FAssetPath>& AssetPaths)
{
    PWorld* World = GetWorld();
    if (World == nullptr || AssetPaths.empty())
    {
        return Failure("Select one or more valid assets");
    }
    std::size_t ReferenceCount = 0;
    for (const FAssetPath& AssetPath : AssetPaths)
    {
        ReferenceCount += FAssetDependencyService::FindWorldReferencers(
            World, AssetPath).size();
    }
    if (ReferenceCount == 0)
    {
        return Success("Asset has no scene references");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    const std::string Description = "Clear " + std::to_string(ReferenceCount)
        + " Asset Reference(s)";
    if (!BeginTransaction(Description, Error))
    {
        return Failure("Could not begin reference-cleanup transaction");
    }
    std::size_t ClearedCount = 0;
    for (const FAssetPath& AssetPath : AssetPaths)
    {
        ClearedCount += FAssetDependencyService::ReplaceWorldReferences(
            World, AssetPath, {});
    }
    if (ClearedCount != ReferenceCount)
    {
        RollbackTransaction(Error);
        return Failure("Could not clear every scene asset reference");
    }
    return CommitTransaction(Error)
        ? Success("Cleared " + std::to_string(ClearedCount) + " scene reference(s)")
        : Failure("Could not commit reference-cleanup transaction");
}

FEditorCommandResult FEditorCommandService::SetSelectedStaticMeshAsset(
    const FAssetPath& AssetPath,
    bool bClear)
{
    if (!bClear)
    {
        const FAssetRecord* Record = EngineLoop != nullptr
            ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
        if (Record == nullptr || Record->Type != EAssetType::StaticMesh)
        {
            return Failure("Select a registered Static Mesh asset");
        }
    }
    std::vector<PStaticMeshComponent*> Components;
    if (Selection != nullptr)
    {
        for (PObject* Object : Selection->ResolveAll())
        {
            if (Object->IsA(PStaticMeshComponent::StaticClass()))
            {
                Components.push_back(static_cast<PStaticMeshComponent*>(Object));
            }
        }
    }
    if (Components.empty())
    {
        return Failure("Select one or more StaticMeshComponents");
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    const std::string Description = bClear
        ? "Clear Static Mesh Asset" : "Assign Static Mesh Asset";
    if (!BeginTransaction(Description, Error))
    {
        return Failure("Could not begin asset-assignment transaction");
    }
    for (PStaticMeshComponent* Component : Components)
    {
        Component->SetStaticMeshAsset(AssetPath);
    }
    if (!CommitTransaction(Error))
    {
        return Failure("Could not commit asset-assignment transaction");
    }
    return Success(
        (bClear ? "Cleared asset on " : "Assigned asset to ")
        + std::to_string(Components.size()) + " component(s)");
}

FEditorCommandResult FEditorCommandService::SetSelectedMaterialAsset(
    const FAssetPath& AssetPath,
    bool bClear)
{
    if (!bClear)
    {
        const FAssetRecord* Record = EngineLoop != nullptr
            ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
        if (Record == nullptr || Record->Type != EAssetType::Material)
        {
            return Failure("Select a registered Material asset");
        }
    }
    std::vector<PStaticMeshComponent*> Components;
    if (Selection != nullptr)
    {
        for (PObject* Object : Selection->ResolveAll())
        {
            if (Object->IsA(PStaticMeshComponent::StaticClass()))
            {
                Components.push_back(static_cast<PStaticMeshComponent*>(Object));
            }
        }
    }
    if (Components.empty())
    {
        return Failure("Select one or more StaticMeshComponents");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(
            bClear ? "Clear Material Asset" : "Assign Material Asset", Error))
    {
        return Failure("Could not begin material-assignment transaction");
    }
    for (PStaticMeshComponent* Component : Components)
    {
        Component->SetMaterialAsset(AssetPath);
    }
    if (!CommitTransaction(Error))
    {
        return Failure("Could not commit material-assignment transaction");
    }
    return Success(
        (bClear ? "Cleared material on " : "Assigned material to ")
        + std::to_string(Components.size()) + " component(s)");
}

FEditorCommandResult FEditorCommandService::SetSelectedComponentAsRoot()
{
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PSceneComponent* Component = Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object) : nullptr;
    PActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
    if (Owner == nullptr)
    {
        return Failure("Selected component could not become the root");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Set Root Component", Error) || !Owner->SetRootComponent(Component))
    {
        if (Transactions != nullptr && Transactions->HasPendingTransaction())
        {
            RollbackTransaction(Error);
        }
        return Failure("Selected component could not become the root");
    }
    const std::string Path = Component->GetPathName();
    return CommitTransaction(Error) ? Success("Set " + Path + " as RootComponent")
                                    : Failure("Could not commit root-component transaction");
}

FEditorCommandResult FEditorCommandService::DeleteSelectedObject()
{
    if (Selection == nullptr)
    {
        return Failure("Only Actors and Components can be destroyed");
    }

    std::vector<FObjectHandle> ComponentHandles;
    std::vector<FObjectHandle> ActorHandles;
    for (PObject* Object : Selection->ResolveAll())
    {
        if (Object->IsA(PActor::StaticClass()))
        {
            ActorHandles.push_back(Object->GetHandle());
            continue;
        }
        if (!Object->IsA(PActorComponent::StaticClass()))
        {
            continue;
        }

        PActorComponent* Component = static_cast<PActorComponent*>(Object);
        if (HasAnyFlags(Component->GetFlags(), EObjectFlags::DefaultSubobject))
        {
            return Failure("Default subobjects cannot be deleted");
        }
        if (Selection->Contains(Component->GetOwner()))
        {
            continue;
        }
        bool bAncestorSelected = false;
        if (Component->IsA(PSceneComponent::StaticClass()))
        {
            for (PSceneComponent* Parent =
                     static_cast<PSceneComponent*>(Component)->GetAttachParent();
                 Parent != nullptr;
                 Parent = Parent->GetAttachParent())
            {
                if (Selection->Contains(Parent))
                {
                    bAncestorSelected = true;
                    break;
                }
            }
        }
        if (!bAncestorSelected)
        {
            ComponentHandles.push_back(Object->GetHandle());
        }
    }

    const std::size_t ObjectCount = ComponentHandles.size() + ActorHandles.size();
    if (ObjectCount == 0)
    {
        return Failure("Only Actors and Components can be destroyed");
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    const std::string Description = ObjectCount == 1
        ? "Delete Object" : "Delete " + std::to_string(ObjectCount) + " Objects";
    if (!BeginTransaction(Description, Error))
    {
        return Failure("Could not begin delete transaction");
    }

    for (FObjectHandle Handle : ComponentHandles)
    {
        PObject* Object = ResolveObject(Handle);
        PActorComponent* Component = Object != nullptr
            && Object->IsA(PActorComponent::StaticClass())
            ? static_cast<PActorComponent*>(Object) : nullptr;
        PActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
        if (Owner == nullptr || !Owner->DestroyComponent(Component))
        {
            RollbackTransaction(Error);
            return Failure("Could not destroy the selected Components");
        }
    }
    for (FObjectHandle Handle : ActorHandles)
    {
        PObject* Object = ResolveObject(Handle);
        PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
            ? static_cast<PActor*>(Object) : nullptr;
        PWorld* World = Actor != nullptr ? Actor->GetWorld() : nullptr;
        if (World == nullptr || !World->DestroyActor(Actor))
        {
            RollbackTransaction(Error);
            return Failure("Could not destroy the selected Actors");
        }
    }

    Selection->Set(GetWorld());
    return CommitTransaction(Error)
        ? Success("Destroyed " + std::to_string(ObjectCount) + " object(s)")
                                    : Failure("Could not commit delete transaction");
}

FEditorCommandResult FEditorCommandService::RenameObject(
    FObjectHandle ObjectHandle,
    std::string NewName)
{
    PObject* Object = ResolveObject(ObjectHandle);
    if (Object == nullptr)
    {
        return Failure("The object being renamed no longer exists");
    }
    if (HasAnyFlags(Object->GetFlags(), EObjectFlags::DefaultSubobject))
    {
        return Failure("Default subobjects cannot be renamed");
    }
    if (!IsValidObjectName(NewName))
    {
        return Failure("Names must start with a letter or underscore and contain only letters, numbers, or underscores");
    }
    if (Object->GetName() == FName(NewName))
    {
        return Success("Name unchanged");
    }
    const std::string OldPath = Object->GetPathName();
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Rename " + Object->GetName().ToString(), Error)
        || !Pico::RenameObject(Object, FName(NewName)))
    {
        if (Transactions != nullptr && Transactions->HasPendingTransaction())
        {
            RollbackTransaction(Error);
        }
        return Failure("The name is already used in this object scope");
    }
    const std::string NewPath = Object->GetPathName();
    return CommitTransaction(Error) ? Success("Renamed " + OldPath + " to " + NewPath)
                                    : Failure("Could not commit rename transaction");
}

bool FEditorCommandService::CanCopySelectedObject() const
{
    if (Clipboard == nullptr || Selection == nullptr || Selection->Num() == 0)
    {
        return false;
    }
    const std::vector<PObject*> Objects = Selection->ResolveAll();
    if (Objects.size() != Selection->Num())
    {
        return false;
    }
    const bool bAllActors = std::all_of(
        Objects.begin(),
        Objects.end(),
        [](PObject* Object) { return Object->IsA(PActor::StaticClass()); });
    return bAllActors
        || (Objects.size() == 1
            && Objects.front()->IsA(PSceneComponent::StaticClass()));
}

bool FEditorCommandService::CanPasteClipboard() const
{
    if (Clipboard == nullptr || !Clipboard->HasContent() || GetWorld() == nullptr)
    {
        return false;
    }
    if (Clipboard->GetContentType() == EEditorClipboardContentType::Actor)
    {
        return true;
    }
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    return Object != nullptr && (Object->IsA(PActor::StaticClass())
        || Object->IsA(PSceneComponent::StaticClass()));
}

FEditorCommandResult FEditorCommandService::CopySelectedObject()
{
    PWorld* World = GetWorld();
    if (World == nullptr || Selection == nullptr || !CanCopySelectedObject())
    {
        return Failure("Select one or more Actors, or one SceneComponent, to copy");
    }
    const std::vector<std::string> Paths = Selection->GetObjectPaths();
    EEditorClipboardError Error = EEditorClipboardError::None;
    return Clipboard->Copy(*World, Paths, &Error)
        ? Success("Copied " + std::to_string(Paths.size()) + " object(s)")
        : Failure("Could not copy object: " + std::string(ToString(Error)));
}

FEditorCommandResult FEditorCommandService::PasteClipboard()
{
    PWorld* World = GetWorld();
    if (!CanPasteClipboard() || EngineLoop == nullptr || Selection == nullptr)
    {
        return Failure("Clipboard cannot be pasted at the current selection");
    }
    FWorldAssetData Data;
    std::vector<std::string> PastedPaths;
    EEditorClipboardError ClipboardError = EEditorClipboardError::None;
    if (!Clipboard->BuildPaste(
            *World,
            Selection->GetObjectPath(),
            Data,
            PastedPaths,
            &ClipboardError))
    {
        return Failure("Could not build pasted object: " + std::string(ToString(ClipboardError)));
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(Clipboard->GetContentType() == EEditorClipboardContentType::Actor
            ? "Paste Actor" : "Paste Component", Error)
        || !EngineLoop->ReplaceWorld(Data, &Error))
    {
        if (Transactions != nullptr && Transactions->HasPendingTransaction())
        {
            RollbackTransaction(Error);
        }
        return Failure("Could not paste object: " + std::string(ToString(Error)));
    }
    Selection->Clear();
    for (const std::string& PastedPath : PastedPaths)
    {
        PObject* PastedObject = FindEditorWorldObjectByPath(GetWorld(), PastedPath);
        if (PastedObject == nullptr)
        {
            RollbackTransaction(Error);
            return Failure("Pasted object could not be selected");
        }
        Selection->Add(PastedObject);
    }
    return CommitTransaction(Error)
        ? Success("Pasted " + std::to_string(PastedPaths.size()) + " object(s)")
                                    : Failure("Could not commit paste transaction");
}

FEditorCommandResult FEditorCommandService::Undo()
{
    if (Transactions == nullptr || !Transactions->CanUndo())
    {
        return Failure("Nothing to undo");
    }
    std::string Description;
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!Transactions->Undo(
            [this](const FEditorWorldSnapshot& Snapshot, EWorldSerializationError* RestoreError)
            {
                return RestoreSnapshot(Snapshot, RestoreError);
            },
            &Description,
            &Error))
    {
        return Failure("Undo failed: " + std::string(ToString(Error)));
    }
    if (OnWorldChanged) OnWorldChanged();
    return Success("Undid " + Description);
}

FEditorCommandResult FEditorCommandService::Redo()
{
    if (Transactions == nullptr || !Transactions->CanRedo())
    {
        return Failure("Nothing to redo");
    }
    std::string Description;
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!Transactions->Redo(
            [this](const FEditorWorldSnapshot& Snapshot, EWorldSerializationError* RestoreError)
            {
                return RestoreSnapshot(Snapshot, RestoreError);
            },
            &Description,
            &Error))
    {
        return Failure("Redo failed: " + std::string(ToString(Error)));
    }
    if (OnWorldChanged) OnWorldChanged();
    return Success("Redid " + Description);
}
}
