#include "PicoSandbox/SandboxGameInstance.h"

#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/ScriptComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/ObjectGlobals.h"
#include "PicoSandbox/SandboxReplicationLabActor.h"
#include "PicoSandbox/SandboxGameplayAbilities.h"
#include "PicoSandbox/SandboxPlayerController.h"
#include "PicoSandbox/SandboxPawn.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace PicoSandbox
{
namespace
{
std::vector<const PSandboxPawn*> CollectOrderedSandboxPawns(
    Pico::PWorld* World,
    const Pico::PPawn* LocalPawn)
{
    std::vector<const PSandboxPawn*> Pawns;
    if (World == nullptr) return Pawns;
    for (Pico::PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (Pico::PActor* Actor : Level->GetActors())
        {
            if (Actor != nullptr && !Actor->IsPendingDestroy()
                && Actor->IsA(PSandboxPawn::StaticClass()))
                Pawns.push_back(static_cast<const PSandboxPawn*>(Actor));
        }
    }
    std::stable_sort(Pawns.begin(), Pawns.end(), [LocalPawn](
        const PSandboxPawn* Left, const PSandboxPawn* Right)
    {
        if (Left == LocalPawn) return Right != LocalPawn;
        if (Right == LocalPawn) return false;
        const Pico::FNetObjectId LeftId = Left->GetNetObjectId();
        const Pico::FNetObjectId RightId = Right->GetNetObjectId();
        if (LeftId.IsValid() != RightId.IsValid()) return LeftId.IsValid();
        if (LeftId.IsValid() && LeftId != RightId) return LeftId.Value < RightId.Value;
        return Left->GetPathName() < Right->GetPathName();
    });
    return Pawns;
}
}

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

void PSandboxGameInstance::OnWorldInitialized(Pico::PWorld* World)
{
    ReplicationLabActorHandle = {};
    ReplicationLabMoveStep = 0;
    const Pico::FGameEngine* GameEngine = GetGameEngine();
    if (GameEngine != nullptr
        && GameEngine->GetNetDriver().GetNetMode() == Pico::ENetMode::Server)
        RemoveUnpossessedServerPreviewPawns(World);
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
    OutLines.emplace_back(
        "Authority: T Spawn | Y Move | U Change State | I Destroy | Client: F Use Door");
    OutLines.emplace_back(
        "Movement networking: SavedMove prediction + authority replay + simulated proxy interpolation; the separate server has no local input.");
    OutLines.emplace_back(
        "Mini GAS: 1 Gravity (purple) | 2 Burn (red) | 3 Freeze (blue)");

    const Pico::PLocalPlayer* LocalPlayer = GetPrimaryLocalPlayer();
    const auto* Controller = LocalPlayer != nullptr
            && LocalPlayer->GetPlayerController() != nullptr
            && LocalPlayer->GetPlayerController()->IsA(
                PSandboxPlayerController::StaticClass())
        ? static_cast<const PSandboxPlayerController*>(
            LocalPlayer->GetPlayerController()) : nullptr;
    if (Controller != nullptr)
    {
        OutLines.emplace_back(std::string("Local controller role: ")
            + Pico::ToString(Controller->GetLocalRole())
            + " | Client RPC replies: "
            + std::to_string(Controller->GetClientInteractionResultCount())
            + " | last accepted: "
            + (Controller->WasLastInteractionAccepted() ? "yes" : "no"));
        const auto* Pawn = Controller->GetPawn() != nullptr
                && Controller->GetPawn()->IsA(PSandboxPawn::StaticClass())
            ? static_cast<const PSandboxPawn*>(Controller->GetPawn()) : nullptr;
        if (Pawn != nullptr)
        {
            std::ostringstream ControlIdentity;
            ControlIdentity << "Pawn class: " << Pawn->GetClass()->GetName().ToString()
                << " | control profile: "
                << Pawn->GetThirdPersonControlProfileAsset().ToString();
            OutLines.push_back(ControlIdentity.str());
            std::ostringstream Policy;
            Policy << "Control policy: "
                << Pico::ToString(Pawn->GetMovementReference())
                << " | hash: 0x" << std::hex << std::uppercase
                << Pawn->GetActiveControlProfileHash()
                << " | loaded: " << (Pawn->HasLoadedControlProfile() ? "yes" : "no");
            OutLines.push_back(Policy.str());
            std::ostringstream Gameplay;
            Gameplay << std::fixed << std::setprecision(1)
                << "GAS state: Health " << Pawn->GetReplicatedHealth()
                << " | Mana " << Pawn->GetReplicatedMana()
                << " | Burn " << Pawn->GetReplicatedBurnRemaining() << "s"
                << " | Gravity " << Pawn->GetReplicatedGravityRemaining() << "s"
                << " | Freeze " << Pawn->GetReplicatedFreezeRemaining() << "s"
                << " | Loadout bits " << Pawn->GetAbilityLoadoutBits();
            OutLines.push_back(Gameplay.str());
            const char* AbilityName = Controller->GetLastGameplayAbilityId() == 1
                ? "Gravity" : Controller->GetLastGameplayAbilityId() == 2
                    ? "Burn" : Controller->GetLastGameplayAbilityId() == 3
                        ? "Freeze" : "none";
            OutLines.emplace_back(std::string("GAS RPC results: ")
                + std::to_string(Controller->GetGameplayAbilityResultCount())
                + " | last " + AbilityName + " | accepted "
                + (Controller->WasLastGameplayAbilityAccepted() ? "yes" : "no"));
        }
    }

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
    OutLines.emplace_back(std::string("Door: ")
        + (Actor->IsDoorOpen() ? "open" : "closed")
        + " | authority uses: " + std::to_string(Actor->GetDoorUseCount())
        + " | multicast pulses: "
        + std::to_string(Actor->GetMulticastPulseCount()));
    OutLines.emplace_back("Last action: " + ReplicationLabLastAction);
}

void PSandboxGameInstance::AppendGameplayStatusLines(
    std::vector<std::string>& OutLines) const
{
    Pico::PWorld* World = GetWorld();
    if (World == nullptr) return;
    const Pico::PLocalPlayer* LocalPlayer = GetPrimaryLocalPlayer();
    const Pico::PPawn* LocalPawn = LocalPlayer != nullptr
            && LocalPlayer->GetPlayerController() != nullptr
        ? LocalPlayer->GetPlayerController()->GetPawn() : nullptr;
    const std::vector<const PSandboxPawn*> Pawns =
        CollectOrderedSandboxPawns(World, LocalPawn);
    Pico::int32 PawnIndex = 0;
    for (const PSandboxPawn* Pawn : Pawns)
    {
        ++PawnIndex;
        OutLines.emplace_back("---");
        std::ostringstream Attributes;
        Attributes << std::fixed << std::setprecision(0)
            << "P" << PawnIndex << (Pawn == LocalPawn ? " [LOCAL]" : "")
            << "  HP " << Pawn->GetReplicatedHealth()
            << "  Mana " << Pawn->GetReplicatedMana();
        OutLines.push_back(Attributes.str());

        std::ostringstream Status;
        Status << std::fixed << std::setprecision(1) << "Status  ";
        bool bHasStatus = false;
        const auto AppendStatus = [&Status, &bHasStatus](
            const char* Name, float Remaining)
        {
            if (Remaining <= 0.05f) return;
            if (bHasStatus) Status << " | ";
            Status << Name << " " << Remaining << "s";
            bHasStatus = true;
        };
        AppendStatus("Burning", Pawn->GetReplicatedBurnRemaining());
        AppendStatus("Gravity", Pawn->GetReplicatedGravityRemaining());
        AppendStatus("Frozen", Pawn->GetReplicatedFreezeRemaining());
        if (!bHasStatus) Status << "Normal";
        OutLines.push_back(Status.str());

        for (Pico::PActorComponent* Component : Pawn->GetComponents())
        {
            if (Component == nullptr
                || !Component->IsA(Pico::PScriptComponent::StaticClass()))
                continue;
            const auto* Script = static_cast<const Pico::PScriptComponent*>(Component);
            for (const Pico::FScriptScreenMessage& Message : Script->GetScreenMessages())
            {
                OutLines.emplace_back("Message  " + Message.Text);
            }
        }
    }
    if (PawnIndex == 0) OutLines.emplace_back("Waiting for replicated players...");
}

void PSandboxGameInstance::AppendGameplayAbilityStatus(
    std::vector<Pico::FPlayerAbilityStatus>& OutPlayers) const
{
    Pico::PWorld* World = GetWorld();
    if (World == nullptr) return;
    const Pico::PLocalPlayer* LocalPlayer = GetPrimaryLocalPlayer();
    const Pico::PPawn* LocalPawn = LocalPlayer != nullptr
            && LocalPlayer->GetPlayerController() != nullptr
        ? LocalPlayer->GetPlayerController()->GetPawn() : nullptr;
    const std::vector<const PSandboxPawn*> Pawns =
        CollectOrderedSandboxPawns(World, LocalPawn);
    Pico::int32 PawnIndex = 0;
    for (const PSandboxPawn* Pawn : Pawns)
    {
        Pico::FPlayerAbilityStatus Player;
        Player.PlayerLabel = "P" + std::to_string(++PawnIndex);
        Player.bIsLocalPlayer = Pawn == LocalPawn;
        const Pico::int32 Loadout = Pawn->GetAbilityLoadoutBits();
        const auto* AbilitySystem = Pawn->GetAbilitySystemComponent();
        const auto AppendAbility = [&Player, AbilitySystem](
                Pico::int32 Bit,
                Pico::int32 LoadoutBits,
                const char* Input,
                const char* Name,
                const Pico::PClass* AbilityClass,
                float Cooldown)
            {
                if ((LoadoutBits & Bit) == 0) return;
                Pico::FGameplayAbilityStatus Status;
                Status.InputLabel = Input;
                Status.AbilityName = Name;
                Status.CooldownRemaining = std::max(0.0f, Cooldown);
                if (AbilitySystem != nullptr)
                {
                    for (const Pico::FGameplayAbilitySpec& Spec
                        : AbilitySystem->GetActivatableAbilities())
                    {
                        if (Spec.AbilityClass == AbilityClass)
                        {
                            Status.bActive = Spec.IsActive();
                            break;
                        }
                    }
                }
                Player.Abilities.push_back(std::move(Status));
        };
        AppendAbility(1, Loadout, "1", "Gravity",
            PSandboxDashAbility::StaticClass(),
            Pawn->GetReplicatedGravityCooldownRemaining());
        AppendAbility(2, Loadout, "2", "Burn",
            PSandboxFireballAbility::StaticClass(),
            Pawn->GetReplicatedBurnCooldownRemaining());
        AppendAbility(4, Loadout, "3", "Freeze",
            PSandboxStunAbility::StaticClass(),
            Pawn->GetReplicatedFreezeCooldownRemaining());
        OutPlayers.push_back(std::move(Player));
    }
}

void PSandboxGameInstance::RemoveUnpossessedServerPreviewPawns(Pico::PWorld* World)
{
    if (World == nullptr) return;
    std::vector<PSandboxPawn*> PreviewPawns;
    for (Pico::PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (Pico::PActor* Actor : Level->GetActors())
        {
            auto* Pawn = Actor != nullptr && Actor->IsA(PSandboxPawn::StaticClass())
                ? static_cast<PSandboxPawn*>(Actor) : nullptr;
            if (Pawn != nullptr && !Pawn->IsPendingDestroy()
                && Pawn->GetController() == nullptr
                && Pawn->GetAutoPossessPlayerIndex() >= 0)
                PreviewPawns.push_back(Pawn);
        }
    }
    for (PSandboxPawn* Pawn : PreviewPawns)
        World->DestroyActor(Pawn);
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
