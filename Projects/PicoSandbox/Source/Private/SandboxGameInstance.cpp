#include "PicoSandbox/SandboxGameInstance.h"

#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/ObjectGlobals.h"
#include "PicoSandbox/SandboxReplicationLabActor.h"

#include <iomanip>
#include <sstream>

namespace PicoSandbox
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxGameInstance)

PSandboxGameInstance::PSandboxGameInstance(
    const Pico::FObjectConstructionParams& Params)
    : PGameInstance(Params)
{
}

bool PSandboxGameInstance::Init(Pico::FGameEngine& GameEngine)
{
    return Pico::PGameInstance::Init(GameEngine);
}

void PSandboxGameInstance::OnWorldInitialized(Pico::PWorld*)
{
    ReplicationLabActorHandle = {};
    ReplicationLabMoveStep = 0;
    const Pico::FGameEngine* GameEngine = GetGameEngine();
    if (GameEngine != nullptr
        && GameEngine->GetNetDriver().GetNetMode() != Pico::ENetMode::Client)
    {
        ReplicationLabLastAction = SpawnReplicationLabActor()
            ? "authority spawned the lab actor"
            : "authority failed to spawn the lab actor";
    }
    else
    {
        ReplicationLabLastAction = "client waiting for replicated spawn";
    }
}

void PSandboxGameInstance::Tick(float)
{
    Pico::FGameEngine* GameEngine = GetGameEngine();
    if (GameEngine == nullptr)
    {
        return;
    }

    PSandboxReplicationLabActor* Actor = ResolveReplicationLabActor();
    if (Actor == nullptr)
    {
        Actor = FindReplicationLabActor();
        ReplicationLabActorHandle = Actor != nullptr
            ? Actor->GetHandle() : Pico::FObjectHandle {};
    }

    if (GameEngine->GetNetDriver().GetNetMode() == Pico::ENetMode::Client)
    {
        if (Actor != nullptr && ReplicationLabLastAction == "client waiting for replicated spawn")
        {
            ReplicationLabLastAction = "client received replicated spawn";
        }
        return;
    }

    Pico::FInputSystem& Input = GameEngine->GetInputSystem();
    if (Input.WasKeyPressed(Pico::EKey::T) && Actor == nullptr)
    {
        ReplicationLabLastAction = SpawnReplicationLabActor()
            ? "T: spawned the lab actor"
            : "T: spawn failed";
        Actor = ResolveReplicationLabActor();
    }
    if (Actor == nullptr)
    {
        return;
    }

    if (Input.WasKeyPressed(Pico::EKey::Y))
    {
        static constexpr Pico::FVector3 Positions[] = {
            {180.0f, 0.0f, 130.0f},
            {180.0f, 180.0f, 200.0f},
            {180.0f, -180.0f, 90.0f},
            {360.0f, 0.0f, 160.0f}
        };
        ReplicationLabMoveStep =
            (ReplicationLabMoveStep + 1) % static_cast<Pico::int32>(std::size(Positions));
        Actor->SetActorLocation(Positions[ReplicationLabMoveStep]);
        ReplicationLabLastAction = "Y: changed authority Transform";
    }
    if (Input.WasKeyPressed(Pico::EKey::U))
    {
        Actor->AdvanceRevision();
        ReplicationLabLastAction = "U: changed replicated revision and color";
    }
    if (Input.WasKeyPressed(Pico::EKey::I))
    {
        Actor->Destroy();
        ReplicationLabActorHandle = {};
        ReplicationLabLastAction = "I: destroyed the authority actor";
    }
}

void PSandboxGameInstance::OnWorldCleanup(Pico::PWorld*)
{
    ReplicationLabActorHandle = {};
    ReplicationLabLastAction = "world cleanup";
}

void PSandboxGameInstance::Shutdown()
{
    ReplicationLabActorHandle = {};
    Pico::PGameInstance::Shutdown();
}

void PSandboxGameInstance::AppendGameplayDebugLines(
    std::vector<std::string>& OutLines) const
{
    const Pico::FGameEngine* GameEngine = GetGameEngine();
    const Pico::ENetMode Mode = GameEngine != nullptr
        ? GameEngine->GetNetDriver().GetNetMode()
        : Pico::ENetMode::Standalone;
    OutLines.emplace_back(std::string("Replication Lab | endpoint: ")
        + Pico::ToString(Mode));
    OutLines.emplace_back("Authority controls: T Spawn | Y Move | U Change State | I Destroy");

    const PSandboxReplicationLabActor* Actor = ResolveReplicationLabActor();
    if (Actor == nullptr)
    {
        OutLines.emplace_back("Lab Actor: <none> | last action: "
            + ReplicationLabLastAction);
        return;
    }

    const Pico::FVector3 Location = Actor->GetActorLocation();
    std::ostringstream Details;
    Details << std::fixed << std::setprecision(1)
        << "Lab Actor: " << Actor->GetName().ToString()
        << " | NetId " << Actor->GetNetObjectId().Value
        << " | location (" << Location.X << ", " << Location.Y << ", " << Location.Z << ")";
    OutLines.push_back(Details.str());
    OutLines.emplace_back("InitialOnly marker: "
        + std::to_string(Actor->GetInitialSpawnMarker())
        + " | revision: " + std::to_string(Actor->GetLabRevision())
        + " | local OnRep calls: " + std::to_string(Actor->GetRepNotifyCount()));
    OutLines.emplace_back("Last action: " + ReplicationLabLastAction);
}

PSandboxReplicationLabActor* PSandboxGameInstance::ResolveReplicationLabActor() const
{
    Pico::PObject* Object = Pico::ResolveObject(ReplicationLabActorHandle);
    return Object != nullptr
            && Object->IsA(PSandboxReplicationLabActor::StaticClass())
            && !static_cast<PSandboxReplicationLabActor*>(Object)->IsPendingDestroy()
        ? static_cast<PSandboxReplicationLabActor*>(Object)
        : nullptr;
}

PSandboxReplicationLabActor* PSandboxGameInstance::FindReplicationLabActor() const
{
    Pico::PWorld* World = GetWorld();
    if (World == nullptr)
    {
        return nullptr;
    }
    for (Pico::PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (Pico::PActor* Actor : Level->GetActors())
        {
            if (Actor != nullptr
                && Actor->IsA(PSandboxReplicationLabActor::StaticClass())
                && !Actor->IsPendingDestroy())
            {
                return static_cast<PSandboxReplicationLabActor*>(Actor);
            }
        }
    }
    return nullptr;
}

bool PSandboxGameInstance::SpawnReplicationLabActor()
{
    Pico::PWorld* World = GetWorld();
    if (World == nullptr || FindReplicationLabActor() != nullptr)
    {
        return false;
    }
    Pico::FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = Pico::FName("ReplicationLabActor");
    SpawnParameters.ObjectFlags = Pico::EObjectFlags::Transient;
    PSandboxReplicationLabActor* Actor =
        World->SpawnActor<PSandboxReplicationLabActor>(SpawnParameters);
    if (Actor == nullptr)
    {
        return false;
    }
    Actor->SetAuthoritySpawnMarker(6202);
    Actor->SetActorLocation({180.0f, 0.0f, 130.0f});
    ReplicationLabActorHandle = Actor->GetHandle();
    return true;
}

}
