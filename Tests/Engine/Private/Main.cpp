#include "TestRunner.h"

#include "Pico/Core/App.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/DirectionalLightComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/LightComponent.h"
#include "Pico/Engine/PointLightComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectInitializer.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ObjectSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace
{
class PCountingActor : public Pico::PActor
{
    PICO_DECLARE_CLASS(PCountingActor, Pico::PActor)

public:
    int BeginPlayCount = 0;
    int TickCount = 0;
    int EndPlayCount = 0;
    float LastDeltaSeconds = 0.0f;
    int SpawnedEventCount = 0;
    int DestroyedEventCount = 0;
    bool bSpawnedActorHadBegunPlay = false;
    bool bDestroyedActorWasPending = false;
    bool bDestroyedActorHadEndedPlay = false;
    inline static int TotalEndPlayCount = 0;

    void ObserveActorSpawned(Pico::PActor* Actor)
    {
        ++SpawnedEventCount;
        bSpawnedActorHadBegunPlay = Actor != nullptr
            && Actor->HasBegunPlay()
            && Actor->GetWorld() != nullptr
            && Pico::ResolveObject(Actor->GetHandle()) == Actor;
    }

    void ObserveActorDestroyed(Pico::PActor* Actor)
    {
        ++DestroyedEventCount;
        bDestroyedActorWasPending = Actor != nullptr && Actor->IsPendingDestroy();
        bDestroyedActorHadEndedPlay = Actor != nullptr
            && Actor->IsA(PCountingActor::StaticClass())
            && static_cast<PCountingActor*>(Actor)->EndPlayCount == 1;
    }

    void BeginPlay() override
    {
        ++BeginPlayCount;
    }

    void Tick(float DeltaSeconds) override
    {
        ++TickCount;
        LastDeltaSeconds = DeltaSeconds;
    }

    void EndPlay() override
    {
        ++EndPlayCount;
        ++TotalEndPlayCount;
    }

protected:
    explicit PCountingActor(const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PCountingActor)

class PLoadTrackingActor : public Pico::PActor
{
    PICO_DECLARE_CLASS(PLoadTrackingActor, Pico::PActor)

public:
    inline static int TotalPostLoadCount = 0;
    inline static bool bThrowPostLoad = false;
    inline static bool bObservedRootComponent = false;

protected:
    explicit PLoadTrackingActor(const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
    }

    void PostLoad() override
    {
        ++TotalPostLoadCount;
        bObservedRootComponent = GetRootComponent() != nullptr;
        if (bThrowPostLoad)
        {
            throw std::runtime_error("PostLoad failure requested by test");
        }
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PLoadTrackingActor)

class PStageHActor final : public Pico::PActor
{
    PICO_DECLARE_CLASS(PStageHActor, Pico::PActor)

public:
    void BroadcastValue(Pico::int32 InValue)
    {
        OnValue.Broadcast(InValue);
    }

    void HandleValue(Pico::int32 InValue)
    {
        ++ReceivedCount;
        LastReceivedValue = InValue;
    }

    Pico::TDynamicMulticastDelegate<void(Pico::int32)>& GetOnValue()
    {
        return OnValue;
    }

    Pico::int32 GetValue() const { return Value; }
    Pico::int32 GetReceivedCount() const { return ReceivedCount; }
    Pico::int32 GetLastReceivedValue() const { return LastReceivedValue; }
    Pico::int32 GetPreChangeCount() const { return PreChangeCount; }
    Pico::int32 GetPostChangeCount() const { return PostChangeCount; }
    Pico::EPropertyChangeType GetLastChangeType() const { return LastChangeType; }
    Pico::FName GetLastPropertyName() const { return LastPropertyName; }

    void ResetChangeTracking()
    {
        PreChangeCount = 0;
        PostChangeCount = 0;
        LastPropertyName = {};
        LastChangeType = Pico::EPropertyChangeType::ValueSet;
    }

protected:
    explicit PStageHActor(const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
    }

    void PreEditChange(const Pico::PProperty* Property) override
    {
        ++PreChangeCount;
        LastPropertyName = Property != nullptr ? Property->GetName() : Pico::FName {};
    }

    void PostEditChangeProperty(const Pico::FPropertyChangedEvent& Event) override
    {
        ++PostChangeCount;
        LastPropertyName = Event.Property != nullptr
            ? Event.Property->GetName()
            : Pico::FName {};
        LastChangeType = Event.ChangeType;
    }

private:
    Pico::int32 Value = 10;
    Pico::TDynamicMulticastDelegate<void(Pico::int32)> OnValue;
    Pico::int32 ReceivedCount = 0;
    Pico::int32 LastReceivedValue = 0;
    Pico::int32 PreChangeCount = 0;
    Pico::int32 PostChangeCount = 0;
    Pico::EPropertyChangeType LastChangeType = Pico::EPropertyChangeType::ValueSet;
    Pico::FName LastPropertyName;
};

PICO_DEFINE_CLASS(PStageHActor)

bool PStageHActor::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Value);
    Pico::FPropertyMetadata DelegateMetadata;
    DelegateMetadata.Flags = Pico::EPropertyFlags::Serializable;
    Properties.push_back(Pico::PProperty::Create<&ThisClass::OnValue>(
        Pico::FName("OnValue"), DelegateMetadata));
    if (!Class.AddProperties(std::move(Properties)))
    {
        return false;
    }

    std::vector<Pico::PFunction> Functions;
    PICO_ADD_FUNCTION(
        Functions,
        HandleValue,
        Pico::EFunctionFlags::Callable,
        Pico::FName("Value"));
    return Class.AddFunctions(std::move(Functions));
}

class PSelfDestroyActor : public Pico::PActor
{
    PICO_DECLARE_CLASS(PSelfDestroyActor, Pico::PActor)

public:
    int TickCount = 0;
    int EndPlayCount = 0;
    inline static int TotalTickCount = 0;
    inline static int TotalEndPlayCount = 0;

    void Tick(float) override
    {
        ++TickCount;
        ++TotalTickCount;
        Destroy();
    }

    void EndPlay() override
    {
        ++EndPlayCount;
        ++TotalEndPlayCount;
    }

protected:
    explicit PSelfDestroyActor(const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PSelfDestroyActor)

class PThrowingTickActor : public Pico::PActor
{
    PICO_DECLARE_CLASS(PThrowingTickActor, Pico::PActor)

public:
    void Tick(float) override
    {
        throw std::runtime_error("Tick failure requested by test");
    }

protected:
    explicit PThrowingTickActor(const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PThrowingTickActor)

class PCountingSceneComponent : public Pico::PSceneComponent
{
    PICO_DECLARE_CLASS(PCountingSceneComponent, Pico::PSceneComponent)

public:
    inline static int TotalRegisterCount = 0;
    inline static int TotalUnregisterCount = 0;

protected:
    explicit PCountingSceneComponent(const Pico::FObjectConstructionParams& Params)
        : PSceneComponent(Params)
    {
    }

    void OnRegister() override
    {
        ++TotalRegisterCount;
    }

    void OnUnregister() override
    {
        ++TotalUnregisterCount;
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PCountingSceneComponent)

class PDefaultSubobjectActor : public Pico::PActor
{
    PICO_DECLARE_CLASS(PDefaultSubobjectActor, Pico::PActor)

protected:
    explicit PDefaultSubobjectActor(const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
    }

    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override
    {
        Pico::PSceneComponent* Root =
            Initializer.CreateDefaultSubobject<Pico::PSceneComponent>("TemplateRoot");
        Pico::PCubeComponent* Cube =
            Initializer.CreateDefaultSubobject<Pico::PCubeComponent>("TemplateCube");
        return Root != nullptr
            && Cube != nullptr
            && Initializer.SetRootSubobject(Root)
            && Initializer.AttachSubobject(Cube, Root);
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PDefaultSubobjectActor)

class PDerivedDefaultSubobjectActor final : public PDefaultSubobjectActor
{
    PICO_DECLARE_CLASS(PDerivedDefaultSubobjectActor, PDefaultSubobjectActor)

protected:
    explicit PDerivedDefaultSubobjectActor(const Pico::FObjectConstructionParams& Params)
        : PDefaultSubobjectActor(Params)
    {
    }

    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override
    {
        Pico::PSceneComponent* Root =
            Initializer.CreateDefaultSubobject<Pico::PSceneComponent>("TemplateRoot");
        Pico::PCubeComponent* Cube =
            Initializer.CreateDefaultSubobject<Pico::PCubeComponent>("TemplateCube");
        Pico::PSceneComponent* Child =
            Initializer.CreateDefaultSubobject<Pico::PSceneComponent>("DerivedChild");
        return Root != nullptr
            && Cube != nullptr
            && Child != nullptr
            && Initializer.SetRootSubobject(Root)
            && Initializer.AttachSubobject(Cube, Root)
            && Initializer.AttachSubobject(Child, Root);
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PDerivedDefaultSubobjectActor)

bool InitializeWorldTypes(FTestRunner& Runner)
{
    Pico::PObjectSystem::Shutdown();
    const bool bObjectSystemInitialized = Pico::PObjectSystem::Init();
    Runner.Expect(bObjectSystemInitialized, "World tests initialize the object system");
    if (!bObjectSystemInitialized)
    {
        return false;
    }

    const bool bActorComponentRegistered = Pico::PActorComponent::RegisterClass();
    const bool bSceneComponentRegistered = Pico::PSceneComponent::RegisterClass();
    const bool bCameraComponentRegistered = Pico::PCameraComponent::RegisterClass();
    const bool bLightComponentRegistered = Pico::PLightComponent::RegisterClass();
    const bool bDirectionalLightComponentRegistered =
        Pico::PDirectionalLightComponent::RegisterClass();
    const bool bPointLightComponentRegistered =
        Pico::PPointLightComponent::RegisterClass();
    const bool bSpringArmComponentRegistered =
        Pico::PSpringArmComponent::RegisterClass();
    const bool bPrimitiveComponentRegistered = Pico::PPrimitiveComponent::RegisterClass();
    const bool bCubeComponentRegistered = Pico::PCubeComponent::RegisterClass();
    const bool bStaticMeshComponentRegistered =
        Pico::PStaticMeshComponent::RegisterClass();
    const bool bCountingSceneComponentRegistered = PCountingSceneComponent::RegisterClass();
    const bool bActorRegistered = Pico::PActor::RegisterClass();
    const bool bDefaultSubobjectActorRegistered = PDefaultSubobjectActor::RegisterClass();
    const bool bDerivedDefaultSubobjectActorRegistered =
        PDerivedDefaultSubobjectActor::RegisterClass();
    const bool bCountingActorRegistered = PCountingActor::RegisterClass();
    const bool bLoadTrackingActorRegistered = PLoadTrackingActor::RegisterClass();
    const bool bStageHActorRegistered = PStageHActor::RegisterClass();
    const bool bSelfDestroyActorRegistered = PSelfDestroyActor::RegisterClass();
    const bool bThrowingTickActorRegistered = PThrowingTickActor::RegisterClass();
    const bool bLevelRegistered = Pico::PLevel::RegisterClass();
    const bool bWorldRegistered = Pico::PWorld::RegisterClass();
    Runner.Expect(bActorComponentRegistered, "PActorComponent registers with the class registry");
    Runner.Expect(bSceneComponentRegistered, "PSceneComponent registers with the class registry");
    Runner.Expect(bCameraComponentRegistered, "PCameraComponent registers with the class registry");
    Runner.Expect(bLightComponentRegistered, "PLightComponent registers with the class registry");
    Runner.Expect(
        bDirectionalLightComponentRegistered,
        "PDirectionalLightComponent registers with the class registry");
    Runner.Expect(
        bPointLightComponentRegistered,
        "PPointLightComponent registers with the class registry");
    Runner.Expect(
        bSpringArmComponentRegistered,
        "PSpringArmComponent registers with the class registry");
    Runner.Expect(bPrimitiveComponentRegistered, "PPrimitiveComponent registers with the class registry");
    Runner.Expect(bCubeComponentRegistered, "PCubeComponent registers with the class registry");
    Runner.Expect(
        bStaticMeshComponentRegistered,
        "PStaticMeshComponent registers with the class registry");
    Runner.Expect(bCountingSceneComponentRegistered, "A test scene component registers with the class registry");
    Runner.Expect(bActorRegistered, "PActor registers with the class registry");
    Runner.Expect(
        bDefaultSubobjectActorRegistered,
        "A default-subobject Actor registers with the class registry");
    Runner.Expect(
        bDerivedDefaultSubobjectActorRegistered,
        "A derived default-subobject Actor registers with the class registry");
    Runner.Expect(bCountingActorRegistered, "A test actor registers with the class registry");
    Runner.Expect(bLoadTrackingActorRegistered, "A PostLoad test actor registers with the class registry");
    Runner.Expect(bStageHActorRegistered, "The Stage H persistence actor registers with delegate metadata");
    Runner.Expect(bSelfDestroyActorRegistered, "A self-destroying test actor registers with the class registry");
    Runner.Expect(bLevelRegistered, "PLevel registers with the class registry");
    Runner.Expect(bWorldRegistered, "PWorld registers with the class registry");
    return bActorComponentRegistered
        && bSceneComponentRegistered
        && bCameraComponentRegistered
        && bLightComponentRegistered
        && bDirectionalLightComponentRegistered
        && bPointLightComponentRegistered
        && bSpringArmComponentRegistered
        && bPrimitiveComponentRegistered
        && bCubeComponentRegistered
        && bStaticMeshComponentRegistered
        && bCountingSceneComponentRegistered
        && bActorRegistered
        && bDefaultSubobjectActorRegistered
        && bDerivedDefaultSubobjectActorRegistered
        && bCountingActorRegistered
        && bLoadTrackingActorRegistered
        && bStageHActorRegistered
        && bSelfDestroyActorRegistered
        && bThrowingTickActorRegistered
        && bLevelRegistered
        && bWorldRegistered;
}

void TestWorldLifecycle(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "TestWorld");
    Runner.Expect(World != nullptr, "NewObject constructs a reflected PWorld");
    if (World == nullptr)
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Runner.Expect(World->GetState() == Pico::EWorldState::Uninitialized, "A new world starts uninitialized");
    Runner.Expect(World->Initialize(), "World initialization succeeds");
    Runner.Expect(!World->Initialize(), "World initialization cannot run twice");

    Pico::PLevel* PersistentLevel = World->GetPersistentLevel();
    Runner.Expect(PersistentLevel != nullptr, "World initialization creates a persistent level");
    Runner.Expect(World->GetCurrentLevel() == PersistentLevel, "The persistent level is current by default");
    Runner.Expect(PersistentLevel != nullptr && PersistentLevel->GetWorld() == World, "A level resolves its owning world");
    Runner.Expect(
        PersistentLevel != nullptr && PersistentLevel->GetPathName() == "TestWorld.PersistentLevel",
        "The persistent level uses the object outer path");
    Runner.Expect(World->GetLevels().size() == 1, "A new world contains one level");

    Pico::PLevel* GameplayLevel = World->CreateLevel("Gameplay");
    Runner.Expect(GameplayLevel != nullptr, "World creates an additional level");
    Runner.Expect(World->CreateLevel("Gameplay") == nullptr, "Duplicate level names are rejected within one world");
    Runner.Expect(World->GetLevels().size() == 2, "The world exposes both live levels");
    Runner.Expect(World->SetCurrentLevel(GameplayLevel), "An owned level can become current");
    Runner.Expect(World->GetCurrentLevel() == GameplayLevel, "The selected level becomes current");
    Runner.Expect(!World->RemoveLevel(PersistentLevel), "The persistent level cannot be removed");

    const Pico::FObjectHandle GameplayHandle = GameplayLevel != nullptr
        ? GameplayLevel->GetHandle()
        : Pico::FObjectHandle {};
    Runner.Expect(World->RemoveLevel(GameplayLevel), "A non-persistent level can be removed");
    Runner.Expect(Pico::ResolveObject(GameplayHandle) == nullptr, "Removing a level invalidates its object handle");
    Runner.Expect(World->GetCurrentLevel() == PersistentLevel, "Removing the current level falls back to persistent");

    World->Tick(0.25f);
    World->Tick(-1.0f);
    World->Tick(std::numeric_limits<float>::infinity());
    Runner.Expect(World->GetTickCount() == 1, "Only a valid world tick increments the tick count");
    Runner.Expect(std::abs(World->GetTimeSeconds() - 0.25) < 0.000001, "World time accumulates valid delta seconds");

    const Pico::FObjectHandle PersistentLevelHandle = PersistentLevel != nullptr
        ? PersistentLevel->GetHandle()
        : Pico::FObjectHandle {};
    World->TearDown();
    World->TearDown();
    Runner.Expect(World->GetState() == Pico::EWorldState::TornDown, "World teardown is idempotent");
    Runner.Expect(World->GetLevels().empty(), "World teardown destroys all levels");
    Runner.Expect(Pico::ResolveObject(PersistentLevelHandle) == nullptr, "Teardown invalidates the persistent level");
    Runner.Expect(!World->Initialize(), "A torn-down world cannot be initialized again");
    Runner.Expect(World->CreateLevel("LateLevel") == nullptr, "A torn-down world cannot create levels");

    const Pico::FObjectHandle WorldHandle = World->GetHandle();
    Runner.Expect(Pico::DestroyObject(World), "A torn-down world can be destroyed");
    Runner.Expect(Pico::ResolveObject(WorldHandle) == nullptr, "Destroying a world invalidates its handle");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "World lifecycle leaves no registered objects");
    Pico::PObjectSystem::Shutdown();
}

void TestWorldOwnershipAndStaleHandles(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* FirstWorld = Pico::NewObject<Pico::PWorld>(nullptr, "FirstWorld");
    Pico::PWorld* SecondWorld = Pico::NewObject<Pico::PWorld>(nullptr, "SecondWorld");
    const bool bWorldsReady = FirstWorld != nullptr
        && SecondWorld != nullptr
        && FirstWorld->Initialize()
        && SecondWorld->Initialize();
    Runner.Expect(bWorldsReady, "Two independent worlds initialize");

    if (bWorldsReady)
    {
        Pico::PLevel* TemporaryLevel = FirstWorld->CreateLevel("Temporary");
        Runner.Expect(TemporaryLevel != nullptr, "The first world creates a temporary level");
        Runner.Expect(!SecondWorld->SetCurrentLevel(TemporaryLevel), "A world rejects a level owned by another world");
        Runner.Expect(FirstWorld->SetCurrentLevel(TemporaryLevel), "The owning world accepts its level");

        const Pico::FObjectHandle StaleHandle = TemporaryLevel != nullptr
            ? TemporaryLevel->GetHandle()
            : Pico::FObjectHandle {};
        Runner.Expect(Pico::DestroyObject(TemporaryLevel), "A level can be destroyed through the object API");
        Runner.Expect(Pico::ResolveObject(StaleHandle) == nullptr, "The externally destroyed level handle becomes stale");
        Runner.Expect(
            FirstWorld->GetCurrentLevel() == FirstWorld->GetPersistentLevel(),
            "A stale current level falls back to the persistent level");
        Runner.Expect(FirstWorld->GetLevels().size() == 1, "Stale level handles are omitted from world queries");

        Pico::PLevel* ReplacementLevel = FirstWorld->CreateLevel("Replacement");
        Runner.Expect(ReplacementLevel != nullptr, "A replacement level can reuse registry storage");
        Runner.Expect(Pico::ResolveObject(StaleHandle) == nullptr, "Slot reuse does not revive a stale handle");
        Runner.Expect(
            ReplacementLevel != nullptr && ReplacementLevel->GetHandle() != StaleHandle,
            "A reused slot receives a new serial number");
    }

    Pico::DestroyObjectTree(FirstWorld);
    Pico::DestroyObjectTree(SecondWorld);
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Destroying both world trees removes every object");
    Pico::PObjectSystem::Shutdown();
}

void TestActorSpawnLifecycleAndOwnership(FTestRunner& Runner)
{
    PCountingActor::TotalEndPlayCount = 0;

    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "ActorWorld");
    Runner.Expect(World != nullptr && World->Initialize(), "An actor test world initializes");
    if (World == nullptr)
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    PCountingActor* Hero = World->SpawnActor<PCountingActor>("Hero");
    Runner.Expect(Hero != nullptr, "World spawns a reflected actor into the current level");
    Runner.Expect(Hero != nullptr && Hero->GetWorld() == World, "A spawned actor resolves its owning world");
    Runner.Expect(
        Hero != nullptr && Hero->GetLevel() == World->GetCurrentLevel(),
        "A spawned actor is outered to the current level");
    Runner.Expect(
        Hero != nullptr && Hero->GetPathName() == "ActorWorld.PersistentLevel.Hero",
        "A spawned actor uses the level object path");
    Runner.Expect(World->GetCurrentLevel()->GetActors().size() == 1, "The level exposes its spawned actor");
    Runner.Expect(
        World->SpawnActor(Pico::PLevel::StaticClass(), "NotAnActor") == nullptr,
        "World rejects spawning a non-actor class");
    Runner.Expect(World->SpawnActor<PCountingActor>("Hero") == nullptr, "Duplicate actor names are rejected per level");

    Pico::FActorSpawnParameters OwnedSpawnParameters;
    OwnedSpawnParameters.Name = Pico::FName("OwnedActor");
    OwnedSpawnParameters.OverrideLevel = World->GetPersistentLevel();
    OwnedSpawnParameters.Owner = Hero;
    OwnedSpawnParameters.ObjectFlags = Pico::EObjectFlags::Transient;
    PCountingActor* OwnedActor = World->SpawnActor<PCountingActor>(OwnedSpawnParameters);
    Runner.Expect(
        OwnedActor != nullptr
            && OwnedActor->GetLevel() == World->GetPersistentLevel()
            && OwnedActor->GetOwner() == Hero
            && Pico::HasAnyFlags(OwnedActor->GetFlags(), Pico::EObjectFlags::Transient),
        "Structured spawn parameters select Level, Owner, and object flags");
    Runner.Expect(
        OwnedActor != nullptr && World->DestroyActor(OwnedActor),
        "An actor created through structured spawn parameters follows normal destruction");

    World->Tick(0.5f);
    Runner.Expect(Hero->BeginPlayCount == 1, "The first world tick begins play for existing actors");
    Runner.Expect(Hero->TickCount == 1, "The first world tick ticks begun actors");
    Runner.Expect(std::abs(Hero->LastDeltaSeconds - 0.5f) < 0.000001f, "Actor tick receives world delta seconds");

    PCountingActor* LateActor = World->SpawnActor<PCountingActor>("LateActor");
    Runner.Expect(
        LateActor != nullptr && LateActor->BeginPlayCount == 1,
        "Actors spawned after BeginPlay begin immediately");
    const Pico::FObjectHandle LateActorHandle = LateActor != nullptr
        ? LateActor->GetHandle()
        : Pico::FObjectHandle {};
    const int EndPlayCountBeforeDestroy = PCountingActor::TotalEndPlayCount;
    Runner.Expect(World->DestroyActor(LateActor), "World destroys an owned actor");
    Runner.Expect(
        PCountingActor::TotalEndPlayCount == EndPlayCountBeforeDestroy + 1,
        "Destroying an actor dispatches EndPlay");
    Runner.Expect(Pico::ResolveObject(LateActorHandle) == nullptr, "Destroyed actor handles become invalid");
    Runner.Expect(World->GetCurrentLevel()->GetActors().size() == 1, "Destroyed actors leave the level actor list");

    const Pico::FObjectHandle HeroHandle = Hero->GetHandle();
    Pico::DestroyObject(Hero);
    Runner.Expect(Pico::ResolveObject(HeroHandle) == nullptr, "Externally destroyed actor handles become stale");
    Runner.Expect(World->GetCurrentLevel()->GetActors().empty(), "Stale actor handles are omitted from level queries");

    Pico::DestroyObjectTree(World);
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Actor lifecycle leaves no registered objects");
    Pico::PObjectSystem::Shutdown();
}

void TestActorLifecycleDelegates(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "DelegateWorld");
    PCountingActor* Listener =
        World != nullptr && World->Initialize()
        ? World->SpawnActor<PCountingActor>("DelegateListener")
        : nullptr;
    if (World != nullptr)
    {
        World->Tick(0.0f);
    }

    const Pico::FDelegateHandle SpawnListenerHandle = Listener != nullptr
        ? World->OnActorSpawned().AddObject(
            Listener,
            &PCountingActor::ObserveActorSpawned)
        : Pico::FDelegateHandle {};
    const Pico::FDelegateHandle ThrowingSpawnHandle = World != nullptr
        ? World->OnActorSpawned().AddLambda(
            [](Pico::PActor*)
            {
                throw std::runtime_error("spawn delegate test");
            })
        : Pico::FDelegateHandle {};

    PCountingActor* Subject = World != nullptr
        ? World->SpawnActor<PCountingActor>("DelegateSubject")
        : nullptr;
    Runner.Expect(
        SpawnListenerHandle.IsValid()
            && ThrowingSpawnHandle.IsValid()
            && Subject != nullptr
            && Listener != nullptr
            && Listener->SpawnedEventCount == 1
            && Listener->bSpawnedActorHadBegunPlay,
        "World spawn delegates observe a complete begun-play Actor and cannot fail spawning");

    if (World != nullptr)
    {
        World->OnActorSpawned().Remove(ThrowingSpawnHandle);
    }
    const Pico::FDelegateHandle DestroyListenerHandle =
        Subject != nullptr && Listener != nullptr
        ? Subject->OnDestroyed().AddObject(
            Listener,
            &PCountingActor::ObserveActorDestroyed)
        : Pico::FDelegateHandle {};
    const Pico::FDelegateHandle ThrowingDestroyHandle = Subject != nullptr
        ? Subject->OnDestroyed().AddLambda(
            [](Pico::PActor*)
            {
                throw std::runtime_error("destroy delegate test");
            })
        : Pico::FDelegateHandle {};
    const Pico::FObjectHandle SubjectHandle = Subject != nullptr
        ? Subject->GetHandle()
        : Pico::FObjectHandle {};
    Runner.Expect(
        DestroyListenerHandle.IsValid()
            && ThrowingDestroyHandle.IsValid()
            && World != nullptr
            && World->DestroyActor(Subject)
            && Pico::ResolveObject(SubjectHandle) == nullptr
            && Listener->DestroyedEventCount == 1
            && Listener->bDestroyedActorWasPending
            && Listener->bDestroyedActorHadEndedPlay,
        "Actor destruction delegates run once after pending-destroy and EndPlay state is committed");

    const Pico::FObjectHandle ListenerHandle = Listener != nullptr
        ? Listener->GetHandle()
        : Pico::FObjectHandle {};
    Runner.Expect(
        World != nullptr
            && World->DestroyActor(Listener)
            && Pico::ResolveObject(ListenerHandle) == nullptr,
        "The weak lifecycle listener can be destroyed independently");
    PCountingActor* AfterListenerDestroy = World != nullptr
        ? World->SpawnActor<PCountingActor>("AfterListenerDestroy")
        : nullptr;
    Runner.Expect(
        AfterListenerDestroy != nullptr
            && World->OnActorSpawned().Num() == 0,
        "World events skip and compact bindings whose object listener was destroyed");

    Pico::DestroyObjectTree(World);
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Lifecycle delegate integration leaves no registered objects");
    Pico::PObjectSystem::Shutdown();
}

void TestActorDestroyDuringTick(FTestRunner& Runner)
{
    PSelfDestroyActor::TotalTickCount = 0;
    PSelfDestroyActor::TotalEndPlayCount = 0;

    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "DestroyDuringTickWorld");
    Runner.Expect(World != nullptr && World->Initialize(), "A destroy-during-tick world initializes");
    if (World == nullptr)
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    PSelfDestroyActor* SelfDestroyingActor = World->SpawnActor<PSelfDestroyActor>("SelfDestroyingActor");
    Runner.Expect(SelfDestroyingActor != nullptr, "World spawns a self-destroying actor");
    const Pico::FObjectHandle ActorHandle = SelfDestroyingActor != nullptr
        ? SelfDestroyingActor->GetHandle()
        : Pico::FObjectHandle {};

    World->Tick(0.25f);
    Runner.Expect(PSelfDestroyActor::TotalTickCount == 1, "The self-destroying actor ticks once");
    Runner.Expect(PSelfDestroyActor::TotalEndPlayCount == 1, "Self-destroy during tick dispatches EndPlay once");
    Runner.Expect(Pico::ResolveObject(ActorHandle) == nullptr, "Self-destroy during tick releases the actor at frame end");
    Runner.Expect(World->GetCurrentLevel()->GetActors().empty(), "Self-destroy removes the actor from its level");

    Pico::DestroyObjectTree(World);
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Destroy-during-tick leaves no registered objects");
    Pico::PObjectSystem::Shutdown();
}

void TestTickExceptionSafety(FTestRunner& Runner)
{
    char Program[] = "PicoTickExceptionTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, MaxFPS };
    Pico::FEngineLoop EngineLoop;
    Runner.Expect(
        EngineLoop.PreInit(2, Arguments) == 0
            && EngineLoop.Init() == 0
            && PCountingActor::RegisterClass()
            && PThrowingTickActor::RegisterClass(),
        "Tick exception test initializes the engine loop");

    Pico::PWorld* World = EngineLoop.GetWorld();
    PThrowingTickActor* ThrowingActor = World != nullptr
        ? World->SpawnActor<PThrowingTickActor>("ThrowingTickActor")
        : nullptr;
    PCountingActor* Survivor = World != nullptr
        ? World->SpawnActor<PCountingActor>("TickSurvivor")
        : nullptr;
    const Pico::FObjectHandle SurvivorHandle = Survivor != nullptr
        ? Survivor->GetHandle()
        : Pico::FObjectHandle {};
    Pico::FWorldAssetData Snapshot;
    Pico::EWorldSerializationError Error = Pico::EWorldSerializationError::None;
    Runner.Expect(
        ThrowingActor != nullptr
            && Survivor != nullptr
            && Pico::CaptureWorld(*World, Snapshot, &Error),
        "Tick exception test captures a replacement snapshot");

    bool bCaughtException = false;
    try
    {
        EngineLoop.Tick();
    }
    catch (const std::runtime_error&)
    {
        bCaughtException = true;
    }

    Runner.Expect(
        bCaughtException
            && World->DestroyActor(Survivor)
            && Pico::ResolveObject(SurvivorHandle) == nullptr,
        "World Tick restores its ticking flag before propagating an exception");
    Runner.Expect(
        EngineLoop.ReplaceWorld(Snapshot, &Error),
        "EngineLoop restores its world-ticking flag before propagating an exception");

    EngineLoop.Exit();
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Tick exception test exits without leaking objects");
}

void TestActorComponentsAndSceneTransform(FTestRunner& Runner)
{
    PCountingSceneComponent::TotalRegisterCount = 0;
    PCountingSceneComponent::TotalUnregisterCount = 0;

    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "ComponentWorld");
    Runner.Expect(World != nullptr && World->Initialize(), "A component test world initializes");
    if (World == nullptr)
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PActor* Actor = World->SpawnActor<Pico::PActor>("CubeActor");
    Runner.Expect(Actor != nullptr, "World spawns a plain actor for component tests");
    if (Actor == nullptr)
    {
        Pico::DestroyObjectTree(World);
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Runner.Expect(
        Actor->GetActorTransform().Equals(Pico::FTransform::Identity),
        "An actor without a root component has an identity transform");
    Runner.Expect(
        !Actor->SetActorTransform(Pico::FTransform(Pico::FVector3(1.0f, 2.0f, 3.0f))),
        "An actor without a root component rejects transform changes");
    Runner.Expect(
        !Actor->SetActorLocation(Pico::FVector3(1.0f, 2.0f, 3.0f)),
        "An actor without a root component rejects location changes");

    PCountingSceneComponent* Root =
        Actor->CreateComponent<PCountingSceneComponent>("RootComponent");
    Runner.Expect(Root != nullptr, "Actor creates a reflected scene component");
    Runner.Expect(
        Root != nullptr && Root->GetPathName() == "ComponentWorld.PersistentLevel.CubeActor.RootComponent",
        "A component uses the actor as its outer path");
    Runner.Expect(Root != nullptr && Root->GetOwner() == Actor, "A component resolves its owning actor");
    Runner.Expect(Root != nullptr && Root->GetWorld() == World, "A component resolves its owning world");
    Runner.Expect(Actor->GetComponents().size() == 1, "Actor exposes its created component");
    Runner.Expect(Actor->SetRootComponent(Root), "Actor accepts an owned scene component as root");
    Runner.Expect(Actor->GetRootComponent() == Root, "Actor returns its root scene component");
    Runner.Expect(
        Actor->CreateComponent(Pico::PLevel::StaticClass(), "NotAComponent") == nullptr,
        "Actor rejects creating a non-component class");
    Runner.Expect(
        Actor->CreateComponent<PCountingSceneComponent>("RootComponent") == nullptr,
        "Duplicate component names are rejected per actor");

    const Pico::FTransform ExpectedTransform(
        Pico::FRotator(0.0f, 90.0f, 0.0f),
        Pico::FVector3(100.0f, 0.0f, 50.0f),
        Pico::FVector3(1.0f, 2.0f, 1.0f));
    Root->SetRelativeTransform(ExpectedTransform);
    Runner.Expect(
        Root->GetRelativeTransform().Equals(ExpectedTransform, 0.001f),
        "Scene component stores a relative transform");
    Runner.Expect(
        Root->GetWorldTransform().Equals(ExpectedTransform, 0.001f),
        "Without attachment, scene component world transform equals relative transform");
    Runner.Expect(
        Actor->GetActorTransform().Equals(ExpectedTransform, 0.001f),
        "Actor transform is provided by its root component");

    const Pico::FTransform ActorTransform(
        Pico::FRotator(15.0f, 30.0f, 5.0f),
        Pico::FVector3(25.0f, 50.0f, 75.0f),
        Pico::FVector3(1.5f, 2.0f, 0.5f));
    Runner.Expect(Actor->SetActorTransform(ActorTransform), "Actor sets its root world transform");
    Runner.Expect(
        Root->GetWorldTransform().Equals(ActorTransform, 0.001f),
        "Setting actor transform updates the root component");

    Runner.Expect(
        Actor->SetActorLocation(Pico::FVector3(120.0f, 10.0f, 25.0f)),
        "Actor exposes a location setter");
    Runner.Expect(
        Actor->SetActorRotation(Pico::FRotator(10.0f, 45.0f, 5.0f)),
        "Actor exposes a rotation setter");
    Runner.Expect(
        Actor->SetActorScale(Pico::FVector3(2.0f, 2.0f, 0.5f)),
        "Actor exposes a scale setter");
    Runner.Expect(
        Actor->GetActorLocation().Equals(Pico::FVector3(120.0f, 10.0f, 25.0f)),
        "Actor location is read from the root component");
    Runner.Expect(
        Actor->GetActorRotation().Equals(Pico::FRotator(10.0f, 45.0f, 5.0f), 0.001f),
        "Actor rotation is read from the root component");
    Runner.Expect(
        Actor->GetActorScale().Equals(Pico::FVector3(2.0f, 2.0f, 0.5f)),
        "Actor scale is read from the root component");

    Root->SetRelativeLocation(Pico::FVector3(120.0f, 10.0f, 25.0f));
    Root->SetRelativeRotation(Pico::FRotator(10.0f, 45.0f, 5.0f));
    Root->SetRelativeScale(Pico::FVector3(2.0f, 2.0f, 0.5f));
    Runner.Expect(
        Root->GetRelativeLocation().Equals(Pico::FVector3(120.0f, 10.0f, 25.0f)),
        "Scene component exposes location setters");
    Runner.Expect(
        Root->GetRelativeRotation().Equals(Pico::FRotator(10.0f, 45.0f, 5.0f), 0.001f),
        "Scene component exposes rotation setters");
    Runner.Expect(
        Root->GetRelativeScale().Equals(Pico::FVector3(2.0f, 2.0f, 0.5f)),
        "Scene component exposes scale setters");

    Pico::PActor* StaleRootActor = World->SpawnActor<Pico::PActor>("StaleRootActor");
    Pico::PSceneComponent* StaleRoot = StaleRootActor != nullptr
        ? StaleRootActor->CreateComponent<Pico::PSceneComponent>("RootComponent")
        : nullptr;
    Runner.Expect(
        StaleRootActor != nullptr &&
            StaleRoot != nullptr &&
            StaleRootActor->SetRootComponent(StaleRoot),
        "A second actor accepts a root component");
    Runner.Expect(
        StaleRoot != nullptr && Pico::DestroyObject(StaleRoot),
        "An independently destroyed root component is released");
    Runner.Expect(
        StaleRootActor != nullptr && StaleRootActor->GetRootComponent() == nullptr,
        "A stale root handle resolves to null");
    Runner.Expect(
        StaleRootActor != nullptr &&
            StaleRootActor->GetActorTransform().Equals(Pico::FTransform::Identity),
        "An actor with a stale root handle falls back to identity");
    Runner.Expect(
        StaleRootActor != nullptr &&
            !StaleRootActor->SetActorLocation(Pico::FVector3(1.0f, 2.0f, 3.0f)),
        "An actor with a stale root handle rejects transform changes");

    Pico::PSceneComponent* TreeRoot = StaleRootActor != nullptr
        ? StaleRootActor->CreateComponent<Pico::PSceneComponent>("TreeRoot")
        : nullptr;
    Pico::PSceneComponent* TreeMiddle = StaleRootActor != nullptr
        ? StaleRootActor->CreateComponent<Pico::PSceneComponent>("TreeMiddle")
        : nullptr;
    Pico::PCubeComponent* TreeLeaf = StaleRootActor != nullptr
        ? StaleRootActor->CreateComponent<Pico::PCubeComponent>("TreeLeaf")
        : nullptr;
    const bool bDestroyTreeReady =
        StaleRootActor != nullptr
        && TreeRoot != nullptr
        && TreeMiddle != nullptr
        && TreeLeaf != nullptr
        && StaleRootActor->SetRootComponent(TreeRoot)
        && TreeMiddle->AttachToComponent(
            TreeRoot,
            Pico::EAttachmentTransformRule::KeepRelative)
        && TreeLeaf->AttachToComponent(
            TreeMiddle,
            Pico::EAttachmentTransformRule::KeepRelative);
    Runner.Expect(bDestroyTreeReady, "An Actor creates a component subtree for deletion");
    if (bDestroyTreeReady)
    {
        const Pico::FObjectHandle TreeRootHandle = TreeRoot->GetHandle();
        const Pico::FObjectHandle TreeMiddleHandle = TreeMiddle->GetHandle();
        const Pico::FObjectHandle TreeLeafHandle = TreeLeaf->GetHandle();
        Runner.Expect(
            StaleRootActor->DestroyComponent(TreeRoot),
            "Actor component deletion destroys the selected scene subtree");
        Runner.Expect(
            StaleRootActor->GetRootComponent() == nullptr
                && StaleRootActor->GetComponents().empty(),
            "Deleting a root subtree clears Actor component and root handles");
        Runner.Expect(
            Pico::ResolveObject(TreeRootHandle) == nullptr
                && Pico::ResolveObject(TreeMiddleHandle) == nullptr
                && Pico::ResolveObject(TreeLeafHandle) == nullptr,
            "Deleting a component subtree invalidates every descendant handle");
    }

    World->Tick(0.1f);
    Runner.Expect(Root->IsRegistered(), "Actor BeginPlay registers owned components");
    Runner.Expect(PCountingSceneComponent::TotalRegisterCount == 1, "Component OnRegister runs once");

    PCountingSceneComponent* LateComponent =
        Actor->CreateComponent<PCountingSceneComponent>("LateComponent");
    Runner.Expect(
        LateComponent != nullptr && LateComponent->IsRegistered(),
        "Components created after BeginPlay register immediately");
    Runner.Expect(PCountingSceneComponent::TotalRegisterCount == 2, "Late component OnRegister runs once");

    const Pico::FObjectHandle RootHandle = Root->GetHandle();
    const Pico::FObjectHandle LateHandle = LateComponent != nullptr
        ? LateComponent->GetHandle()
        : Pico::FObjectHandle {};
    Runner.Expect(World->DestroyActor(Actor), "Destroying an actor with components succeeds");
    Runner.Expect(
        PCountingSceneComponent::TotalUnregisterCount == 2,
        "Destroying an actor unregisters owned components");
    Runner.Expect(Pico::ResolveObject(RootHandle) == nullptr, "Destroying an actor invalidates root component handle");
    Runner.Expect(Pico::ResolveObject(LateHandle) == nullptr, "Destroying an actor invalidates all component handles");

    Pico::DestroyObjectTree(World);
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Component lifecycle leaves no registered objects");
    Pico::PObjectSystem::Shutdown();
}

void TestDefaultSubobjectTemplatesAndWorldRestore(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    const Pico::PClass* BaseClass = PDefaultSubobjectActor::StaticClass();
    const Pico::PClass* DerivedClass = PDerivedDefaultSubobjectActor::StaticClass();
    const std::vector<Pico::FDefaultSubobjectRecord>& BaseRecords =
        BaseClass->GetDefaultSubobjects();
    const std::vector<Pico::FDefaultSubobjectRecord>& DerivedRecords =
        DerivedClass->GetDefaultSubobjects();
    Runner.Expect(BaseRecords.size() == 2, "A base Actor class owns two default-subobject templates");
    Runner.Expect(
        DerivedRecords.size() == 3,
        "A derived Actor class inherits default subobjects and adds one template");

    const auto FindRecord = [](
        const std::vector<Pico::FDefaultSubobjectRecord>& Records,
        std::string_view Name) -> const Pico::FDefaultSubobjectRecord*
    {
        const Pico::FName ObjectName(Name);
        const auto Found = std::find_if(
            Records.begin(),
            Records.end(),
            [ObjectName](const Pico::FDefaultSubobjectRecord& Record)
            {
                return Record.Name == ObjectName;
            });
        return Found != Records.end() ? &*Found : nullptr;
    };

    const Pico::FDefaultSubobjectRecord* RootRecord =
        FindRecord(DerivedRecords, "TemplateRoot");
    const Pico::FDefaultSubobjectRecord* CubeRecord =
        FindRecord(DerivedRecords, "TemplateCube");
    const Pico::FDefaultSubobjectRecord* ChildRecord =
        FindRecord(DerivedRecords, "DerivedChild");
    Runner.Expect(
        RootRecord != nullptr
            && RootRecord->bIsRoot
            && RootRecord->Template != nullptr
            && RootRecord->Template->GetOuter() == DerivedClass->GetDefaultObject(),
        "The derived class owns an inherited root template under its CDO");
    Runner.Expect(
        CubeRecord != nullptr
            && CubeRecord->AttachParentName == Pico::FName("TemplateRoot")
            && ChildRecord != nullptr
            && ChildRecord->AttachParentName == Pico::FName("TemplateRoot"),
        "Default-subobject attachment metadata belongs to the class template graph");
    Runner.Expect(
        CubeRecord != nullptr
            && CubeRecord->Template != nullptr
            && !CubeRecord->Template->GetHandle().IsValid()
            && Pico::HasAnyFlags(
                CubeRecord->Template->GetFlags(),
                Pico::EObjectFlags::DefaultSubobject)
            && Pico::HasAnyFlags(
                CubeRecord->Template->GetFlags(),
                Pico::EObjectFlags::Transient)
            && Pico::HasAnyFlags(
                CubeRecord->Template->GetFlags(),
                Pico::EObjectFlags::RootSet),
        "A default-subobject template is rooted class data outside the live object registry");

    Pico::PCubeComponent* CubeTemplate = CubeRecord != nullptr
        ? static_cast<Pico::PCubeComponent*>(CubeRecord->Template.get())
        : nullptr;
    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "DefaultSubobjectWorld");
    PDerivedDefaultSubobjectActor* FirstActor =
        World != nullptr && World->Initialize()
        ? World->SpawnActor<PDerivedDefaultSubobjectActor>("FirstActor")
        : nullptr;
    Pico::PCubeComponent* FirstCube = FirstActor != nullptr
        ? static_cast<Pico::PCubeComponent*>(
            Pico::FindObject(FirstActor, Pico::FName("TemplateCube")))
        : nullptr;
    Runner.Expect(
        FirstActor != nullptr
            && FirstActor->GetComponents().size() == 3
            && FirstActor->GetRootComponent() != nullptr
            && FirstActor->GetRootComponent()->GetName() == Pico::FName("TemplateRoot")
            && FirstCube != nullptr
            && FirstCube->GetAttachParent() == FirstActor->GetRootComponent(),
        "Spawning an Actor materializes its complete default-subobject graph");
    Runner.Expect(
        FirstCube != nullptr
            && FirstCube->GetExtent().Equals(Pico::FVector3(50.0f))
            && Pico::HasAnyFlags(FirstCube->GetFlags(), Pico::EObjectFlags::DefaultSubobject),
        "A materialized default subobject copies reflected values and keeps its identity flag");

    if (CubeTemplate != nullptr)
    {
        CubeTemplate->SetExtent(Pico::FVector3(75.0f));
    }
    PDerivedDefaultSubobjectActor* SecondActor = World != nullptr
        ? World->SpawnActor<PDerivedDefaultSubobjectActor>("SecondActor")
        : nullptr;
    Pico::PCubeComponent* SecondCube = SecondActor != nullptr
        ? static_cast<Pico::PCubeComponent*>(
            Pico::FindObject(SecondActor, Pico::FName("TemplateCube")))
        : nullptr;
    Runner.Expect(
        FirstCube != nullptr
            && FirstCube->GetExtent().Equals(Pico::FVector3(50.0f))
            && SecondCube != nullptr
            && SecondCube->GetExtent().Equals(Pico::FVector3(75.0f)),
        "Changing a class template affects future instances without mutating existing Actors");
    Runner.Expect(
        SecondActor != nullptr
            && SecondCube != nullptr
            && !Pico::RenameObject(SecondCube, Pico::FName("RenamedCube"))
            && !SecondActor->DestroyComponent(SecondCube),
        "Default subobjects cannot be renamed or deleted from an individual Actor");

    if (SecondCube != nullptr)
    {
        SecondCube->SetExtent(Pico::FVector3(80.0f));
    }
    Pico::FWorldAssetData Snapshot;
    Pico::EWorldSerializationError Error = Pico::EWorldSerializationError::None;
    Runner.Expect(
        World != nullptr && Pico::CaptureWorld(*World, Snapshot, &Error),
        "World capture serializes materialized default subobjects");

    const Pico::FObjectHandle FirstCubeHandle = FirstCube != nullptr
        ? FirstCube->GetHandle()
        : Pico::FObjectHandle {};
    if (CubeTemplate != nullptr)
    {
        CubeTemplate->SetExtent(Pico::FVector3(50.0f));
    }
    Pico::DestroyObjectTree(World);
    Runner.Expect(
        Pico::ResolveObject(FirstCubeHandle) == nullptr,
        "Destroying the World releases materialized default subobjects");

    Pico::PWorld* RestoredWorld = Pico::CreateWorldFromAssetData(Snapshot, &Error);
    PDerivedDefaultSubobjectActor* RestoredFirst = nullptr;
    PDerivedDefaultSubobjectActor* RestoredSecond = nullptr;
    if (RestoredWorld != nullptr)
    {
        for (Pico::PActor* Actor : RestoredWorld->GetPersistentLevel()->GetActors())
        {
            if (Actor->GetName() == Pico::FName("FirstActor"))
            {
                RestoredFirst = static_cast<PDerivedDefaultSubobjectActor*>(Actor);
            }
            else if (Actor->GetName() == Pico::FName("SecondActor"))
            {
                RestoredSecond = static_cast<PDerivedDefaultSubobjectActor*>(Actor);
            }
        }
    }
    Pico::PCubeComponent* RestoredFirstCube = RestoredFirst != nullptr
        ? static_cast<Pico::PCubeComponent*>(
            Pico::FindObject(RestoredFirst, Pico::FName("TemplateCube")))
        : nullptr;
    Pico::PCubeComponent* RestoredSecondCube = RestoredSecond != nullptr
        ? static_cast<Pico::PCubeComponent*>(
            Pico::FindObject(RestoredSecond, Pico::FName("TemplateCube")))
        : nullptr;
    Runner.Expect(
        RestoredFirst != nullptr
            && RestoredSecond != nullptr
            && RestoredFirst->GetComponents().size() == 3
            && RestoredSecond->GetComponents().size() == 3,
        "World loading reuses implicit default subobjects instead of creating duplicates");
    Runner.Expect(
        RestoredFirstCube != nullptr
            && RestoredFirstCube->GetExtent().Equals(Pico::FVector3(50.0f))
            && RestoredSecondCube != nullptr
            && RestoredSecondCube->GetExtent().Equals(Pico::FVector3(80.0f))
            && RestoredSecondCube->GetAttachParent() == RestoredSecond->GetRootComponent(),
        "World loading reapplies saved overrides and restores default-subobject relations");

    Pico::DestroyObjectTree(RestoredWorld);
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Default-subobject template and restore tests leave no registered objects");
    Pico::PObjectSystem::Shutdown();
}

void TestSceneComponentAttachmentHierarchy(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "AttachmentWorld");
    Runner.Expect(World != nullptr && World->Initialize(), "An attachment test world initializes");
    if (World == nullptr)
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PActor* Actor = World->SpawnActor<Pico::PActor>("HierarchyActor");
    Pico::PSceneComponent* Root =
        Actor != nullptr ? Actor->CreateComponent<Pico::PSceneComponent>("Root") : nullptr;
    Pico::PSceneComponent* Middle =
        Actor != nullptr ? Actor->CreateComponent<Pico::PSceneComponent>("Middle") : nullptr;
    Pico::PSceneComponent* Leaf =
        Actor != nullptr ? Actor->CreateComponent<Pico::PSceneComponent>("Leaf") : nullptr;
    const bool bComponentsReady =
        Actor != nullptr
        && Root != nullptr
        && Middle != nullptr
        && Leaf != nullptr
        && Actor->SetRootComponent(Root);
    Runner.Expect(bComponentsReady, "An Actor creates a root and two attachable scene components");
    if (!bComponentsReady)
    {
        Pico::DestroyObjectTree(World);
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Root->SetRelativeTransform(Pico::FTransform(
        Pico::FRotator(),
        Pico::FVector3(100.0f, 0.0f, 0.0f),
        Pico::FVector3(2.0f, 2.0f, 2.0f)));
    Middle->SetRelativeLocation(Pico::FVector3(10.0f, 0.0f, 0.0f));
    Leaf->SetRelativeLocation(Pico::FVector3(5.0f, 0.0f, 0.0f));

    Runner.Expect(
        Middle->AttachToComponent(Root, Pico::EAttachmentTransformRule::KeepRelative),
        "A scene component attaches to the root while keeping its relative transform");
    Runner.Expect(
        Leaf->AttachToComponent(Middle, Pico::EAttachmentTransformRule::KeepRelative),
        "A scene component attaches below another child");
    Runner.Expect(Middle->GetAttachParent() == Root, "The middle component resolves its attach parent");
    Runner.Expect(Leaf->IsAttachedTo(Root), "A descendant recognizes an ancestor");
    Runner.Expect(
        Root->GetAttachChildren().size() == 1 && Root->GetAttachChildren()[0] == Middle,
        "The root exposes its direct attach child");
    Runner.Expect(
        Middle->GetWorldTransform().Translation.Equals(Pico::FVector3(120.0f, 0.0f, 0.0f)),
        "A child world transform includes parent translation and scale");
    Runner.Expect(
        Leaf->GetWorldTransform().Translation.Equals(Pico::FVector3(130.0f, 0.0f, 0.0f)),
        "A nested child world transform includes the complete parent chain");

    const Pico::FTransform RequestedMiddleWorld(Pico::FVector3(150.0f, 20.0f, 0.0f));
    Middle->SetWorldTransform(RequestedMiddleWorld);
    Runner.Expect(
        Middle->GetWorldTransform().Equals(RequestedMiddleWorld, 0.001f),
        "Setting a child world transform converts it into parent-relative space");

    const Pico::FTransform WorldBeforeDetach = Middle->GetWorldTransform();
    Runner.Expect(
        Middle->DetachFromComponent(Pico::EAttachmentTransformRule::KeepWorld),
        "A child detaches from its parent");
    Runner.Expect(Middle->GetAttachParent() == nullptr, "A detached component has no attach parent");
    Runner.Expect(
        Middle->GetWorldTransform().Equals(WorldBeforeDetach, 0.001f),
        "KeepWorld preserves a component world transform while detaching");
    Runner.Expect(
        Root->GetAttachChildren().empty(),
        "Detaching a child removes it from the former parent");

    Runner.Expect(
        Middle->AttachToComponent(Root, Pico::EAttachmentTransformRule::KeepWorld),
        "A detached component can reattach while keeping world space");
    Runner.Expect(
        Middle->GetWorldTransform().Equals(WorldBeforeDetach, 0.001f),
        "KeepWorld preserves a component world transform while attaching");
    Runner.Expect(
        Middle->AttachToComponent(Root, Pico::EAttachmentTransformRule::KeepRelative)
            && Root->GetAttachChildren().size() == 1,
        "Reattaching to the same parent is idempotent");

    const Pico::FTransform RelativeBeforeDetach = Middle->GetRelativeTransform();
    Runner.Expect(
        Middle->DetachFromComponent(Pico::EAttachmentTransformRule::KeepRelative),
        "A child can detach while keeping relative space");
    Runner.Expect(
        Middle->GetRelativeTransform().Equals(RelativeBeforeDetach, 0.001f)
            && Middle->GetWorldTransform().Equals(RelativeBeforeDetach, 0.001f),
        "KeepRelative leaves the stored relative transform unchanged");
    Runner.Expect(
        Middle->AttachToComponent(Root, Pico::EAttachmentTransformRule::KeepRelative),
        "A component can reattach after a KeepRelative detach");
    Runner.Expect(
        Middle->GetRelativeTransform().Equals(RelativeBeforeDetach, 0.001f),
        "KeepRelative leaves the stored relative transform unchanged while attaching");
    Runner.Expect(
        !Middle->AttachToComponent(Leaf, Pico::EAttachmentTransformRule::KeepRelative),
        "A component rejects attachment to one of its descendants");
    Runner.Expect(
        !Root->AttachToComponent(Middle, Pico::EAttachmentTransformRule::KeepRelative),
        "An Actor root component cannot attach below another component");

    Pico::PActor* OtherActor = World->SpawnActor<Pico::PActor>("OtherActor");
    Pico::PSceneComponent* OtherRoot =
        OtherActor != nullptr
        ? OtherActor->CreateComponent<Pico::PSceneComponent>("OtherRoot")
        : nullptr;
    Runner.Expect(
        OtherActor != nullptr && OtherRoot != nullptr && OtherActor->SetRootComponent(OtherRoot),
        "A second Actor creates its own root component");
    Runner.Expect(
        !Middle->AttachToComponent(OtherRoot, Pico::EAttachmentTransformRule::KeepWorld),
        "Scene components reject cross-Actor attachment");

    const Pico::FTransform LeafWorldBeforeParentDestroy = Leaf->GetWorldTransform();
    const Pico::FObjectHandle MiddleHandle = Middle->GetHandle();
    Runner.Expect(Pico::DestroyObject(Middle), "An attached parent component can be destroyed independently");
    Runner.Expect(Pico::ResolveObject(MiddleHandle) == nullptr, "Destroying an attached parent invalidates its handle");
    Runner.Expect(Leaf->GetAttachParent() == nullptr, "Destroying a parent detaches its live children");
    Runner.Expect(
        Leaf->GetWorldTransform().Equals(LeafWorldBeforeParentDestroy, 0.001f),
        "Children keep their world transform when an attach parent is destroyed");
    Runner.Expect(Root->GetAttachChildren().empty(), "A destroyed child leaves its parent child list");

    Pico::PSceneComponent* ReplacementRoot =
        Actor->CreateComponent<Pico::PSceneComponent>("ReplacementRoot");
    Runner.Expect(ReplacementRoot != nullptr, "The Actor creates a replacement root candidate");
    if (ReplacementRoot == nullptr)
    {
        Pico::DestroyObjectTree(World);
        Pico::PObjectSystem::Shutdown();
        return;
    }
    ReplacementRoot->SetRelativeLocation(Pico::FVector3(25.0f, 0.0f, 0.0f));
    Runner.Expect(
        ReplacementRoot->AttachToComponent(Root, Pico::EAttachmentTransformRule::KeepRelative),
        "A future root may initially be attached to the current root");
    const Pico::FTransform ReplacementWorld = ReplacementRoot->GetWorldTransform();
    Runner.Expect(
        Actor->SetRootComponent(ReplacementRoot),
        "An attached owned scene component can become the Actor root");
    Runner.Expect(
        ReplacementRoot->GetAttachParent() == nullptr,
        "Promoting a component to root detaches it from its former parent");
    Runner.Expect(
        ReplacementRoot->GetWorldTransform().Equals(ReplacementWorld, 0.001f),
        "Promoting a component to root preserves its world transform");

    Pico::DestroyObjectTree(World);
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Attachment lifecycle leaves no registered objects");
    Pico::PObjectSystem::Shutdown();
}

void TestPrimitiveComponentSceneData(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "PrimitiveWorld");
    Pico::PActor* Actor =
        World != nullptr && World->Initialize()
        ? World->SpawnActor<Pico::PActor>("CubeActor")
        : nullptr;
    Pico::PSceneComponent* Root =
        Actor != nullptr ? Actor->CreateComponent<Pico::PSceneComponent>("Root") : nullptr;
    Pico::PCubeComponent* Cube =
        Actor != nullptr ? Actor->CreateComponent<Pico::PCubeComponent>("Cube") : nullptr;
    Pico::PStaticMeshComponent* StaticMesh = Actor != nullptr
        ? Actor->CreateComponent<Pico::PStaticMeshComponent>("StaticMesh")
        : nullptr;
    const bool bSceneReady =
        Actor != nullptr
        && Root != nullptr
        && Cube != nullptr
        && StaticMesh != nullptr
        && Actor->SetRootComponent(Root)
        && Cube->AttachToComponent(Root, Pico::EAttachmentTransformRule::KeepRelative)
        && StaticMesh->AttachToComponent(
            Root,
            Pico::EAttachmentTransformRule::KeepRelative);
    Runner.Expect(bSceneReady, "An Actor creates and attaches a renderable cube component");

    if (bSceneReady)
    {
        Runner.Expect(
            Cube->IsA(Pico::PPrimitiveComponent::StaticClass())
                && Cube->IsA(Pico::PSceneComponent::StaticClass()),
            "A cube component participates in both renderable and spatial class hierarchies");
        Runner.Expect(Cube->IsVisible(), "A cube component is visible by default");
        Runner.Expect(
            Cube->GetExtent().Equals(Pico::FVector3(50.0f)),
            "A cube component starts with a fifty-unit half extent");

        Pico::FAssetPath MeshPath;
        Pico::FAssetPath::TryParse("/Game/Meshes/Robot.pmesh", MeshPath);
        StaticMesh->SetStaticMeshAsset(MeshPath);
        Runner.Expect(
            StaticMesh->IsA(Pico::PPrimitiveComponent::StaticClass())
                && StaticMesh->GetStaticMeshAsset() == MeshPath,
            "A static mesh component stores a typed virtual asset reference");

        Root->SetRelativeLocation(Pico::FVector3(100.0f, 0.0f, 0.0f));
        Cube->SetRelativeLocation(Pico::FVector3(25.0f, 0.0f, 50.0f));
        Runner.Expect(
            Cube->GetWorldTransform().Translation.Equals(Pico::FVector3(125.0f, 0.0f, 50.0f)),
            "A renderable cube receives its world transform from the scene attachment hierarchy");

        Cube->SetVisible(false);
        Cube->SetColor(Pico::FVector3(0.8f, 0.2f, 0.1f));
        Cube->SetExtent(Pico::FVector3(20.0f, 30.0f, 40.0f));
        Runner.Expect(!Cube->IsVisible(), "Cube visibility is editable scene data");
        Runner.Expect(
            Cube->GetColor().Equals(Pico::FVector3(0.8f, 0.2f, 0.1f)),
            "Cube color is editable scene data");
        Runner.Expect(
            Cube->GetExtent().Equals(Pico::FVector3(20.0f, 30.0f, 40.0f)),
            "Cube extent is editable scene data");
    }

    Pico::DestroyObjectTree(World);
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Primitive scene data leaves no registered objects");
    Pico::PObjectSystem::Shutdown();
}

const Pico::FSceneObjectRecord* FindSceneRecord(
    const Pico::FWorldAssetData& Data,
    std::string_view ObjectName)
{
    const auto Found = std::find_if(
        Data.Objects.begin(),
        Data.Objects.end(),
        [ObjectName](const Pico::FSceneObjectRecord& Record)
        {
            return Record.ObjectName == ObjectName;
        });
    return Found != Data.Objects.end() ? &*Found : nullptr;
}

const Pico::FSerializedPropertyRecord* FindSceneProperty(
    const Pico::FSceneObjectRecord* Record,
    std::string_view PropertyName)
{
    if (Record == nullptr)
    {
        return nullptr;
    }

    const auto Found = std::find_if(
        Record->Properties.begin(),
        Record->Properties.end(),
        [PropertyName](const Pico::FSerializedPropertyRecord& Property)
        {
            return Property.Name == PropertyName;
        });
    return Found != Record->Properties.end() ? &*Found : nullptr;
}

void TestWorldAssetDataSerialization(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "AssetWorld");
    Pico::FWorldAssetData RejectedData;
    Pico::EWorldSerializationError Error =
        Pico::EWorldSerializationError::None;
    Runner.Expect(
        World != nullptr
            && !Pico::CaptureWorld(*World, RejectedData, &Error)
            && Error == Pico::EWorldSerializationError::InvalidArgument,
        "CaptureWorld rejects an uninitialized world");

    const bool bWorldInitialized = World != nullptr && World->Initialize();
    Pico::FWorldAssetData EmptyWorldData;
    Runner.Expect(
        bWorldInitialized
            && Pico::CaptureWorld(*World, EmptyWorldData, &Error)
            && EmptyWorldData.Objects.size() == 2
            && EmptyWorldData.Relations.empty(),
        "CaptureWorld records an empty initialized world and persistent level");

    Pico::PLevel* GameplayLevel =
        bWorldInitialized ? World->CreateLevel("Gameplay") : nullptr;
    Runner.Expect(
        bWorldInitialized
            && GameplayLevel != nullptr
            && World->SetCurrentLevel(GameplayLevel),
        "World asset test creates persistent and current levels");
    if (!bWorldInitialized || GameplayLevel == nullptr)
    {
        Pico::DestroyObjectTree(World);
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::PActor* Actor = World->SpawnActor<PLoadTrackingActor>(
        "SerializedActor",
        GameplayLevel);
    Pico::PSceneComponent* Root =
        Actor != nullptr
        ? Actor->CreateComponent<Pico::PSceneComponent>("Root")
        : nullptr;
    Pico::PCubeComponent* Cube =
        Actor != nullptr
        ? Actor->CreateComponent<Pico::PCubeComponent>("Cube")
        : nullptr;
    const bool bSceneReady =
        Actor != nullptr
        && Root != nullptr
        && Cube != nullptr
        && Actor->SetRootComponent(Root)
        && Cube->AttachToComponent(
            Root,
            Pico::EAttachmentTransformRule::KeepRelative);
    Runner.Expect(
        bSceneReady,
        "World asset test creates a root and attached cube component");
    if (!bSceneReady)
    {
        Pico::DestroyObjectTree(World);
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Root->SetRelativeLocation(Pico::FVector3(100.0f, 20.0f, 5.0f));
    Cube->SetRelativeLocation(Pico::FVector3(10.0f, 0.0f, 50.0f));
    Cube->SetVisible(false);
    Cube->SetColor(Pico::FVector3(0.8f, 0.2f, 0.1f));
    Cube->SetExtent(Pico::FVector3(20.0f, 30.0f, 40.0f));

    Pico::FWorldAssetData CapturedData;
    Runner.Expect(
        Pico::CaptureWorld(*World, CapturedData, &Error)
            && Error == Pico::EWorldSerializationError::None,
        "CaptureWorld produces validated scene asset data");
    Runner.Expect(
        CapturedData.Objects.size() == 6,
        "World capture records the world, two levels, actor, and components");
    Runner.Expect(
        CapturedData.Relations.size() == 2,
        "World capture records root and attachment relationships");

    const Pico::FSceneObjectRecord* CubeRecord =
        FindSceneRecord(CapturedData, "Cube");
    const Pico::FSerializedPropertyRecord* VisibleProperty =
        FindSceneProperty(CubeRecord, "bVisible");
    const Pico::FSerializedPropertyRecord* ColorProperty =
        FindSceneProperty(CubeRecord, "Color");
    const Pico::FSerializedPropertyRecord* ExtentProperty =
        FindSceneProperty(CubeRecord, "Extent");
    const Pico::FSerializedPropertyRecord* TransformProperty =
        FindSceneProperty(CubeRecord, "RelativeTransform");
    Runner.Expect(
        CubeRecord != nullptr
            && CubeRecord->ClassName == "PCubeComponent"
            && CubeRecord->OuterId.IsValid(),
        "Captured cube record stores class, name, and outer identity");
    Runner.Expect(
        VisibleProperty != nullptr
            && VisibleProperty->Type == Pico::EPropertyType::Bool
            && !VisibleProperty->BoolValue,
        "Captured scene data stores inherited visibility");
    Runner.Expect(
        ColorProperty != nullptr
            && ColorProperty->Vector3Value.Equals(
                Pico::FVector3(0.8f, 0.2f, 0.1f)),
        "Captured scene data stores inherited color");
    Runner.Expect(
        ExtentProperty != nullptr
            && ExtentProperty->Vector3Value.Equals(
                Pico::FVector3(20.0f, 30.0f, 40.0f)),
        "Captured scene data stores cube extent");
    Runner.Expect(
        TransformProperty != nullptr
            && TransformProperty->TransformValue.Translation.Equals(
                Pico::FVector3(10.0f, 0.0f, 50.0f)),
        "Captured scene data stores component relative transform");

    Pico::FMemoryWriter Writer;
    Runner.Expect(
        Pico::SerializeWorldAsset(Writer, CapturedData, &Error)
            && Error == Pico::EWorldSerializationError::None,
        "Validated world asset data serializes to a memory archive");
    Pico::FMemoryWriter SecondWriter;
    Runner.Expect(
        Pico::SerializeWorldAsset(SecondWriter, CapturedData, &Error)
            && Writer.GetData() == SecondWriter.GetData(),
        "Serializing unchanged world data produces deterministic bytes");

    Pico::FMemoryReader Reader(Writer.GetData());
    Pico::FWorldAssetData LoadedData;
    Runner.Expect(
        Pico::DeserializeWorldAsset(Reader, LoadedData, &Error)
            && Error == Pico::EWorldSerializationError::None
            && Reader.GetRemainingSize() == 0,
        "World asset bytes deserialize completely into pure data");
    Runner.Expect(
        LoadedData.Objects.size() == CapturedData.Objects.size()
            && LoadedData.Relations.size() == CapturedData.Relations.size()
            && LoadedData.WorldId == CapturedData.WorldId
            && LoadedData.PersistentLevelId == CapturedData.PersistentLevelId
            && LoadedData.CurrentLevelId == CapturedData.CurrentLevelId,
        "World asset round trip preserves graph identity and counts");

    const Pico::FSerializedPropertyRecord* LoadedExtent = FindSceneProperty(
        FindSceneRecord(LoadedData, "Cube"),
        "Extent");
    Runner.Expect(
        LoadedExtent != nullptr
            && LoadedExtent->Vector3Value.Equals(
                Pico::FVector3(20.0f, 30.0f, 40.0f)),
        "World asset round trip preserves reflected property values");

    Pico::FAssetPath StaticMeshPath;
    Pico::FAssetPath::TryParse("/Game/Meshes/Robot.pmesh", StaticMeshPath);
    Pico::FWorldAssetData AssetReferenceData = CapturedData;
    Pico::FSerializedPropertyRecord AssetReference;
    AssetReference.Name = "StaticMeshAsset";
    AssetReference.Type = Pico::EPropertyType::AssetPath;
    AssetReference.AssetPathValue = StaticMeshPath;
    AssetReferenceData.Objects.back().Properties.push_back(AssetReference);
    Pico::FMemoryWriter AssetReferenceWriter;
    Pico::FWorldAssetData LoadedAssetReferenceData;
    Runner.Expect(
        Pico::SerializeWorldAsset(AssetReferenceWriter, AssetReferenceData, &Error),
        "World format version 2 serializes AssetPath property records");
    Pico::FMemoryReader AssetReferenceReader(AssetReferenceWriter.GetData());
    const bool bLoadedAssetReference = Pico::DeserializeWorldAsset(
        AssetReferenceReader,
        LoadedAssetReferenceData,
        &Error);
    const Pico::FSerializedPropertyRecord* LoadedAssetReference =
        FindSceneProperty(
            FindSceneRecord(LoadedAssetReferenceData, "Cube"),
            "StaticMeshAsset");
    Runner.Expect(
        bLoadedAssetReference
            && LoadedAssetReference != nullptr
            && LoadedAssetReference->Type == Pico::EPropertyType::AssetPath
            && LoadedAssetReference->AssetPathValue == StaticMeshPath,
        "World format version 2 restores AssetPath property records");

    Pico::FWorldAssetData InvalidData = CapturedData;
    InvalidData.Objects[1].Id = InvalidData.Objects[0].Id;
    Runner.Expect(
        !Pico::ValidateWorldAssetData(InvalidData, &Error)
            && Error == Pico::EWorldSerializationError::InvalidObjectGraph,
        "World validation rejects duplicate scene object IDs");

    InvalidData = CapturedData;
    InvalidData.Objects.back().OuterId = Pico::FSceneObjectId { 999999 };
    Runner.Expect(
        !Pico::ValidateWorldAssetData(InvalidData, &Error)
            && Error == Pico::EWorldSerializationError::InvalidObjectGraph,
        "World validation rejects a missing outer");

    InvalidData = CapturedData;
    InvalidData.Objects[0].OuterId = InvalidData.Objects[1].Id;
    InvalidData.Objects[1].OuterId = InvalidData.Objects[0].Id;
    Runner.Expect(
        !Pico::ValidateWorldAssetData(InvalidData, &Error)
            && Error == Pico::EWorldSerializationError::InvalidObjectGraph,
        "World validation rejects an outer cycle");

    const Pico::FSceneObjectRecord* RootRecord =
        FindSceneRecord(CapturedData, "Root");
    Runner.Expect(
        RootRecord != nullptr && CubeRecord != nullptr,
        "World asset test finds scene components for malformed graph checks");
    if (RootRecord != nullptr && CubeRecord != nullptr)
    {
        InvalidData = CapturedData;
        InvalidData.Relations.push_back(
            Pico::FSceneRelationRecord {
                RootRecord->Id,
                {},
                CubeRecord->Id });
        Runner.Expect(
            !Pico::ValidateWorldAssetData(InvalidData, &Error)
                && Error == Pico::EWorldSerializationError::InvalidObjectGraph,
            "World validation rejects an attachment cycle");
    }

    InvalidData = CapturedData;
    InvalidData.Objects.back().ClassName = "PMissingSceneClass";
    Runner.Expect(
        !Pico::ValidateWorldAssetData(InvalidData, &Error)
            && Error == Pico::EWorldSerializationError::ClassNotFound,
        "World validation rejects missing reflected classes");

    InvalidData = CapturedData;
    InvalidData.Objects.back().Properties.push_back(
        InvalidData.Objects.back().Properties.front());
    Runner.Expect(
        !Pico::ValidateWorldAssetData(InvalidData, &Error)
            && Error == Pico::EWorldSerializationError::InvalidObjectGraph,
        "World validation rejects duplicate property records");

    InvalidData = CapturedData;
    InvalidData.Objects.back().Properties.resize(4097);
    Runner.Expect(
        !Pico::ValidateWorldAssetData(InvalidData, &Error)
            && Error == Pico::EWorldSerializationError::PropertyLimitExceeded,
        "World validation enforces the per-object property limit");

    InvalidData = CapturedData;
    InvalidData.Objects.back().ObjectName.assign(1025, 'A');
    Runner.Expect(
        !Pico::ValidateWorldAssetData(InvalidData, &Error)
            && Error == Pico::EWorldSerializationError::InvalidObjectGraph,
        "World validation enforces the scene name limit");

    std::vector<Pico::uint8> InvalidMagic = Writer.GetData();
    InvalidMagic[0] ^= 0xffu;
    Pico::FMemoryReader InvalidMagicReader(InvalidMagic);
    Pico::FWorldAssetData UnchangedData;
    UnchangedData.WorldId = Pico::FSceneObjectId { 777 };
    Runner.Expect(
        !Pico::DeserializeWorldAsset(
            InvalidMagicReader,
            UnchangedData,
            &Error)
            && Error == Pico::EWorldSerializationError::InvalidArchive
            && UnchangedData.WorldId.Value == 777,
        "Invalid world magic fails without changing the output data");

    Pico::FMemoryWriter Version1Writer;
    Pico::uint32 LegacyMagic = 0x444c5750;
    Pico::uint32 LegacyVersion = 1;
    Pico::uint32 LegacyObjectCount =
        static_cast<Pico::uint32>(EmptyWorldData.Objects.size());
    Pico::uint32 LegacyRelationCount = 0;
    Pico::uint64 LegacyWorldId = EmptyWorldData.WorldId.Value;
    Pico::uint64 LegacyPersistentLevelId =
        EmptyWorldData.PersistentLevelId.Value;
    Pico::uint64 LegacyCurrentLevelId = EmptyWorldData.CurrentLevelId.Value;
    Version1Writer.SerializeUInt32(LegacyMagic);
    Version1Writer.SerializeUInt32(LegacyVersion);
    Version1Writer.SerializeUInt32(LegacyObjectCount);
    Version1Writer.SerializeUInt32(LegacyRelationCount);
    Version1Writer.SerializeUInt64(LegacyWorldId);
    Version1Writer.SerializeUInt64(LegacyPersistentLevelId);
    Version1Writer.SerializeUInt64(LegacyCurrentLevelId);
    for (const Pico::FSceneObjectRecord& SourceRecord : EmptyWorldData.Objects)
    {
        Pico::FSceneObjectRecord Record = SourceRecord;
        Version1Writer.SerializeUInt64(Record.Id.Value);
        Version1Writer.SerializeUInt64(Record.OuterId.Value);
        Version1Writer.SerializeString(Record.ClassName);
        Version1Writer.SerializeString(Record.ObjectName);
        Pico::uint32 Flags = static_cast<Pico::uint32>(Record.Flags);
        Pico::uint32 PropertyCount =
            static_cast<Pico::uint32>(Record.Properties.size());
        Version1Writer.SerializeUInt32(Flags);
        Version1Writer.SerializeUInt32(PropertyCount);
        for (Pico::FSerializedPropertyRecord& Property : Record.Properties)
        {
            Pico::SerializePropertyRecord(Version1Writer, Property);
        }
    }
    Runner.Expect(
        !Version1Writer.HasError(),
        "A relation-free scene prepares a real version 1 compatibility fixture");
    Pico::FMemoryReader Version1Reader(Version1Writer.GetData());
    Pico::FWorldAssetData Version1LoadedData;
    Runner.Expect(
        Pico::DeserializeWorldAsset(Version1Reader, Version1LoadedData, &Error)
            && Version1LoadedData.Objects.size() == EmptyWorldData.Objects.size(),
        "World format version 4 remains compatible with version 1 scenes");

    std::vector<Pico::uint8> UnsupportedVersion = Writer.GetData();
    UnsupportedVersion[4] = 99;
    UnsupportedVersion[5] = 0;
    UnsupportedVersion[6] = 0;
    UnsupportedVersion[7] = 0;
    Pico::FMemoryReader UnsupportedVersionReader(UnsupportedVersion);
    Runner.Expect(
        !Pico::DeserializeWorldAsset(
            UnsupportedVersionReader,
            UnchangedData,
            &Error)
            && Error == Pico::EWorldSerializationError::UnsupportedVersion,
        "World deserialization rejects unsupported format versions");

    std::vector<Pico::uint8> Truncated = Writer.GetData();
    Truncated.resize(Truncated.size() - 3);
    Pico::FMemoryReader TruncatedReader(Truncated);
    Runner.Expect(
        !Pico::DeserializeWorldAsset(
            TruncatedReader,
            UnchangedData,
            &Error)
            && Error == Pico::EWorldSerializationError::InvalidArchive,
        "World deserialization rejects truncated archives");

    std::vector<Pico::uint8> TooManyObjects = Writer.GetData();
    TooManyObjects[8] = 0xffu;
    TooManyObjects[9] = 0xffu;
    TooManyObjects[10] = 0xffu;
    TooManyObjects[11] = 0xffu;
    Pico::FMemoryReader ObjectLimitReader(TooManyObjects);
    Runner.Expect(
        !Pico::DeserializeWorldAsset(
            ObjectLimitReader,
            UnchangedData,
            &Error)
            && Error == Pico::EWorldSerializationError::ObjectLimitExceeded,
        "World deserialization enforces the scene object limit");

    std::vector<Pico::uint8> TooManyRelations = Writer.GetData();
    TooManyRelations[12] = 0xffu;
    TooManyRelations[13] = 0xffu;
    TooManyRelations[14] = 0xffu;
    TooManyRelations[15] = 0xffu;
    Pico::FMemoryReader RelationLimitReader(TooManyRelations);
    Runner.Expect(
        !Pico::DeserializeWorldAsset(
            RelationLimitReader,
            UnchangedData,
            &Error)
            && Error == Pico::EWorldSerializationError::RelationLimitExceeded,
        "World deserialization enforces the scene relation limit");

    Pico::FMemoryWriter UInt64Writer;
    Pico::uint64 SavedUInt64 = 0x0123456789abcdefull;
    UInt64Writer.SerializeUInt64(SavedUInt64);
    Pico::FMemoryReader UInt64Reader(UInt64Writer.GetData());
    Pico::uint64 LoadedUInt64 = 0;
    UInt64Reader.SerializeUInt64(LoadedUInt64);
    Runner.Expect(
        LoadedUInt64 == SavedUInt64
            && UInt64Reader.GetRemainingSize() == 0,
        "Archive serializes 64-bit scene IDs in a stable byte order");

    const Pico::FObjectHandle OriginalWorldHandle = World->GetHandle();
    const Pico::FObjectHandle OriginalActorHandle = Actor->GetHandle();
    const Pico::FObjectHandle OriginalCubeHandle = Cube->GetHandle();
    Pico::DestroyObjectTree(World);
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "World asset capture leaves no registered objects");

    PLoadTrackingActor::TotalPostLoadCount = 0;
    PLoadTrackingActor::bThrowPostLoad = false;
    PLoadTrackingActor::bObservedRootComponent = false;
    Pico::PWorld* ReconstructedWorld =
        Pico::CreateWorldFromAssetData(LoadedData, &Error);
    Runner.Expect(
        ReconstructedWorld != nullptr
            && Error == Pico::EWorldSerializationError::None
            && ReconstructedWorld->GetState() == Pico::EWorldState::Initialized,
        "Validated world asset data reconstructs an initialized runtime world");
    if (ReconstructedWorld != nullptr)
    {
        Pico::PLevel* ReconstructedGameplay = ReconstructedWorld->GetCurrentLevel();
        Pico::PObject* ReconstructedActorObject = ReconstructedGameplay != nullptr
            ? Pico::FindObject(ReconstructedGameplay, Pico::FName("SerializedActor"))
            : nullptr;
        Pico::PActor* ReconstructedActor =
            ReconstructedActorObject != nullptr
                && ReconstructedActorObject->IsA(Pico::PActor::StaticClass())
            ? static_cast<Pico::PActor*>(ReconstructedActorObject)
            : nullptr;
        Pico::PObject* ReconstructedRootObject = ReconstructedActor != nullptr
            ? Pico::FindObject(ReconstructedActor, Pico::FName("Root"))
            : nullptr;
        Pico::PObject* ReconstructedCubeObject = ReconstructedActor != nullptr
            ? Pico::FindObject(ReconstructedActor, Pico::FName("Cube"))
            : nullptr;
        Pico::PSceneComponent* ReconstructedRoot =
            ReconstructedRootObject != nullptr
                && ReconstructedRootObject->IsA(Pico::PSceneComponent::StaticClass())
            ? static_cast<Pico::PSceneComponent*>(ReconstructedRootObject)
            : nullptr;
        Pico::PCubeComponent* ReconstructedCube =
            ReconstructedCubeObject != nullptr
                && ReconstructedCubeObject->IsA(Pico::PCubeComponent::StaticClass())
            ? static_cast<Pico::PCubeComponent*>(ReconstructedCubeObject)
            : nullptr;

        Runner.Expect(
            ReconstructedWorld->GetPersistentLevel() != nullptr
                && ReconstructedGameplay != nullptr
                && ReconstructedGameplay->GetName() == Pico::FName("Gameplay"),
            "World reconstruction restores persistent and current level selection");
        Runner.Expect(
            ReconstructedActor != nullptr
                && ReconstructedRoot != nullptr
                && ReconstructedCube != nullptr
                && ReconstructedActor->GetRootComponent() == ReconstructedRoot
                && ReconstructedCube->GetAttachParent() == ReconstructedRoot,
            "World reconstruction restores actor roots and component attachments");
        Runner.Expect(
            ReconstructedCube != nullptr
                && !ReconstructedCube->IsVisible()
                && ReconstructedCube->GetColor().Equals(
                    Pico::FVector3(0.8f, 0.2f, 0.1f))
                && ReconstructedCube->GetExtent().Equals(
                    Pico::FVector3(20.0f, 30.0f, 40.0f))
                && ReconstructedCube->GetWorldTransform().Translation.Equals(
                    Pico::FVector3(110.0f, 20.0f, 55.0f)),
            "World reconstruction reapplies reflected scene properties");
        Runner.Expect(
            ReconstructedWorld->GetHandle() != OriginalWorldHandle
                && ReconstructedActor != nullptr
                && ReconstructedActor->GetHandle() != OriginalActorHandle
                && ReconstructedCube != nullptr
                && ReconstructedCube->GetHandle() != OriginalCubeHandle,
            "Reconstructed objects receive fresh runtime handles");
        Runner.Expect(
            PLoadTrackingActor::TotalPostLoadCount == 1
                && PLoadTrackingActor::bObservedRootComponent,
            "World reconstruction calls PostLoad once after relationships are restored");

        Pico::FWorldAssetData RecapturedData;
        Pico::FMemoryWriter RecapturedWriter;
        Runner.Expect(
            Pico::CaptureWorld(*ReconstructedWorld, RecapturedData, &Error)
                && Pico::SerializeWorldAsset(
                    RecapturedWriter,
                    RecapturedData,
                    &Error)
                && RecapturedWriter.GetData() == Writer.GetData(),
            "Reconstructed world captures back to identical deterministic bytes");

        const std::size_t LoadedObjectCount =
            Pico::FObjectRegistry::GetObjectCount();
        Runner.Expect(
            Pico::CreateWorldFromAssetData(LoadedData, &Error) == nullptr
                && Error == Pico::EWorldSerializationError::ObjectCreationFailed
                && Pico::FObjectRegistry::GetObjectCount() == LoadedObjectCount
                && Pico::ResolveObject(ReconstructedWorld->GetHandle())
                    == ReconstructedWorld,
            "A conflicting top-level World name leaves the live World untouched");
    }

    Pico::DestroyObjectTree(ReconstructedWorld);
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Destroying a reconstructed world clears every loaded object");

    Pico::FWorldAssetData TypeMismatchData = LoadedData;
    Pico::FSceneObjectRecord* TypeMismatchCube = nullptr;
    for (Pico::FSceneObjectRecord& Record : TypeMismatchData.Objects)
    {
        if (Record.ObjectName == "Cube")
        {
            TypeMismatchCube = &Record;
            break;
        }
    }
    if (TypeMismatchCube != nullptr)
    {
        for (Pico::FSerializedPropertyRecord& Property : TypeMismatchCube->Properties)
        {
            if (Property.Name == "Extent")
            {
                Property.Type = Pico::EPropertyType::Int32;
                break;
            }
        }
    }
    Runner.Expect(
        Pico::CreateWorldFromAssetData(TypeMismatchData, &Error) == nullptr
            && Error == Pico::EWorldSerializationError::PropertyTypeMismatch
            && Pico::FObjectRegistry::GetObjectCount() == 0,
        "A property type mismatch rolls back every partially loaded object");

    PLoadTrackingActor::bThrowPostLoad = true;
    Runner.Expect(
        Pico::CreateWorldFromAssetData(LoadedData, &Error) == nullptr
            && Error == Pico::EWorldSerializationError::PostLoadFailed
            && Pico::FObjectRegistry::GetObjectCount() == 0,
        "A PostLoad exception rolls back every partially loaded object");
    PLoadTrackingActor::bThrowPostLoad = false;

    Pico::PObjectSystem::Shutdown();
}

void TestStageHPersistenceAndPropertyNotifications(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        return;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "StageHWorld");
    const bool bWorldReady = World != nullptr && World->Initialize();
    PStageHActor* Source = bWorldReady
        ? World->SpawnActor<PStageHActor>("Source")
        : nullptr;
    PStageHActor* Target = bWorldReady
        ? World->SpawnActor<PStageHActor>("Target")
        : nullptr;
    Runner.Expect(
        Source != nullptr && Target != nullptr,
        "Stage H creates a source and target Actor in one serializable World");
    if (Source == nullptr || Target == nullptr)
    {
        Pico::DestroyObjectTree(World);
        Pico::PObjectSystem::Shutdown();
        return;
    }

    const Pico::PProperty* ValueProperty =
        PStageHActor::StaticClass()->FindProperty(Pico::FName("Value"));
    int ExternalPreCount = 0;
    int ExternalPostCount = 0;
    Source->OnPropertyChanging().AddLambda(
        [&ExternalPreCount, Source, ValueProperty](
            Pico::PObject* Object,
            const Pico::FPropertyChangedEvent& Event)
        {
            if (Object == Source && Event.Property == ValueProperty)
            {
                ++ExternalPreCount;
            }
        });
    Source->OnPropertyChanged().AddLambda(
        [&ExternalPostCount, Source, ValueProperty](
            Pico::PObject* Object,
            const Pico::FPropertyChangedEvent& Event)
        {
            if (Object == Source
                && Event.Property == ValueProperty
                && Event.ChangeType == Pico::EPropertyChangeType::ValueSet)
            {
                ++ExternalPostCount;
            }
        });
    Source->ResetChangeTracking();
    Runner.Expect(
        ValueProperty != nullptr
            && ValueProperty->SetValue(Source, Pico::int32 {42})
            && Source->GetValue() == 42
            && Source->GetPreChangeCount() == 1
            && Source->GetPostChangeCount() == 1
            && Source->GetLastPropertyName() == Pico::FName("Value")
            && ExternalPreCount == 1
            && ExternalPostCount == 1,
        "Reflected writes share one PreEditChange, PostEditChange, and native notification path");

    PStageHActor* DefaultActor = static_cast<PStageHActor*>(
        PStageHActor::StaticClass()->GetMutableDefaultObject());
    const Pico::int32 OriginalDefault = DefaultActor != nullptr
        ? DefaultActor->GetValue()
        : 0;
    if (DefaultActor != nullptr)
    {
        DefaultActor->ResetChangeTracking();
    }
    const bool bDefaultChanged = DefaultActor != nullptr
        && ValueProperty->SetValue(DefaultActor, Pico::int32 {77});
    PStageHActor* FutureInstance = bDefaultChanged
        ? World->SpawnActor<PStageHActor>("FutureInstance")
        : nullptr;
    Runner.Expect(
        bDefaultChanged
            && DefaultActor->GetPostChangeCount() == 1
            && FutureInstance != nullptr
            && FutureInstance->GetValue() == 77
            && Source->GetValue() == 42,
        "Changing the CDO notifies observers and affects future instances without overwriting live instances");
    if (DefaultActor != nullptr)
    {
        ValueProperty->SetValue(DefaultActor, OriginalDefault);
    }

    const Pico::FDynamicDelegateBindingResult Binding =
        Source->GetOnValue().AddDynamic(Target, Pico::FName("HandleValue"));
    Runner.Expect(
        Binding.IsSuccess(),
        "A reflected dynamic multicast property binds a Callable target function");

    Pico::FWorldAssetData Captured;
    Pico::EWorldSerializationError Error = Pico::EWorldSerializationError::None;
    const bool bCaptured = Pico::CaptureWorld(*World, Captured, &Error);
    const Pico::FSceneObjectRecord* SourceRecord = nullptr;
    for (const Pico::FSceneObjectRecord& Record : Captured.Objects)
    {
        if (Record.ObjectName == "Source")
        {
            SourceRecord = &Record;
            break;
        }
    }
    const bool bBindingCaptured = SourceRecord != nullptr
        && SourceRecord->DynamicDelegates.size() == 1
        && SourceRecord->DynamicDelegates[0].PropertyName == "OnValue"
        && SourceRecord->DynamicDelegates[0].Bindings.size() == 1
        && SourceRecord->DynamicDelegates[0].Bindings[0].Target.SceneId.IsValid()
        && SourceRecord->DynamicDelegates[0].Bindings[0].Target.ObjectPath
            == Target->GetPathName()
        && SourceRecord->DynamicDelegates[0].Bindings[0].FunctionName
            == "HandleValue";
    Runner.Expect(
        bCaptured && bBindingCaptured,
        "World capture stores delegate property, stable scene reference, object path, and function name");

    Pico::FMemoryWriter Writer;
    Pico::FWorldAssetData LoadedData;
    const bool bRoundTripped = bCaptured
        && Pico::SerializeWorldAsset(Writer, Captured, &Error);
    Pico::FMemoryReader Reader(Writer.GetData());
    Runner.Expect(
        bRoundTripped
            && Pico::DeserializeWorldAsset(Reader, LoadedData, &Error)
            && Reader.GetRemainingSize() == 0,
        "World format version 4 round-trips dynamic multicast binding records");

    const auto StageHSuffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path StageHFile =
        std::filesystem::temp_directory_path()
        / ("PicoStageHDelegate_" + std::to_string(StageHSuffix) + ".pworld");
    std::error_code FileError;
    std::filesystem::remove(StageHFile, FileError);
    Runner.Expect(
        Pico::SaveWorldToFile(StageHFile, *World, &Error),
        "A real .pworld file stores the reflected dynamic delegate binding");

    const Pico::FObjectHandle OldTargetHandle = Target->GetHandle();
    Pico::DestroyObjectTree(World);
    World = Pico::LoadWorldFromFile(StageHFile, &Error);
    PStageHActor* FileSource = nullptr;
    PStageHActor* FileTarget = nullptr;
    if (World != nullptr)
    {
        for (Pico::PActor* Actor : World->GetPersistentLevel()->GetActors())
        {
            if (Actor->GetName() == Pico::FName("Source"))
                FileSource = static_cast<PStageHActor*>(Actor);
            else if (Actor->GetName() == Pico::FName("Target"))
                FileTarget = static_cast<PStageHActor*>(Actor);
        }
    }
    if (FileSource != nullptr && FileTarget != nullptr)
    {
        FileSource->BroadcastValue(99);
    }
    Runner.Expect(
        FileSource != nullptr
            && FileTarget != nullptr
            && FileSource->GetOnValue().Num() == 1
            && FileSource->GetLastChangeType() == Pico::EPropertyChangeType::Load
            && FileTarget->GetLastReceivedValue() == 99,
        "Closing and reopening a .pworld restores the binding with Load notifications");
    Pico::DestroyObjectTree(World);

    World = Pico::CreateWorldFromAssetData(
        LoadedData,
        &Error,
        {Pico::EPropertyChangeType::UndoRedo});
    PStageHActor* RestoredSource = nullptr;
    PStageHActor* RestoredTarget = nullptr;
    if (World != nullptr)
    {
        for (Pico::PActor* Actor : World->GetPersistentLevel()->GetActors())
        {
            if (Actor->GetName() == Pico::FName("Source"))
                RestoredSource = static_cast<PStageHActor*>(Actor);
            else if (Actor->GetName() == Pico::FName("Target"))
                RestoredTarget = static_cast<PStageHActor*>(Actor);
        }
    }
    Runner.Expect(
        RestoredSource != nullptr
            && RestoredTarget != nullptr
            && RestoredSource->GetOnValue().Num() == 1
            && RestoredTarget->GetHandle() != OldTargetHandle
            && RestoredSource->GetPostChangeCount() >= 2
            && RestoredSource->GetLastChangeType()
                == Pico::EPropertyChangeType::UndoRedo,
        "Two-phase load resolves new runtime handles and reports UndoRedo property restoration");
    if (RestoredSource != nullptr && RestoredTarget != nullptr)
    {
        RestoredSource->BroadcastValue(123);
    }
    Runner.Expect(
        RestoredTarget != nullptr
            && RestoredTarget->GetReceivedCount() == 1
            && RestoredTarget->GetLastReceivedValue() == 123,
        "A dynamic multicast binding still reaches its target after the original World is destroyed");

    Pico::DestroyObjectTree(World);
    Pico::FWorldAssetData PathFallbackData = LoadedData;
    for (Pico::FSceneObjectRecord& Record : PathFallbackData.Objects)
    {
        if (Record.ObjectName == "Source" && !Record.DynamicDelegates.empty())
        {
            Record.DynamicDelegates[0].Bindings[0].Target.SceneId = Record.Id;
        }
    }
    World = Pico::CreateWorldFromAssetData(PathFallbackData, &Error);
    RestoredSource = nullptr;
    RestoredTarget = nullptr;
    if (World != nullptr)
    {
        for (Pico::PActor* Actor : World->GetPersistentLevel()->GetActors())
        {
            if (Actor->GetName() == Pico::FName("Source"))
                RestoredSource = static_cast<PStageHActor*>(Actor);
            else if (Actor->GetName() == Pico::FName("Target"))
                RestoredTarget = static_cast<PStageHActor*>(Actor);
        }
    }
    if (RestoredSource != nullptr)
    {
        RestoredSource->BroadcastValue(321);
    }
    Runner.Expect(
        RestoredTarget != nullptr
            && RestoredTarget->GetLastReceivedValue() == 321,
        "ObjectPath safely repairs a serialized reference whose SceneId resolves to the wrong object");

    Pico::DestroyObjectTree(World);
    Pico::FWorldAssetData StaleTargetData = LoadedData;
    for (Pico::FSceneObjectRecord& Record : StaleTargetData.Objects)
    {
        if (Record.ObjectName == "Source" && !Record.DynamicDelegates.empty())
        {
            auto& TargetReference =
                Record.DynamicDelegates[0].Bindings[0].Target;
            TargetReference.SceneId = Pico::FSceneObjectId {999999};
            TargetReference.ObjectPath = "StageHWorld.PersistentLevel.Missing";
        }
    }
    World = Pico::CreateWorldFromAssetData(StaleTargetData, &Error);
    RestoredSource = nullptr;
    if (World != nullptr)
    {
        for (Pico::PActor* Actor : World->GetPersistentLevel()->GetActors())
        {
            if (Actor->GetName() == Pico::FName("Source"))
            {
                RestoredSource = static_cast<PStageHActor*>(Actor);
            }
        }
    }
    Runner.Expect(
        World != nullptr
            && RestoredSource != nullptr
            && RestoredSource->GetOnValue().Num() == 0,
        "A stale serialized target is skipped without failing the complete World load");

    Pico::DestroyObjectTree(World);
    Pico::FWorldAssetData MissingFunctionData = LoadedData;
    for (Pico::FSceneObjectRecord& Record : MissingFunctionData.Objects)
    {
        if (Record.ObjectName == "Source" && !Record.DynamicDelegates.empty())
        {
            Record.DynamicDelegates[0].Bindings[0].FunctionName = "MissingFunction";
        }
    }
    World = Pico::CreateWorldFromAssetData(MissingFunctionData, &Error);
    RestoredSource = nullptr;
    if (World != nullptr)
    {
        for (Pico::PActor* Actor : World->GetPersistentLevel()->GetActors())
        {
            if (Actor->GetName() == Pico::FName("Source"))
            {
                RestoredSource = static_cast<PStageHActor*>(Actor);
            }
        }
    }
    Runner.Expect(
        World != nullptr
            && RestoredSource != nullptr
            && RestoredSource->GetOnValue().Num() == 0,
        "A removed or renamed PFunction becomes a skipped binding instead of corrupting the scene");

    Pico::DestroyObjectTree(World);
    std::filesystem::remove(StageHFile, FileError);
    std::filesystem::remove(StageHFile.string() + ".tmp", FileError);
    std::filesystem::remove(StageHFile.string() + ".bak", FileError);
    Pico::PObjectSystem::Shutdown();
}

void TestWorldFilePersistence(FTestRunner& Runner)
{
    if (!InitializeWorldTypes(Runner))
    {
        Pico::PObjectSystem::Shutdown();
        return;
    }

    const auto UniqueSuffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path FilePath =
        std::filesystem::temp_directory_path()
        / ("PicoWorldPersistence_" + std::to_string(UniqueSuffix) + ".pworld");
    std::filesystem::path TemporaryPath = FilePath;
    TemporaryPath += ".tmp";
    std::filesystem::path BackupPath = FilePath;
    BackupPath += ".bak";

    const auto RemoveTestFiles = [&]()
    {
        std::error_code ErrorCode;
        std::filesystem::remove(FilePath, ErrorCode);
        std::filesystem::remove(TemporaryPath, ErrorCode);
        std::filesystem::remove(BackupPath, ErrorCode);
    };
    const auto ReadFileBytes = [&]() -> std::vector<Pico::uint8>
    {
        std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
        if (!File)
        {
            return {};
        }
        const std::streampos EndPosition = File.tellg();
        if (EndPosition < 0)
        {
            return {};
        }
        std::vector<Pico::uint8> Bytes(static_cast<std::size_t>(EndPosition));
        File.seekg(0, std::ios::beg);
        if (!Bytes.empty())
        {
            File.read(
                reinterpret_cast<char*>(Bytes.data()),
                static_cast<std::streamsize>(Bytes.size()));
        }
        return File ? Bytes : std::vector<Pico::uint8> {};
    };
    const auto WriteFileBytes = [&](const std::vector<Pico::uint8>& Bytes)
    {
        std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
        if (!Bytes.empty())
        {
            File.write(
                reinterpret_cast<const char*>(Bytes.data()),
                static_cast<std::streamsize>(Bytes.size()));
        }
        return File.good();
    };

    RemoveTestFiles();
    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "FileWorld");
    const bool bWorldReady = World != nullptr && World->Initialize();
    Pico::PActor* Actor =
        bWorldReady ? World->SpawnActor<Pico::PActor>("FileActor") : nullptr;
    Pico::PSceneComponent* Root =
        Actor != nullptr
        ? Actor->CreateComponent<Pico::PSceneComponent>("Root")
        : nullptr;
    Pico::PCubeComponent* Cube =
        Actor != nullptr
        ? Actor->CreateComponent<Pico::PCubeComponent>("Cube")
        : nullptr;
    const bool bSceneReady =
        Actor != nullptr
        && Root != nullptr
        && Cube != nullptr
        && Actor->SetRootComponent(Root)
        && Cube->AttachToComponent(
            Root,
            Pico::EAttachmentTransformRule::KeepRelative);
    Runner.Expect(
        bWorldReady && bSceneReady,
        "World file test creates a serializable runtime scene");
    if (!bWorldReady || !bSceneReady)
    {
        Pico::DestroyObjectTree(World);
        RemoveTestFiles();
        Pico::PObjectSystem::Shutdown();
        return;
    }

    Pico::EWorldSerializationError Error =
        Pico::EWorldSerializationError::None;
    Runner.Expect(
        !Pico::SaveWorldToFile({}, *World, &Error)
            && Error == Pico::EWorldSerializationError::InvalidArgument,
        "World file saving rejects an empty path");

    Cube->SetExtent(Pico::FVector3(10.0f, 20.0f, 30.0f));
    Runner.Expect(
        Pico::SaveWorldToFile(FilePath, *World, &Error)
            && Error == Pico::EWorldSerializationError::None
            && std::filesystem::is_regular_file(FilePath),
        "World saves to a persistent .pworld file");

    Cube->SetExtent(Pico::FVector3(40.0f, 50.0f, 60.0f));
    Runner.Expect(
        Pico::SaveWorldToFile(FilePath, *World, &Error),
        "Saving again atomically replaces an existing .pworld file");
    const std::vector<Pico::uint8> ReplacedBytes = ReadFileBytes();
    const auto ReadBackupBytes = [&]() -> std::vector<Pico::uint8>
    {
        std::ifstream File(BackupPath, std::ios::binary | std::ios::ate);
        if (!File) return {};
        const std::streampos EndPosition = File.tellg();
        if (EndPosition < 0) return {};
        std::vector<Pico::uint8> Bytes(static_cast<std::size_t>(EndPosition));
        File.seekg(0, std::ios::beg);
        if (!Bytes.empty())
        {
            File.read(
                reinterpret_cast<char*>(Bytes.data()),
                static_cast<std::streamsize>(Bytes.size()));
        }
        return File ? Bytes : std::vector<Pico::uint8> {};
    };
    Runner.Expect(
        !ReplacedBytes.empty()
            && Pico::SaveWorldToFile(FilePath, *World, &Error)
            && ReadFileBytes() == ReplacedBytes,
        "Repeated World saves produce deterministic file bytes");
    Runner.Expect(
        !std::filesystem::exists(TemporaryPath)
            && std::filesystem::is_regular_file(BackupPath)
            && ReadBackupBytes() == ReplacedBytes,
        "Successful World replacement keeps the previous version as a backup");

    Pico::FWorldAssetData FileAssetData;
    Runner.Expect(
        Pico::LoadWorldAssetDataFromFile(FilePath, FileAssetData, &Error)
            && Pico::SaveWorldAssetDataToFile(FilePath, FileAssetData, &Error)
            && ReadFileBytes() == ReplacedBytes,
        "Parsed World asset data can be edited and saved without reconstructing objects");

    const std::size_t ExistingObjectCount =
        Pico::FObjectRegistry::GetObjectCount();
    Runner.Expect(
        Pico::LoadWorldFromFile(FilePath, &Error) == nullptr
            && Error == Pico::EWorldSerializationError::ObjectCreationFailed
            && Pico::FObjectRegistry::GetObjectCount() == ExistingObjectCount
            && Pico::ResolveObject(World->GetHandle()) == World,
        "Loading a conflicting World name leaves the live World untouched");

    Pico::DestroyObjectTree(World);
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "World file test releases the source World before loading");

    Pico::PWorld* LoadedWorld = Pico::LoadWorldFromFile(FilePath, &Error);
    Pico::PLevel* LoadedLevel =
        LoadedWorld != nullptr ? LoadedWorld->GetPersistentLevel() : nullptr;
    Pico::PObject* LoadedActorObject = LoadedLevel != nullptr
        ? Pico::FindObject(LoadedLevel, Pico::FName("FileActor"))
        : nullptr;
    Pico::PObject* LoadedCubeObject = LoadedActorObject != nullptr
        ? Pico::FindObject(LoadedActorObject, Pico::FName("Cube"))
        : nullptr;
    Pico::PCubeComponent* LoadedCube =
        LoadedCubeObject != nullptr
            && LoadedCubeObject->IsA(Pico::PCubeComponent::StaticClass())
        ? static_cast<Pico::PCubeComponent*>(LoadedCubeObject)
        : nullptr;
    Runner.Expect(
        LoadedWorld != nullptr
            && Error == Pico::EWorldSerializationError::None
            && LoadedCube != nullptr
            && LoadedCube->GetExtent().Equals(
                Pico::FVector3(40.0f, 50.0f, 60.0f))
            && LoadedCube->GetAttachParent() != nullptr,
        "A .pworld file reconstructs properties and component relationships");
    Pico::DestroyObjectTree(LoadedWorld);
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Destroying a file-loaded World clears every reconstructed object");

    {
        std::ofstream TrailingFile(FilePath, std::ios::binary | std::ios::app);
        TrailingFile.put(static_cast<char>(0x7f));
    }
    Pico::FWorldAssetData UnchangedFileData;
    UnchangedFileData.WorldId = Pico::FSceneObjectId { 777 };
    Runner.Expect(
        !Pico::LoadWorldAssetDataFromFile(
            FilePath,
            UnchangedFileData,
            &Error)
            && Error == Pico::EWorldSerializationError::TrailingData
            && UnchangedFileData.WorldId.Value == 777
            && Pico::LoadWorldFromFile(FilePath, &Error) == nullptr
            && Pico::FObjectRegistry::GetObjectCount() == 0,
        "World file loading rejects trailing bytes without changing output data");

    std::vector<Pico::uint8> TruncatedBytes = ReplacedBytes;
    if (!TruncatedBytes.empty())
    {
        TruncatedBytes.pop_back();
    }
    Runner.Expect(
        WriteFileBytes(TruncatedBytes)
            && Pico::LoadWorldFromFile(FilePath, &Error) == nullptr
            && Error == Pico::EWorldSerializationError::InvalidArchive
            && Pico::FObjectRegistry::GetObjectCount() == 0,
        "World file loading rejects a truncated archive without object leaks");

    RemoveTestFiles();
    Runner.Expect(
        Pico::LoadWorldFromFile(FilePath, &Error) == nullptr
            && Error == Pico::EWorldSerializationError::FileOpenFailed,
        "World file loading reports a missing file");

    RemoveTestFiles();
    Pico::PObjectSystem::Shutdown();
}

void TestEngineLoopWorldReplacement(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=5";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    const auto UniqueSuffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path FilePath =
        std::filesystem::temp_directory_path()
        / ("PicoWorldReplacement_" + std::to_string(UniqueSuffix) + ".pworld");
    std::error_code ErrorCode;
    std::filesystem::remove(FilePath, ErrorCode);

    Pico::FEngineLoop EngineLoop;
    Runner.Expect(
        EngineLoop.PreInit(3, Arguments) == 0 && EngineLoop.Init() == 0,
        "World replacement test initializes the engine loop");
    Runner.Expect(
        PLoadTrackingActor::RegisterClass(),
        "World replacement test registers its PostLoad actor");

    Pico::PWorld* OldWorld = EngineLoop.GetWorld();
    PLoadTrackingActor* Actor = OldWorld != nullptr
        ? OldWorld->SpawnActor<PLoadTrackingActor>("ReplacementActor")
        : nullptr;
    Pico::PSceneComponent* Root = Actor != nullptr
        ? Actor->CreateComponent<Pico::PSceneComponent>("Root")
        : nullptr;
    Pico::PCubeComponent* Cube = Actor != nullptr
        ? Actor->CreateComponent<Pico::PCubeComponent>("Cube")
        : nullptr;
    const bool bSceneReady =
        Actor != nullptr
        && Root != nullptr
        && Cube != nullptr
        && Actor->SetRootComponent(Root)
        && Cube->AttachToComponent(
            Root,
            Pico::EAttachmentTransformRule::KeepRelative);
    Runner.Expect(
        bSceneReady,
        "World replacement test creates a persistent scene");
    if (!bSceneReady)
    {
        EngineLoop.Exit();
        std::filesystem::remove(FilePath, ErrorCode);
        return;
    }

    Cube->SetExtent(Pico::FVector3(15.0f, 25.0f, 35.0f));
    Pico::EWorldSerializationError Error =
        Pico::EWorldSerializationError::None;
    Runner.Expect(
        Pico::SaveWorldToFile(FilePath, *OldWorld, &Error),
        "World replacement test saves the active World");

    const Pico::FObjectHandle OldWorldHandle = OldWorld->GetHandle();
    const std::size_t OldObjectCount =
        Pico::FObjectRegistry::GetObjectCount();
    PLoadTrackingActor::bThrowPostLoad = true;
    Runner.Expect(
        !EngineLoop.LoadWorld(FilePath, &Error)
            && Error == Pico::EWorldSerializationError::PostLoadFailed
            && EngineLoop.GetWorld() == OldWorld
            && OldWorld->GetHandle() == OldWorldHandle
            && OldWorld->GetName() == Pico::FName("GameWorld")
            && Pico::FObjectRegistry::GetObjectCount() == OldObjectCount
            && Pico::FindObject(
                nullptr,
                Pico::FName("__PicoPreviousWorld_1")) == nullptr,
        "A failed replacement restores the original World name and handle");

    PLoadTrackingActor::bThrowPostLoad = false;
    PLoadTrackingActor::bObservedRootComponent = false;
    Cube->SetExtent(Pico::FVector3(90.0f, 90.0f, 90.0f));
    Runner.Expect(
        EngineLoop.LoadWorld(FilePath, &Error)
            && Error == Pico::EWorldSerializationError::None,
        "EngineLoop transactionally replaces the active World");

    Pico::PWorld* NewWorld = EngineLoop.GetWorld();
    Pico::PLevel* NewLevel =
        NewWorld != nullptr ? NewWorld->GetPersistentLevel() : nullptr;
    Pico::PObject* NewActorObject = NewLevel != nullptr
        ? Pico::FindObject(NewLevel, Pico::FName("ReplacementActor"))
        : nullptr;
    Pico::PObject* NewCubeObject = NewActorObject != nullptr
        ? Pico::FindObject(NewActorObject, Pico::FName("Cube"))
        : nullptr;
    Pico::PCubeComponent* NewCube =
        NewCubeObject != nullptr
            && NewCubeObject->IsA(Pico::PCubeComponent::StaticClass())
        ? static_cast<Pico::PCubeComponent*>(NewCubeObject)
        : nullptr;
    Runner.Expect(
        NewWorld != nullptr
            && NewWorld != OldWorld
            && NewWorld->GetName() == Pico::FName("GameWorld")
            && Pico::ResolveObject(OldWorldHandle) == nullptr
            && NewCube != nullptr
            && NewCube->GetExtent().Equals(
                Pico::FVector3(15.0f, 25.0f, 35.0f))
            && PLoadTrackingActor::bObservedRootComponent
            && Pico::FObjectRegistry::GetObjectCount() == OldObjectCount,
        "Successful replacement commits loaded data and destroys the old World");

    const Pico::FObjectHandle NewWorldHandle =
        NewWorld != nullptr ? NewWorld->GetHandle() : Pico::FObjectHandle {};
    {
        std::ofstream TrailingFile(FilePath, std::ios::binary | std::ios::app);
        TrailingFile.put(static_cast<char>(0x7f));
    }
    Runner.Expect(
        !EngineLoop.LoadWorld(FilePath, &Error)
            && Error == Pico::EWorldSerializationError::TrailingData
            && EngineLoop.GetWorld() == NewWorld
            && NewWorld != nullptr
            && NewWorld->GetHandle() == NewWorldHandle,
        "A malformed replacement file leaves the current World untouched");

    EngineLoop.Tick();
    Runner.Expect(
        NewWorld != nullptr && NewWorld->GetTickCount() == 1,
        "EngineLoop ticks the newly committed World");

    EngineLoop.Exit();
    std::filesystem::remove(FilePath, ErrorCode);
    PLoadTrackingActor::bThrowPostLoad = false;
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "World replacement test leaves no registered objects");
}

void TestEngineLoopWorldLifecycle(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=3";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    Pico::FEngineLoop EngineLoop;
    Runner.Expect(EngineLoop.PreInit(3, Arguments) == 0, "Engine loop pre-initialization succeeds");
    Runner.Expect(EngineLoop.GetWorld() == nullptr, "No world exists before engine initialization");
    Runner.Expect(EngineLoop.Init() == 0, "Engine initialization creates the runtime world");

    Pico::PWorld* World = EngineLoop.GetWorld();
    Runner.Expect(World != nullptr, "The initialized engine exposes its world");
    Runner.Expect(
        World != nullptr && World->GetState() == Pico::EWorldState::Initialized,
        "The engine world is initialized");
    Runner.Expect(
        World != nullptr && World->GetPersistentLevel() != nullptr,
        "The engine world has a persistent level");

    Pico::PActor* ScheduledGarbage =
        Pico::NewObject<Pico::PActor>(nullptr, "ScheduledGarbage");
    const Pico::FObjectHandle ScheduledGarbageHandle =
        ScheduledGarbage != nullptr
            ? ScheduledGarbage->GetHandle()
            : Pico::FObjectHandle {};
    Pico::RequestGarbageCollection();
    Runner.Expect(
        ScheduledGarbage != nullptr
            && Pico::ResolveObject(ScheduledGarbageHandle) == ScheduledGarbage,
        "Engine GC requests remain deferred until a frame safe point");

    EngineLoop.Tick();
    Runner.Expect(
        Pico::ResolveObject(ScheduledGarbageHandle) == nullptr
            && !Pico::IsGarbageCollectionRequested(),
        "EngineLoop performs pending GC after the World tick");
    EngineLoop.Tick();
    Runner.Expect(World != nullptr && World->GetTickCount() == 2, "Engine ticks are forwarded to the world");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 2, "Engine frame and world tick counts advance together");

    EngineLoop.Exit();
    EngineLoop.Exit();
    Runner.Expect(EngineLoop.GetWorld() == nullptr, "Engine exit invalidates the active world handle");
    Runner.Expect(!Pico::PObjectSystem::IsInitialized(), "Engine exit shuts down the object system");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Engine exit leaves no registered objects");
}

void TestCameraSpringArmSockets(FTestRunner& Runner)
{
    char Program[] = "PicoCameraSocketTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, MaxFPS };
    Pico::FEngineLoop EngineLoop;
    const bool bInitialized =
        EngineLoop.PreInit(2, Arguments) == 0 && EngineLoop.Init() == 0;
    Runner.Expect(bInitialized, "Camera socket test initializes the engine");
    if (!bInitialized)
    {
        EngineLoop.Exit();
        return;
    }

    Pico::PWorld* World = EngineLoop.GetWorld();
    Pico::PActor* Actor = World->SpawnActor<Pico::PActor>("CameraRig");
    Pico::PSpringArmComponent* SpringArm = Actor != nullptr
        ? Actor->CreateComponent<Pico::PSpringArmComponent>("SpringArm") : nullptr;
    Pico::PCameraComponent* Camera = Actor != nullptr
        ? Actor->CreateComponent<Pico::PCameraComponent>("Camera") : nullptr;
    const bool bRigCreated = Actor != nullptr
        && SpringArm != nullptr
        && Camera != nullptr
        && Actor->SetRootComponent(SpringArm)
        && Camera->AttachToComponent(
            SpringArm,
            Pico::EAttachmentTransformRule::KeepRelative,
            Pico::PSpringArmComponent::GetEndpointSocketName());
    Runner.Expect(bRigCreated, "A Camera attaches to the SpringArm endpoint socket");
    if (bRigCreated)
    {
        SpringArm->SetRelativeLocation(Pico::FVector3(100.0f, 0.0f, 50.0f));
        SpringArm->SetTargetArmLength(300.0f);
        SpringArm->SetSocketOffset(Pico::FVector3(0.0f, 20.0f, 10.0f));
        SpringArm->SetTargetOffset(Pico::FVector3(5.0f, 0.0f, 0.0f));
        Runner.Expect(
            Camera->GetViewPosition().Equals(
                Pico::FVector3(-195.0f, 20.0f, 60.0f), 0.001f),
            "The SpringArm endpoint drives the attached Camera world position");
        Runner.Expect(
            Camera->GetAttachSocketName()
                == Pico::PSpringArmComponent::GetEndpointSocketName(),
            "The Camera retains its named attachment socket");
    }

    Pico::FWorldAssetData Data;
    Pico::EWorldSerializationError Error = Pico::EWorldSerializationError::None;
    const bool bCaptured = Pico::CaptureWorld(*World, Data, &Error);
    bool bSocketCaptured = false;
    for (const Pico::FSceneRelationRecord& Relation : Data.Relations)
    {
        bSocketCaptured = bSocketCaptured
            || Relation.AttachSocketName == "SpringEndpoint";
    }
    Runner.Expect(
        bCaptured && bSocketCaptured,
        "World capture serializes named component sockets");
    Runner.Expect(
        bCaptured && EngineLoop.ReplaceWorld(Data, &Error),
        "World replacement restores a scene containing socket attachments");

    Pico::PCameraComponent* RestoredCamera = nullptr;
    for (Pico::PLevel* Level : EngineLoop.GetWorld()->GetLevels())
    {
        for (Pico::PActor* RestoredActor : Level->GetActors())
        {
            for (Pico::PActorComponent* Component : RestoredActor->GetComponents())
            {
                if (Component->IsA(Pico::PCameraComponent::StaticClass()))
                {
                    RestoredCamera = static_cast<Pico::PCameraComponent*>(Component);
                }
            }
        }
    }
    Runner.Expect(
        RestoredCamera != nullptr
            && RestoredCamera->GetAttachSocketName()
                == Pico::PSpringArmComponent::GetEndpointSocketName()
            && RestoredCamera->GetViewPosition().Equals(
                Pico::FVector3(-195.0f, 20.0f, 60.0f), 0.001f),
        "World replacement restores the socket name and Camera transform");

    EngineLoop.Exit();
}

void TestTwoFrameLifecycle(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=2";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    const int Result = Pico::GuardedMain(3, Arguments);

    Runner.Expect(Result == 0, "GuardedMain completes successfully");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 2, "Command line frame limit stops after two frames");
    Runner.Expect(Pico::FApp::IsExitRequested(), "Engine loop records the exit request");
    Runner.Expect(Pico::FApp::GetProjectName() == "Pico", "PreInit initializes the project name");
    Runner.Expect(!Pico::PObjectSystem::IsInitialized(), "GuardedMain shuts down the object system");
}

void TestZeroFrameLifecycle(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=0";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    const int Result = Pico::GuardedMain(3, Arguments);

    Runner.Expect(Result == 0, "A zero-frame run completes successfully");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 0, "A zero-frame run does not tick");
    Runner.Expect(Pico::FApp::IsExitRequested(), "A zero-frame run requests exit during initialization");
}

void TestInvalidFrameLimit(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=-2";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    const int Result = Pico::GuardedMain(3, Arguments);

    Runner.Expect(Result != 0, "An invalid frame limit fails during PreInit");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 0, "An invalid frame limit never ticks");
}
}

int main()
{
    FTestRunner Runner;
    TestWorldLifecycle(Runner);
    TestWorldOwnershipAndStaleHandles(Runner);
    TestActorSpawnLifecycleAndOwnership(Runner);
    TestActorLifecycleDelegates(Runner);
    TestActorDestroyDuringTick(Runner);
    TestTickExceptionSafety(Runner);
    TestActorComponentsAndSceneTransform(Runner);
    TestDefaultSubobjectTemplatesAndWorldRestore(Runner);
    TestSceneComponentAttachmentHierarchy(Runner);
    TestPrimitiveComponentSceneData(Runner);
    TestWorldAssetDataSerialization(Runner);
    TestStageHPersistenceAndPropertyNotifications(Runner);
    TestWorldFilePersistence(Runner);
    TestEngineLoopWorldReplacement(Runner);
    TestCameraSpringArmSockets(Runner);
    TestEngineLoopWorldLifecycle(Runner);
    TestTwoFrameLifecycle(Runner);
    TestZeroFrameLifecycle(Runner);
    TestInvalidFrameLimit(Runner);
    return Runner.Finish();
}
