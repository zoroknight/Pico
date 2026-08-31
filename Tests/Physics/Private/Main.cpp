#include "TestRunner.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/TickFunction.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/PhysicsCore/PhysicsScene.h"

#include <cmath>

namespace
{
Pico::PCubeComponent* SpawnPhysicsCube(
    Pico::PWorld& World,
    const char* Name,
    const Pico::FVector3& Location,
    const Pico::FVector3& Extent,
    Pico::EPhysicsBodyType BodyType,
    bool bSensor = false)
{
    Pico::PActor* Actor = World.SpawnActor<Pico::PActor>(Name);
    Pico::PCubeComponent* Cube = Actor != nullptr
        ? Actor->CreateComponent<Pico::PCubeComponent>("Cube")
        : nullptr;
    if (Actor == nullptr || Cube == nullptr || !Actor->SetRootComponent(Cube)) return nullptr;
    Cube->SetExtent(Extent);
    Cube->SetWorldTransform(Pico::FTransform(Location));
    Cube->SetPhysicsBodyType(BodyType);
    Cube->SetSensor(bSensor);
    Cube->SetCollisionEnabled(
        bSensor
            ? Pico::ECollisionEnabled::QueryOnly
            : Pico::ECollisionEnabled::QueryAndPhysics);
    return Cube;
}
}

int main()
{
    FTestRunner Runner;
    Pico::FEngineLoop EngineLoop;
    char ProgramName[] = "PicoPhysicsTests";
    char Frames[] = "-frames=-1";
    char* Arguments[] = { ProgramName, Frames };
    Runner.Expect(
        EngineLoop.PreInit(2, Arguments) == 0 && EngineLoop.Init() == 0,
        "Physics tests initialize a World with the Jolt backend");

    Pico::PWorld* World = EngineLoop.GetWorld();
    Runner.Expect(
        World != nullptr && World->GetPhysicsScene() != nullptr
            && World->GetPhysicsScene()->IsValid(),
        "World owns a valid IPhysicsScene without exposing Jolt types");
    if (World == nullptr || World->GetPhysicsScene() == nullptr)
    {
        EngineLoop.Exit();
        return Runner.Finish();
    }

    Pico::FTickTaskManager& TickManager = World->GetTickTaskManager();
    Runner.Expect(
        TickManager.BeginFrame(1.0f / 60.0f)
            && TickManager.RunTickGroup(Pico::ETickGroup::PrePhysics)
            && !TickManager.RunTickGroup(Pico::ETickGroup::PostPhysics)
            && TickManager.RunTickGroup(Pico::ETickGroup::DuringPhysics)
            && TickManager.RunTickGroup(Pico::ETickGroup::PostPhysics)
            && TickManager.RunTickGroup(Pico::ETickGroup::PostUpdateWork),
        "Tick groups execute only in explicit frame order");
    TickManager.EndFrame();
    Runner.Expect(!TickManager.IsTicking(), "EndFrame closes the staged Tick frame");

    Pico::PCubeComponent* Floor = SpawnPhysicsCube(
        *World, "Floor", {0.0f, 0.0f, -25.0f}, {500.0f, 500.0f, 25.0f},
        Pico::EPhysicsBodyType::Static);
    Pico::PCubeComponent* Wall = SpawnPhysicsCube(
        *World, "Wall", {150.0f, 0.0f, 75.0f}, {20.0f, 120.0f, 75.0f},
        Pico::EPhysicsBodyType::Static);
    Pico::PCubeComponent* Dynamic = SpawnPhysicsCube(
        *World, "Dynamic", {0.0f, 0.0f, 250.0f}, {40.0f, 40.0f, 40.0f},
        Pico::EPhysicsBodyType::Dynamic);
    Pico::PCubeComponent* Trigger = SpawnPhysicsCube(
        *World, "Trigger", {-250.0f, 0.0f, 50.0f}, {60.0f, 60.0f, 60.0f},
        Pico::EPhysicsBodyType::Static, true);
    Pico::PCubeComponent* Probe = SpawnPhysicsCube(
        *World, "Probe", {-250.0f, 0.0f, 50.0f}, {20.0f, 20.0f, 20.0f},
        Pico::EPhysicsBodyType::Kinematic);
    Runner.Expect(
        Floor != nullptr && Wall != nullptr && Dynamic != nullptr
            && Trigger != nullptr && Probe != nullptr,
        "Physics test creates static, dynamic, kinematic, and sensor components");
    if (Floor == nullptr || Wall == nullptr || Dynamic == nullptr
        || Trigger == nullptr || Probe == nullptr)
    {
        EngineLoop.Exit();
        return Runner.Finish();
    }

    int BeginOverlapEvents = 0;
    int EndOverlapEvents = 0;
    Trigger->OnComponentBeginOverlap().AddLambda(
        [&BeginOverlapEvents](Pico::PPrimitiveComponent*, const Pico::FPhysicsContactEvent&)
        {
            ++BeginOverlapEvents;
        });
    Trigger->OnComponentEndOverlap().AddLambda(
        [&EndOverlapEvents](Pico::PPrimitiveComponent*, const Pico::FPhysicsContactEvent&)
        {
            ++EndOverlapEvents;
        });

    World->Tick(1.0f / 60.0f);
    Runner.Expect(
        Floor->GetPhysicsBodyHandle().IsValid()
            && Dynamic->GetPhysicsBodyHandle().IsValid()
            && Probe->GetPhysicsBodyHandle().IsValid(),
        "Registered PrimitiveComponents receive stable Pico body handles");
    Runner.Expect(
        BeginOverlapEvents > 0 && World->GetPhysicsBeginOverlapCount() > 0,
        "Jolt sensor contacts become PostPhysics overlap delegate broadcasts");

    const Pico::FVector3 ProbeStart = Probe->GetWorldTransform().Translation;
    Probe->MoveComponent(
        Pico::FVector3(0.0f, 300.0f, 0.0f),
        Pico::FQuat::Identity,
        false,
        nullptr,
        Pico::EMoveComponentFlags::None,
        Pico::ETeleportType::TeleportPhysics);
    World->Tick(1.0f / 60.0f);
    Runner.Expect(
        !Probe->GetWorldTransform().Translation.Equals(ProbeStart)
            && EndOverlapEvents > 0
            && World->GetPhysicsEndOverlapCount() > 0,
        "Moving a kinematic body out of a sensor produces EndOverlap");

    const int BeginBeforePass = BeginOverlapEvents;
    const int EndBeforePass = EndOverlapEvents;
    const Pico::uint64 WorldBeginBeforePass = World->GetPhysicsBeginOverlapCount();
    const Pico::uint64 WorldEndBeforePass = World->GetPhysicsEndOverlapCount();
    for (int Step = 0; Step < 30; ++Step)
    {
        Probe->MoveComponent(
            Pico::FVector3(0.0f, -20.0f, 0.0f),
            Pico::FQuat::Identity,
            false,
            nullptr,
            Pico::EMoveComponentFlags::None,
            Pico::ETeleportType::TeleportPhysics);
        World->Tick(1.0f / 60.0f);
    }
    Runner.Expect(
        BeginOverlapEvents == BeginBeforePass + 1
            && EndOverlapEvents == EndBeforePass + 1
            && World->GetPhysicsBeginOverlapCount() == WorldBeginBeforePass + 1
            && World->GetPhysicsEndOverlapCount() == WorldEndBeforePass + 1,
        "One continuous sensor passage emits exactly one BeginOverlap and one EndOverlap");

    for (int Step = 0; Step < 180; ++Step)
    {
        World->Tick(1.0f / 60.0f);
    }
    const float DynamicHeight = Dynamic->GetWorldTransform().Translation.Z;
    Runner.Expect(
        DynamicHeight < 100.0f && DynamicHeight > 35.0f,
        "Dynamic body falls under gravity and writes its settled Transform back to Component");

    Pico::FCollisionQueryParams QueryParams;
    QueryParams.IgnoredObjects.push_back(Dynamic->GetHandle());
    Pico::FHitResult RayHit;
    Runner.Expect(
        World->GetCollisionQuery()->Raycast(
            {0.0f, 0.0f, 400.0f},
            {0.0f, 0.0f, -200.0f},
            QueryParams,
            RayHit)
            && RayHit.HitObject == Floor->GetHandle(),
        "Raycast returns the hit PrimitiveComponent through FObjectHandle");

    Pico::FHitResult SweepHit;
    Pico::FCollisionQueryParams SweepParams;
    SweepParams.IgnoredObjects.push_back(Dynamic->GetHandle());
    Runner.Expect(
        World->GetCollisionQuery()->Sweep(
            Pico::FCollisionShape::MakeSphere(20.0f),
            {-200.0f, 0.0f, 75.0f},
            {300.0f, 0.0f, 75.0f},
            Pico::FQuat::Identity,
            SweepParams,
            SweepHit)
            && SweepHit.bBlockingHit
            && SweepHit.HitObject == Wall->GetHandle()
            && SweepHit.Time > 0.0f
            && SweepHit.Time < 1.0f,
        "Sweep reports the earliest blocking wall with normalized hit time");

    Pico::PActor* PawnA = World->SpawnActor<Pico::PActor>("PawnA");
    Pico::PActor* PawnB = World->SpawnActor<Pico::PActor>("PawnB");
    auto* PawnCapsuleA = PawnA != nullptr
        ? PawnA->CreateComponent<Pico::PCapsuleComponent>("Capsule") : nullptr;
    auto* PawnCapsuleB = PawnB != nullptr
        ? PawnB->CreateComponent<Pico::PCapsuleComponent>("Capsule") : nullptr;
    const bool bPawnPairReady = PawnA != nullptr && PawnB != nullptr
        && PawnCapsuleA != nullptr && PawnCapsuleB != nullptr
        && PawnA->SetRootComponent(PawnCapsuleA)
        && PawnB->SetRootComponent(PawnCapsuleB);
    if (bPawnPairReady)
    {
        PawnCapsuleA->SetPhysicsBodyType(Pico::EPhysicsBodyType::Kinematic);
        PawnCapsuleB->SetPhysicsBodyType(Pico::EPhysicsBodyType::Kinematic);
        PawnCapsuleA->SetCollisionProfile(Pico::ECollisionProfile::Pawn);
        PawnCapsuleB->SetCollisionProfile(Pico::ECollisionProfile::Pawn);
        PawnA->SetActorLocation({0.0f, 1000.0f, 96.0f});
        PawnB->SetActorLocation({120.0f, 1000.0f, 96.0f});
    }
    Pico::FHitResult PawnHit;
    const bool bPawnBlocked = bPawnPairReady
        && PawnCapsuleA->MoveComponent(
            {100.0f, 0.0f, 0.0f}, Pico::FQuat::Identity, true, &PawnHit)
        && PawnHit.bBlockingHit
        && PawnHit.HitObject == PawnCapsuleB->GetHandle()
        && PawnCapsuleA->GetWorldTransform().Translation.X < 50.0f;
    Runner.Expect(bPawnBlocked,
        "Pawn collision profile blocks another Pawn capsule through Sweep");
    if (bPawnPairReady)
    {
        PawnA->SetActorLocation({0.0f, 1000.0f, 96.0f});
        PawnCapsuleB->SetCollisionProfile(
            Pico::ECollisionProfile::PawnNoPawnCollision);
    }
    PawnHit.Reset({}, {});
    const bool bPawnIgnored = bPawnPairReady
        && PawnCapsuleA->MoveComponent(
            {100.0f, 0.0f, 0.0f}, Pico::FQuat::Identity, true, &PawnHit)
        && !PawnHit.bBlockingHit
        && std::abs(PawnCapsuleA->GetWorldTransform().Translation.X - 100.0f)
            < 0.01f;
    Runner.Expect(bPawnIgnored,
        "Pawn Ignore Pawns profile preserves World blocking while allowing Pawn passage");
    if (bPawnPairReady)
    {
        PawnA->SetActorLocation({0.0f, 1000.0f, 96.0f});
        PawnCapsuleB->SetCollisionProfile(Pico::ECollisionProfile::Pawn);
        PawnCapsuleB->SetPhysicsContactEnabled(false);
    }
    PawnHit.Reset({}, {});
    const bool bQueryOnlyPawnBlocked = bPawnPairReady
        && PawnCapsuleA->MoveComponent(
            {100.0f, 0.0f, 0.0f}, Pico::FQuat::Identity, true, &PawnHit)
        && PawnHit.bBlockingHit
        && PawnHit.HitObject == PawnCapsuleB->GetHandle();
    Runner.Expect(bQueryOnlyPawnBlocked,
        "Query-only Pawn remains a blocking Sweep target without physics contacts");

    Pico::PActor* CameraRig = World->SpawnActor<Pico::PActor>("CameraRig");
    Pico::PSpringArmComponent* CameraBoom = CameraRig != nullptr
        ? CameraRig->CreateComponent<Pico::PSpringArmComponent>("CameraBoom")
        : nullptr;
    Pico::PCameraComponent* FollowCamera = CameraRig != nullptr
        ? CameraRig->CreateComponent<Pico::PCameraComponent>("FollowCamera")
        : nullptr;
    const bool bCameraRigCreated = CameraRig != nullptr
        && CameraBoom != nullptr
        && FollowCamera != nullptr
        && CameraRig->SetRootComponent(CameraBoom)
        && FollowCamera->AttachToComponent(
            CameraBoom,
            Pico::EAttachmentTransformRule::KeepRelative,
            Pico::PSpringArmComponent::GetEndpointSocketName());
    Runner.Expect(bCameraRigCreated, "Physics test creates a SpringArm Camera rig");
    if (bCameraRigCreated)
    {
        CameraRig->SetActorLocation({300.0f, 0.0f, 75.0f});
        CameraBoom->SetTargetArmLength(400.0f);
        CameraBoom->SetProbeSize(12.0f);
        CameraBoom->SetCollisionTestEnabled(true);
        const float RetractedX = FollowCamera->GetViewPosition().X;
        CameraBoom->SetCollisionTestEnabled(false);
        const float UnfixedX = FollowCamera->GetViewPosition().X;
        Runner.Expect(
            RetractedX > -100.0f && RetractedX < 300.0f
                && std::abs(UnfixedX + 100.0f) < 0.001f,
            "Jolt wall Sweep retracts the Camera and the per-component toggle restores full arm length");
    }

    std::vector<Pico::FOverlapResult> Overlaps;
    Runner.Expect(
        World->GetCollisionQuery()->Overlap(
            Pico::FCollisionShape::MakeBox({30.0f, 30.0f, 30.0f}),
            Wall->GetWorldTransform().Translation,
            Pico::FQuat::Identity,
            {},
            Overlaps)
            && !Overlaps.empty(),
        "Overlap returns bodies intersecting a Pico collision shape");

    const Pico::FPhysicsBodyHandle RemovedHandle = Dynamic->GetPhysicsBodyHandle();
    Pico::PActor* DynamicActor = Dynamic->GetOwner();
    Runner.Expect(
        DynamicActor != nullptr && World->DestroyActor(DynamicActor),
        "DestroyActor unregisters the dynamic PrimitiveComponent");
    Pico::FPhysicsBodyState RemovedState;
    Runner.Expect(
        !World->GetPhysicsScene()->GetBodyState(RemovedHandle, RemovedState),
        "Destroyed components cannot resolve stale physics body handles");

    Pico::FWorldAssetData Snapshot;
    Pico::EWorldSerializationError WorldError = Pico::EWorldSerializationError::None;
    const Pico::uint64 PreviousStepCount = World->GetPhysicsStepCount();
    Runner.Expect(
        Pico::CaptureWorld(*World, Snapshot, &WorldError)
            && EngineLoop.ReplaceWorld(Snapshot, &WorldError),
        "World replacement reconstructs serialized scene data");
    World = EngineLoop.GetWorld();
    Runner.Expect(
        World != nullptr && World->GetPhysicsScene() != nullptr
            && World->GetPhysicsScene()->IsValid(),
        "A deserialized replacement World initializes its Jolt runtime service");
    if (World != nullptr && World->GetPhysicsScene() != nullptr)
    {
        World->Tick(1.0f / 60.0f);
        Runner.Expect(
            World->GetPhysicsStepCount() == 1
                && World->GetPhysicsStepCount() != PreviousStepCount,
            "A deserialized replacement World advances its own physics scene");
        const Pico::uint64 StepsBeforeLongFrame =
            World->GetPhysicsStepCount();
        World->Tick(1.0f);
        Runner.Expect(
            World->GetPhysicsStepCount() - StepsBeforeLongFrame == 4,
            "A long frame is capped to four fixed Jolt substeps");
    }

    EngineLoop.Exit();
    return Runner.Finish();
}
