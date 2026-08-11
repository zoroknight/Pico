#include "TestRunner.h"

#include "Pico/Engine/Controller.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/FloatingPawnMovement.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/PhysicsCore/WorldCollisionQuery.h"

#include <cmath>
#include <thread>

namespace
{
class FTestCollisionQuery final : public Pico::IWorldCollisionQuery
{
public:
    bool Sweep(
        const Pico::FCollisionShape& Shape,
        const Pico::FVector3& Start,
        const Pico::FVector3& End,
        const Pico::FQuat&,
        const Pico::FCollisionQueryParams& Params,
        Pico::FHitResult& OutHit) const override
    {
        ++SweepCount;
        LastShape = Shape;
        LastMovingObject = Params.MovingObject;
        OutHit.Reset(Start, End);
        if (!bBlockNextSweep)
        {
            return false;
        }
        bBlockNextSweep = false;
        OutHit.bBlockingHit = true;
        OutHit.bStartPenetrating = bStartPenetrating;
        OutHit.Time = HitTime;
        OutHit.Normal = HitNormal;
        OutHit.ImpactNormal = HitNormal;
        OutHit.PenetrationDepth = PenetrationDepth;
        OutHit.ImpactPoint = Start + (End - Start) * HitTime;
        return true;
    }

    void BlockNext(
        float Time,
        const Pico::FVector3& Normal,
        bool bInStartPenetrating = false,
        float InPenetrationDepth = 0.0f)
    {
        bBlockNextSweep = true;
        bStartPenetrating = bInStartPenetrating;
        HitTime = Time;
        HitNormal = Normal;
        PenetrationDepth = InPenetrationDepth;
    }

    mutable int SweepCount = 0;
    mutable Pico::FCollisionShape LastShape;
    mutable Pico::FObjectHandle LastMovingObject;
    mutable bool bBlockNextSweep = false;
    bool bStartPenetrating = false;
    float HitTime = 1.0f;
    float PenetrationDepth = 0.0f;
    Pico::FVector3 HitNormal = Pico::FVector3::ZeroVector;
};
}

int main()
{
    FTestRunner Runner;
    Pico::FEngineLoop EngineLoop;
    char ProgramName[] = "PicoMovementTests";
    char Frames[] = "-frames=-1";
    char* Arguments[] = { ProgramName, Frames };
    Runner.Expect(
        EngineLoop.PreInit(2, Arguments) == 0 && EngineLoop.Init() == 0,
        "Movement tests initialize the Engine and World");

    Pico::PWorld* World = EngineLoop.GetWorld();
    Pico::PController* Controller = World != nullptr
        ? World->SpawnActor<Pico::PController>("MovementController")
        : nullptr;
    Pico::PPawn* Pawn = World != nullptr
        ? World->SpawnActor<Pico::PPawn>("MovementPawn")
        : nullptr;
    Pico::PSceneComponent* Root = Pawn != nullptr
        ? Pawn->CreateComponent<Pico::PSceneComponent>("Root")
        : nullptr;
    Pico::PFloatingPawnMovement* Movement = Pawn != nullptr
        ? Pawn->CreateComponent<Pico::PFloatingPawnMovement>("Movement")
        : nullptr;
    Runner.Expect(
        World != nullptr && Controller != nullptr && Pawn != nullptr
            && Root != nullptr && Movement != nullptr
            && Pawn->SetRootComponent(Root)
            && Controller->Possess(Pawn),
        "Movement test creates a possessed Pawn with a root and MovementComponent");
    if (World == nullptr || Controller == nullptr || Pawn == nullptr
        || Root == nullptr || Movement == nullptr)
    {
        EngineLoop.Exit();
        return Runner.Finish();
    }

    Pawn->AddMovementInput(Pico::FVector3(2.0f, 0.0f, 0.0f));
    Pawn->AddMovementInput(Pico::FVector3(0.0f, 2.0f, 0.0f));
    Runner.Expect(
        std::abs(Pawn->GetPendingMovementInputVector().Size() - 1.0f) < 0.0001f,
        "Pawn accumulates and clamps movement input to unit length");
    const Pico::FVector3 ConsumedInput = Pawn->ConsumeMovementInputVector();
    Runner.Expect(
        !ConsumedInput.IsNearlyZero()
            && Pawn->GetPendingMovementInputVector().IsNearlyZero()
            && Pawn->GetLastMovementInputVector().Equals(ConsumedInput),
        "Pawn consumes pending input exactly once and records the last vector");
    Runner.Expect(
        Pawn->ConsumeMovementInputVector().IsNearlyZero()
            && Pawn->GetLastMovementInputVector().IsNearlyZero(),
        "A second input consumption returns zero for the same frame");

    World->Tick(0.0f);
    Runner.Expect(
        Movement->GetUpdatedComponent() == Root,
        "MovementComponent selects its owning Pawn root as UpdatedComponent");
    Runner.Expect(
        Movement->PrimaryComponentTick.HasPrerequisite(Controller->PrimaryActorTick),
        "Possession establishes Controller before Movement tick ordering");

    Pico::PActor* OtherActor = World->SpawnActor<Pico::PActor>("OtherActor");
    Pico::PSceneComponent* OtherRoot = OtherActor != nullptr
        ? OtherActor->CreateComponent<Pico::PSceneComponent>("OtherRoot")
        : nullptr;
    if (OtherActor != nullptr && OtherRoot != nullptr)
    {
        OtherActor->SetRootComponent(OtherRoot);
    }
    Runner.Expect(
        OtherRoot != nullptr && !Movement->SetUpdatedComponent(OtherRoot)
            && Movement->GetUpdatedComponent() == Root,
        "MovementComponent rejects an UpdatedComponent owned by another Actor");

    bool bWorkerMoveResult = true;
    std::thread Worker(
        [&]()
        {
            bWorkerMoveResult = Root->MoveComponent(
                Pico::FVector3(10.0f, 0.0f, 0.0f),
                Pico::FQuat::Identity,
                false);
            Pawn->AddMovementInput(Pico::FVector3::ForwardVector);
        });
    Worker.join();
    Runner.Expect(
        !bWorkerMoveResult && Pawn->GetPendingMovementInputVector().IsNearlyZero(),
        "Worker threads cannot mutate Pawn input or SceneComponent movement");

    FTestCollisionQuery Query;
    World->SetCollisionQuery(&Query);
    Pico::FHitResult Hit;
    Query.BlockNext(0.25f, Pico::FVector3(-1.0f, 0.0f, 0.0f));
    const Pico::FVector3 SweepStart = Root->GetWorldTransform().Translation;
    Runner.Expect(
        Root->MoveComponent(
            Pico::FVector3(100.0f, 0.0f, 0.0f),
            Pico::FQuat::Identity,
            true,
            &Hit)
            && Hit.bBlockingHit
            && std::abs(Hit.Time - 0.25f) < 0.0001f
            && Root->GetWorldTransform().Translation.Equals(
                SweepStart + Pico::FVector3(25.0f, 0.0f, 0.0f)),
        "SceneComponent Sweep stops at the blocking hit time");
    Runner.Expect(
        Query.SweepCount == 1
            && Query.LastShape.Type == Pico::ECollisionShapeType::Point
            && Query.LastMovingObject == Root->GetHandle(),
        "SceneComponent submits its Pico collision shape and stable object handle");

    const int SweepCountBeforeTeleport = Query.SweepCount;
    Runner.Expect(
        Root->MoveComponent(
            Pico::FVector3(10.0f, 0.0f, 0.0f),
            Pico::FQuat::Identity,
            true,
            &Hit,
            Pico::EMoveComponentFlags::None,
            Pico::ETeleportType::TeleportPhysics)
            && Query.SweepCount == SweepCountBeforeTeleport
            && !Hit.bBlockingHit,
        "Teleport moves immediately without issuing a Sweep query");

    Pico::PSceneComponent* Child =
        Pawn->CreateComponent<Pico::PSceneComponent>("AttachedChild");
    Runner.Expect(
        Child != nullptr
            && Child->AttachToComponent(
                Root, Pico::EAttachmentTransformRule::KeepRelative),
        "Movement test attaches a child SceneComponent to UpdatedComponent");
    const Pico::FVector3 ChildStart = Child != nullptr
        ? Child->GetWorldTransform().Translation
        : Pico::FVector3::ZeroVector;
    Movement->MoveUpdatedComponent(
        Pico::FVector3(0.0f, 5.0f, 0.0f),
        Pico::FQuat::Identity,
        false);
    Runner.Expect(
        Child != nullptr
            && Child->GetWorldTransform().Translation.Equals(
                ChildStart + Pico::FVector3(0.0f, 5.0f, 0.0f)),
        "Moving UpdatedComponent preserves attachment propagation");

    const Pico::FVector3 SlideStart = Root->GetWorldTransform().Translation;
    Query.BlockNext(0.5f, Pico::FVector3(-1.0f, 0.0f, 0.0f));
    const Pico::FVector3 DiagonalDelta(10.0f, 10.0f, 0.0f);
    Movement->SafeMoveUpdatedComponent(
        DiagonalDelta, Pico::FQuat::Identity, true, &Hit);
    const Pico::FHitResult InitialHit = Hit;
    Movement->SlideAlongSurface(
        DiagonalDelta,
        1.0f - InitialHit.Time,
        InitialHit.Normal,
        Hit);
    Runner.Expect(
        Root->GetWorldTransform().Translation.Equals(
            SlideStart + Pico::FVector3(5.0f, 10.0f, 0.0f)),
        "MovementComponent projects remaining movement along a blocking surface");
    Runner.Expect(
        Movement->GetLastHitResult().bBlockingHit
            && std::abs(Movement->GetLastHitResult().Time - 0.5f) < 0.0001f,
        "A clear slide sub-move preserves the original blocking hit for diagnostics");

    const Pico::FVector3 PenetrationStart = Root->GetWorldTransform().Translation;
    Query.BlockNext(
        0.0f,
        Pico::FVector3(0.0f, 1.0f, 0.0f),
        true,
        1.0f);
    Runner.Expect(
        Movement->SafeMoveUpdatedComponent(
            Pico::FVector3(0.0f, 5.0f, 0.0f),
            Pico::FQuat::Identity,
            true,
            &Hit)
            && Root->GetWorldTransform().Translation.Y
                > PenetrationStart.Y + 5.0f,
        "SafeMove depenetrates before retrying the requested movement");

    Movement->SetMaxSpeed(200.0f);
    Movement->SetAcceleration(1000.0f);
    Movement->SetDeceleration(1000.0f);
    Pawn->AddMovementInput(Pico::FVector3::ForwardVector);
    const Pico::FVector3 MovementStart = Pawn->GetActorLocation();
    World->Tick(0.1f);
    Runner.Expect(
        Pawn->GetPendingMovementInputVector().IsNearlyZero()
            && Pawn->GetLastMovementInputVector().Equals(
                Pico::FVector3::ForwardVector)
            && Movement->GetVelocity().Equals(Pico::FVector3(100.0f, 0.0f, 0.0f))
            && Pawn->GetActorLocation().Equals(
                MovementStart + Pico::FVector3(10.0f, 0.0f, 0.0f)),
        "FloatingPawnMovement consumes input, accelerates, and moves UpdatedComponent");

    Pawn->AddMovementInput(Pico::FVector3::ForwardVector);
    Query.BlockNext(0.5f, Pico::FVector3(-1.0f, 0.0f, 0.0f));
    World->Tick(0.1f);
    Runner.Expect(
        Movement->GetVelocity().IsNearlyZero(),
        "FloatingPawnMovement removes velocity into a blocking surface");

    World->Tick(0.1f);
    Runner.Expect(
        Movement->GetVelocity().IsNearlyZero(),
        "FloatingPawnMovement decelerates to rest without input");
    Movement->SetVelocity(Pico::FVector3(50.0f, 0.0f, 0.0f));
    Movement->SetMaxSpeed(0.0f);
    World->Tick(0.1f);
    Runner.Expect(
        Movement->GetVelocity().IsNearlyZero(),
        "Zero maximum speed immediately clears residual velocity");
    Movement->SetVelocity(Pico::FVector3(
        std::nanf(""), 1.0f, 0.0f));
    Runner.Expect(
        Movement->GetVelocity().IsNearlyZero(),
        "MovementComponent rejects non-finite velocity state");
    Controller->UnPossess();
    Runner.Expect(
        !Movement->PrimaryComponentTick.HasPrerequisite(Controller->PrimaryActorTick),
        "UnPossess removes the Controller tick prerequisite");

    World->SetCollisionQuery(nullptr);
    EngineLoop.Exit();
    return Runner.Finish();
}
