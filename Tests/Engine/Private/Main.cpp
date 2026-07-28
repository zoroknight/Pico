#include "TestRunner.h"

#include "Pico/Core/App.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ObjectSystem.h"

#include <cmath>
#include <limits>

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
    inline static int TotalEndPlayCount = 0;

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
    const bool bCountingSceneComponentRegistered = PCountingSceneComponent::RegisterClass();
    const bool bActorRegistered = Pico::PActor::RegisterClass();
    const bool bCountingActorRegistered = PCountingActor::RegisterClass();
    const bool bSelfDestroyActorRegistered = PSelfDestroyActor::RegisterClass();
    const bool bLevelRegistered = Pico::PLevel::RegisterClass();
    const bool bWorldRegistered = Pico::PWorld::RegisterClass();
    Runner.Expect(bActorComponentRegistered, "PActorComponent registers with the class registry");
    Runner.Expect(bSceneComponentRegistered, "PSceneComponent registers with the class registry");
    Runner.Expect(bCountingSceneComponentRegistered, "A test scene component registers with the class registry");
    Runner.Expect(bActorRegistered, "PActor registers with the class registry");
    Runner.Expect(bCountingActorRegistered, "A test actor registers with the class registry");
    Runner.Expect(bSelfDestroyActorRegistered, "A self-destroying test actor registers with the class registry");
    Runner.Expect(bLevelRegistered, "PLevel registers with the class registry");
    Runner.Expect(bWorldRegistered, "PWorld registers with the class registry");
    return bActorComponentRegistered
        && bSceneComponentRegistered
        && bCountingSceneComponentRegistered
        && bActorRegistered
        && bCountingActorRegistered
        && bSelfDestroyActorRegistered
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

    EngineLoop.Tick();
    EngineLoop.Tick();
    Runner.Expect(World != nullptr && World->GetTickCount() == 2, "Engine ticks are forwarded to the world");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 2, "Engine frame and world tick counts advance together");

    EngineLoop.Exit();
    EngineLoop.Exit();
    Runner.Expect(EngineLoop.GetWorld() == nullptr, "Engine exit invalidates the active world handle");
    Runner.Expect(!Pico::PObjectSystem::IsInitialized(), "Engine exit shuts down the object system");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Engine exit leaves no registered objects");
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
    TestActorDestroyDuringTick(Runner);
    TestActorComponentsAndSceneTransform(Runner);
    TestEngineLoopWorldLifecycle(Runner);
    TestTwoFrameLifecycle(Runner);
    TestZeroFrameLifecycle(Runner);
    TestInvalidFrameLimit(Runner);
    return Runner.Finish();
}
