#include "Pico/Launch/GameLaunch.h"

#include "Pico/Core/Platform.h"
#include "Pico/Core/CommandLine.h"
#include "Pico/Core/PlatformTextInput.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/Character.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/MatchState.h"
#include "Pico/Engine/MovementComponent.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PawnMovementComponent.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Render/SceneViewportRenderer.h"
#include "Pico/PhysicsCore/WorldCollisionQuery.h"

#include <GLFW/glfw3.h>
#if PICO_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void* GetNativeWindowHandle(GLFWwindow* Window)
{
#if PICO_PLATFORM_WINDOWS
    return Window != nullptr ? glfwGetWin32Window(Window) : nullptr;
#else
    (void)Window;
    return nullptr;
#endif
}

struct FWindowInputContext
{
    FWindowInputContext(Pico::FInputSystem* InInputSystem, void* NativeWindow)
        : InputSystem(InInputSystem)
        , TextInputContext(NativeWindow)
    {
    }

    Pico::FInputSystem* InputSystem = nullptr;
    Pico::FPlatformTextInputContext TextInputContext;
    bool bMouseCaptured = false;
};

void SetMouseCaptured(GLFWwindow* Window, FWindowInputContext& Context, bool bCaptured)
{
    Context.bMouseCaptured = bCaptured;
    glfwSetInputMode(Window, GLFW_CURSOR, bCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(Window, GLFW_RAW_MOUSE_MOTION, bCaptured ? GLFW_TRUE : GLFW_FALSE);
    if (Context.InputSystem != nullptr) Context.InputSystem->SetFocused(false);
    if (Context.InputSystem != nullptr) Context.InputSystem->SetFocused(true);
}

struct FGameplayDebugPanel
{
    void Update(Pico::FGameEngine& GameEngine)
    {
        Pico::PGameInstance* GameInstance = GameEngine.GetGameInstance();
        Pico::PLocalPlayer* LocalPlayer = GameInstance != nullptr
            ? GameInstance->GetPrimaryLocalPlayer()
            : nullptr;
        Pico::PPlayerController* Controller = LocalPlayer != nullptr
            ? LocalPlayer->GetPlayerController()
            : nullptr;
        Pico::PWorld* World = GameEngine.GetEngineLoop().GetWorld();
        Observe("World", World, WorldHandle);
        Observe("GameMode", World != nullptr ? World->GetGameMode() : nullptr, GameModeHandle);
        Observe("GameState", World != nullptr ? World->GetGameState() : nullptr, GameStateHandle);
        Observe("LocalPlayer", LocalPlayer, LocalPlayerHandle);
        Observe("PlayerController", Controller, ControllerHandle);
        Observe("PlayerState", Controller != nullptr ? Controller->GetPlayerState() : nullptr, PlayerStateHandle);
        Pico::PPawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
        Observe("Pawn", Pawn, PawnHandle);
        if (Pawn != nullptr)
        {
            LastPawnHandle = Pawn->GetHandle();
        }

        Pico::PGameStateBase* GameState = World != nullptr
            ? World->GetGameState()
            : nullptr;
        if (GameState != nullptr && GameState->GetMatchState() != LastMatchState)
        {
            Events.emplace_back(std::string("MatchState -> ")
                + std::string(Pico::ToString(GameState->GetMatchState())));
            LastMatchState = GameState->GetMatchState();
        }

        Pico::PPlayerStart* PlayerStart = FindPlayerStart(World);
        if (PlayerStart != nullptr
            && PlayerStart->GetSpawnEventCount() != LastSpawnEventCount)
        {
            Events.emplace_back("PlayerStart broadcast observed (count "
                + std::to_string(PlayerStart->GetSpawnEventCount()) + ")");
            LastSpawnEventCount = PlayerStart->GetSpawnEventCount();
        }
    }

    void Draw(Pico::FGameEngine& GameEngine)
    {
        if (!bVisible)
        {
            return;
        }
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(500.0f, 680.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Gameplay Debug", &bVisible))
        {
            ImGui::End();
            return;
        }

        Pico::PGameInstance* GameInstance = GameEngine.GetGameInstance();
        Pico::PLocalPlayer* LocalPlayer = GameInstance != nullptr
            ? GameInstance->GetPrimaryLocalPlayer()
            : nullptr;
        Pico::PPlayerController* Controller = LocalPlayer != nullptr
            ? LocalPlayer->GetPlayerController()
            : nullptr;
        Pico::PPlayerState* PlayerState = Controller != nullptr
            ? Controller->GetPlayerState()
            : nullptr;
        Pico::PPawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
        Pico::PWorld* World = GameEngine.GetEngineLoop().GetWorld();
        Pico::PGameModeBase* GameMode = World != nullptr
            ? World->GetGameMode()
            : nullptr;
        Pico::PGameStateBase* GameState = World != nullptr
            ? World->GetGameState()
            : nullptr;
        Pico::PPlayerStart* PlayerStart = FindPlayerStart(World);

        ImGui::TextUnformatted("Runtime object chain");
        DrawObject("GameInstance", GameInstance);
        DrawObject("LocalPlayer", LocalPlayer);
        DrawObject("World", World);
        DrawObject("GameMode", GameMode);
        DrawObject("GameState", GameState);
        DrawObject("PlayerController", Controller);
        DrawObject("ViewTarget", Controller != nullptr ? Controller->GetViewTarget() : nullptr);
        DrawObject("PlayerState", PlayerState);
        DrawObject("Controlled Pawn", Pawn);
        if (PlayerState != nullptr)
        {
            ImGui::Text("PlayerId: %d   Score: %.1f   Spectator: %s",
                PlayerState->GetPlayerId(),
                PlayerState->GetScore(),
                PlayerState->IsSpectator() ? "yes" : "no");
        }

        if (GameState != nullptr)
        {
            ImGui::Text("Match: %s   Elapsed: %.2fs",
                Pico::ToString(GameState->GetMatchState()),
                GameState->GetElapsedMatchTime());
        }
        if (PlayerStart != nullptr)
        {
            ImGui::Text("PlayerStart bindings: %zu   broadcasts observed: %d",
                PlayerStart->OnPlayerSpawned().Num(),
                PlayerStart->GetSpawnEventCount());
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Network");
        const Pico::FNetDriver& NetDriver = GameEngine.GetNetDriver();
        const std::string LocalAddress = NetDriver.GetLocalAddress().IsValid()
            ? NetDriver.GetLocalAddress().ToString() : "none";
        ImGui::Text("Mode: %s   Local: %s",
            Pico::ToString(NetDriver.GetNetMode()), LocalAddress.c_str());
        ImGui::Text("Open connections: %zu   Invalid packets: %llu",
            NetDriver.GetOpenConnectionCount(),
            static_cast<unsigned long long>(NetDriver.GetInvalidPacketCount()));
        const Pico::FReplicationStatistics Replication =
            NetDriver.GetReplicationStatistics();
        ImGui::Text("Net objects: %zu   Actor channels: %zu   Unresolved refs: %zu",
            Replication.NetObjectCount,
            Replication.ChannelCount,
            Replication.UnresolvedReferenceCount);
        ImGui::Text("Replication: spawn %llu  delta %llu  destroy %llu  OnRep %llu",
            static_cast<unsigned long long>(Replication.SpawnMessagesSent),
            static_cast<unsigned long long>(Replication.DeltaMessagesSent),
            static_cast<unsigned long long>(Replication.DestroyMessagesSent),
            static_cast<unsigned long long>(Replication.OnRepCalls));
        ImGui::Text("Received: %llu   rejected: %llu",
            static_cast<unsigned long long>(Replication.MessagesReceived),
            static_cast<unsigned long long>(Replication.RejectedMessages));
        ImGui::Text("RPC: sent %llu  received %llu  rejected %llu",
            static_cast<unsigned long long>(Replication.RpcMessagesSent),
            static_cast<unsigned long long>(Replication.RpcMessagesReceived),
            static_cast<unsigned long long>(Replication.RpcMessagesRejected));
        ImGui::Text(
            "Character moves: sent %llu  received %llu  rejected %llu",
            static_cast<unsigned long long>(
                Replication.CharacterMoveMessagesSent),
            static_cast<unsigned long long>(
                Replication.CharacterMoveMessagesReceived),
            static_cast<unsigned long long>(
                Replication.CharacterMoveMessagesRejected));
        ImGui::Text("Movement state: corrections %llu/%llu  snapshots %llu/%llu",
            static_cast<unsigned long long>(
                Replication.CharacterCorrectionsSent),
            static_cast<unsigned long long>(
                Replication.CharacterCorrectionsReceived),
            static_cast<unsigned long long>(
                Replication.CharacterSnapshotsSent),
            static_cast<unsigned long long>(
                Replication.CharacterSnapshotsReceived));
        const Pico::FNetworkSimulationSnapshot Simulation =
            NetDriver.GetNetworkSimulationSnapshot();
        ImGui::Text("Net simulation: outgoing %d ms  +/- %d ms  loss %d%%  queued %zu  dropped %llu",
            Simulation.Settings.LatencyMs,
            Simulation.Settings.JitterMs,
            Simulation.Settings.PacketLossPercent,
            Simulation.DelayedPacketCount,
            static_cast<unsigned long long>(Simulation.DroppedPacketCount));
        if (Pawn != nullptr && Pawn->IsA(Pico::PCharacter::StaticClass()))
        {
            const Pico::PCharacterMovementComponent* Movement =
                static_cast<Pico::PCharacter*>(Pawn)->GetCharacterMovement();
            if (Movement != nullptr)
            {
                const Pico::FCharacterPredictionStatistics Prediction =
                    Movement->GetPredictionStatistics();
                ImGui::Text(
                    "Prediction: %s  policy %s  sent %u  ack %u  pending %zu",
                    Prediction.bPredictionEnabled ? "on" : "off",
                    Prediction.bPolicyHashMatches ? "match" : "mismatch",
                    Prediction.LastSentMove,
                    Prediction.LastAcknowledgedMove,
                    Prediction.PendingMoveCount);
                ImGui::Text(
                    "Corrections: %llu  replays %llu  max error %.2f  snapshots %zu",
                    static_cast<unsigned long long>(Prediction.CorrectionCount),
                    static_cast<unsigned long long>(Prediction.ReplayCount),
                    Prediction.MaxPositionError,
                    Prediction.SnapshotCount);
            }
        }
        const Pico::PCharacterMovementComponent* SimulatedMovement = nullptr;
        if (World != nullptr)
        {
            for (Pico::PLevel* Level : World->GetLevels())
            {
                if (Level == nullptr) continue;
                for (Pico::PActor* Actor : Level->GetActors())
                {
                    if (Actor != nullptr
                        && Actor->IsA(Pico::PCharacter::StaticClass())
                        && Actor->GetLocalRole()
                            == Pico::ENetRole::SimulatedProxy)
                    {
                        SimulatedMovement = static_cast<Pico::PCharacter*>(Actor)
                            ->GetCharacterMovement();
                        break;
                    }
                }
                if (SimulatedMovement != nullptr) break;
            }
        }
        if (SimulatedMovement != nullptr)
        {
            const Pico::FCharacterPredictionStatistics SimulatedStats =
                SimulatedMovement->GetPredictionStatistics();
            ImGui::Text(
                "Remote smoothing: %s  time %.0f ms  offset %.2f  buffer %zu  delay %.1f ticks",
                Pico::ToString(SimulatedMovement->GetNetworkSmoothingMode()),
                SimulatedMovement->GetNetworkSimulatedSmoothLocationTime()
                    * 1000.0f,
                SimulatedMovement->GetNetworkSmoothingVisualOffsetDistance(),
                SimulatedStats.SnapshotCount,
                SimulatedMovement->GetSnapshotInterpolationDelayTicks());
            ImGui::Text(
                "Remote simulation: extrapolation %s  %.0f / %.0f ms  snapshot age %.0f ms  clamps %llu",
                SimulatedMovement->IsSimulatedProxyExtrapolationEnabled()
                    ? "on" : "off",
                SimulatedStats.SimulatedProxyExtrapolationSeconds * 1000.0f,
                SimulatedMovement->GetNetworkMaxSimulatedProxyExtrapolationTime()
                    * 1000.0f,
                SimulatedStats.SimulatedProxySnapshotAgeSeconds * 1000.0f,
                static_cast<unsigned long long>(
                    SimulatedStats.SimulatedProxyExtrapolationClampCount));
        }
        const std::vector<Pico::FNetConnectionSnapshot> Connections =
            NetDriver.GetConnectionSnapshots();
        if (Connections.empty()) ImGui::TextDisabled("No network connections");
        for (std::size_t Index = 0; Index < Connections.size(); ++Index)
        {
            const Pico::FNetConnectionSnapshot& Connection = Connections[Index];
            ImGui::PushID(static_cast<int>(Index));
            ImGui::Text("Connection %u: %s",
                Connection.ConnectionId.Value, Pico::ToString(Connection.State));
            ImGui::Text("Remote: %s   RTT: %.1f ms",
                Connection.RemoteAddress.ToString().c_str(),
                Connection.Statistics.SmoothedRoundTripSeconds * 1000.0);
            ImGui::Text("Packets: sent %llu  received %llu  dropped %llu",
                static_cast<unsigned long long>(Connection.Statistics.PacketsSent),
                static_cast<unsigned long long>(Connection.Statistics.PacketsReceived),
                static_cast<unsigned long long>(Connection.Statistics.PacketsDropped));
            ImGui::Text("Duplicate: %llu  out of order: %llu  reliable queue: %zu",
                static_cast<unsigned long long>(Connection.Statistics.DuplicatePackets),
                static_cast<unsigned long long>(Connection.Statistics.OutOfOrderPackets),
                Connection.PendingReliableMessages);
            ImGui::Text("Messages: reliable sent %llu  unreliable sent %llu / delivered %llu",
                static_cast<unsigned long long>(
                    Connection.Statistics.ReliableMessagesSent),
                static_cast<unsigned long long>(
                    Connection.Statistics.UnreliableMessagesSent),
                static_cast<unsigned long long>(
                    Connection.Statistics.UnreliableMessagesDelivered));
            if (!Connection.CloseReason.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                    "Closed: %s", Connection.CloseReason.c_str());
            }
            ImGui::PopID();
        }
        const std::vector<Pico::FActorChannelSnapshot> Channels =
            NetDriver.GetActorChannelSnapshots();
        for (const Pico::FActorChannelSnapshot& Channel : Channels)
        {
            ImGui::Text("Channel C%u / N%u: state %u  fields %zu  pending %u",
                Channel.ConnectionId.Value,
                Channel.NetObjectId.Value,
                static_cast<unsigned int>(Channel.State),
                Channel.BaselineFieldCount,
                Channel.PendingReliableId);
        }
        if (!NetDriver.GetLastError().empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                "Last error: %s", NetDriver.GetLastError().c_str());
        }

        std::vector<std::string> ProjectDebugLines;
        if (GameInstance != nullptr)
        {
            GameInstance->AppendGameplayDebugLines(ProjectDebugLines);
        }
        if (!ProjectDebugLines.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Project Debug");
            for (const std::string& Line : ProjectDebugLines)
            {
                ImGui::TextWrapped("%s", Line.c_str());
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Movement");
        Pico::PPawnMovementComponent* Movement =
            Pawn != nullptr ? Pawn->GetMovementComponent() : nullptr;
        DrawObject("MovementComponent", Movement);
        DrawObject(
            "UpdatedComponent",
            Movement != nullptr ? Movement->GetUpdatedComponent() : nullptr);
        if (Pawn != nullptr)
        {
            const Pico::FVector3& Pending =
                Pawn->GetPendingMovementInputVector();
            const Pico::FVector3& LastInput =
                Pawn->GetLastMovementInputVector();
            ImGui::Text("Pending input: %.3f  %.3f  %.3f",
                Pending.X, Pending.Y, Pending.Z);
            ImGui::Text("Last input: %.3f  %.3f  %.3f",
                LastInput.X, LastInput.Y, LastInput.Z);
            const Pico::FRotator ActorRotation = Pawn->GetActorRotation();
            const Pico::FRotator ControlRotation = Controller != nullptr
                ? Controller->GetControlRotation() : Pico::FRotator::ZeroRotator;
            ImGui::Text("Actor yaw: %.1f   Control yaw: %.1f   pitch: %.1f",
                ActorRotation.Yaw, ControlRotation.Yaw, ControlRotation.Pitch);
        }
        if (Movement != nullptr)
        {
            const Pico::FVector3& Velocity = Movement->GetVelocity();
            const Pico::FVector3& MoveDelta = Movement->GetLastMoveDelta();
            const Pico::FHitResult& Hit = Movement->GetLastHitResult();
            ImGui::Text("Velocity: %.2f  %.2f  %.2f",
                Velocity.X, Velocity.Y, Velocity.Z);
            ImGui::Text("Last delta: %.3f  %.3f  %.3f",
                MoveDelta.X, MoveDelta.Y, MoveDelta.Z);
            ImGui::Text("Move: %s   Blocking hit: %s   Time: %.3f",
                Movement->GetLastTeleportType() == Pico::ETeleportType::None
                    ? (Movement->WasLastMoveSwept() ? "Sweep" : "Direct")
                    : "Teleport",
                Hit.bBlockingHit ? "yes" : "no",
                Hit.Time);
        }
        Pico::PCharacterMovementComponent* CharacterMovement =
            Movement != nullptr
                && Movement->IsA(
                    Pico::PCharacterMovementComponent::StaticClass())
            ? static_cast<Pico::PCharacterMovementComponent*>(Movement)
            : nullptr;
        Pico::PCharacter* Character = Pawn != nullptr
                && Pawn->IsA(Pico::PCharacter::StaticClass())
            ? static_cast<Pico::PCharacter*>(Pawn)
            : nullptr;
        if (CharacterMovement != nullptr)
        {
            const Pico::FFindFloorResult& Floor =
                CharacterMovement->GetCurrentFloor();
            const Pico::FVector3& FloorNormal = Floor.HitResult.ImpactNormal;
            ImGui::Text("Character mode: %s   On ground: %s",
                Pico::ToString(CharacterMovement->GetMovementMode()),
                CharacterMovement->IsMovingOnGround() ? "yes" : "no");
            ImGui::Text("Floor: %s   Walkable: %s   Distance: %.3f",
                Floor.bBlockingHit ? "hit" : "none",
                Floor.bWalkableFloor ? "yes" : "no",
                Floor.FloorDistance);
            ImGui::Text("Floor normal: %.3f  %.3f  %.3f",
                FloorNormal.X, FloorNormal.Y, FloorNormal.Z);
            ImGui::Text("Jump pressed: %s   Simulation iterations: %d",
                Character != nullptr && Character->IsJumpPressed()
                    ? "yes"
                    : "no",
                CharacterMovement->GetLastSimulationIterations());
            ImGui::Text("Rotation policy: %s   Rate: %.1f deg/s",
                Character != nullptr && Character->UsesControllerRotationYaw()
                    ? "Controller Yaw"
                    : CharacterMovement->ShouldOrientRotationToMovement()
                        ? "Orient to Movement"
                        : CharacterMovement->UsesControllerDesiredRotation()
                            ? "Controller Desired"
                            : "Keep Actor Yaw",
                CharacterMovement->GetRotationRate());
        }

        Pico::PSkeletalMeshComponent* SkeletalMesh = nullptr;
        if (Pawn != nullptr)
        {
            for (Pico::PActorComponent* Component : Pawn->GetComponents())
            {
                if (Component != nullptr
                    && Component->IsA(Pico::PSkeletalMeshComponent::StaticClass()))
                {
                    SkeletalMesh = static_cast<Pico::PSkeletalMeshComponent*>(Component);
                    break;
                }
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Animation");
        if (SkeletalMesh != nullptr)
        {
            const Pico::FVector3 Velocity = Movement != nullptr
                ? Movement->GetVelocity() : Pico::FVector3::ZeroVector;
            const float GroundSpeed = std::sqrt(
                Velocity.X * Velocity.X + Velocity.Y * Velocity.Y);
            Pico::PAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
            const Pico::FAnimationClipData* Clip = AnimInstance != nullptr
                ? AnimInstance->GetCurrentClip() : nullptr;
            ImGui::Text("Ground speed: %.2f   Animation state: %s",
                GroundSpeed,
                Pico::ToString(SkeletalMesh->GetAnimationState()));
            ImGui::Text("Movement mode: %s   Current clip: %s",
                CharacterMovement != nullptr
                    ? Pico::ToString(CharacterMovement->GetMovementMode())
                    : "None",
                Clip != nullptr ? Clip->Name.c_str() : "None");
            ImGui::Text("Playback time: %.3f s",
                AnimInstance != nullptr ? AnimInstance->GetPlaybackTime() : 0.0f);
        }
        else
        {
            ImGui::TextDisabled("No SkeletalMeshComponent on the possessed Pawn");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Physics");
        ImGui::Text("Backend: %s   Steps: %llu",
            World != nullptr && World->GetPhysicsScene() != nullptr ? "Jolt 5.6.0" : "none",
            static_cast<unsigned long long>(World != nullptr ? World->GetPhysicsStepCount() : 0));
        ImGui::Text("Hits: %llu   Begin overlap: %llu   End overlap: %llu",
            static_cast<unsigned long long>(World != nullptr ? World->GetPhysicsHitCount() : 0),
            static_cast<unsigned long long>(World != nullptr ? World->GetPhysicsBeginOverlapCount() : 0),
            static_cast<unsigned long long>(World != nullptr ? World->GetPhysicsEndOverlapCount() : 0));
        Pico::PSceneComponent* PawnRoot = Pawn != nullptr ? Pawn->GetRootComponent() : nullptr;
        Pico::PPrimitiveComponent* PawnPrimitive = PawnRoot != nullptr
                && PawnRoot->IsA(Pico::PPrimitiveComponent::StaticClass())
            ? static_cast<Pico::PPrimitiveComponent*>(PawnRoot)
            : nullptr;
        if (PawnPrimitive != nullptr)
        {
            const Pico::FPhysicsBodyHandle Body = PawnPrimitive->GetPhysicsBodyHandle();
            ImGui::Text("Pawn body: %u:%u   Type: %s",
                Body.Id,
                Body.Serial,
                PawnPrimitive->GetPhysicsBodyType() == Pico::EPhysicsBodyType::Dynamic
                    ? "Dynamic"
                    : PawnPrimitive->GetPhysicsBodyType() == Pico::EPhysicsBodyType::Kinematic
                        ? "Kinematic"
                        : "Static");
        }
        if (ImGui::Button("Raycast Down") && World != nullptr && World->GetCollisionQuery() != nullptr)
        {
            const Pico::FVector3 Start = Pawn != nullptr
                ? Pawn->GetActorLocation() + Pico::FVector3(0.0f, 0.0f, 300.0f)
                : Pico::FVector3(0.0f, 0.0f, 300.0f);
            Pico::FCollisionQueryParams Params;
            if (PawnPrimitive != nullptr) Params.MovingObject = PawnPrimitive->GetHandle();
            bLastRaycastHit = World->GetCollisionQuery()->Raycast(
                Start,
                Start - Pico::FVector3(0.0f, 0.0f, 1000.0f),
                Params,
                LastRaycastHit);
        }
        ImGui::SameLine();
        ImGui::Text("%s  time %.3f  object %u:%u",
            bLastRaycastHit ? "hit" : "no hit",
            LastRaycastHit.Time,
            LastRaycastHit.HitObject.Index,
            LastRaycastHit.HitObject.Serial);

        ImGui::Separator();
        const Pico::EMatchState MatchState = GameMode != nullptr
            ? GameMode->GetMatchState()
            : Pico::EMatchState::Aborted;
        const bool bCanStartMatch = GameMode != nullptr
            && MatchState == Pico::EMatchState::WaitingToStart;
        if (!bCanStartMatch) ImGui::BeginDisabled();
        if (ImGui::Button("Start Match")) GameMode->StartMatch();
        if (!bCanStartMatch) ImGui::EndDisabled();
        ImGui::SameLine();
        const bool bCanEndMatch = GameMode != nullptr
            && MatchState == Pico::EMatchState::InProgress;
        if (!bCanEndMatch) ImGui::BeginDisabled();
        if (ImGui::Button("End Match")) GameMode->EndMatch();
        if (!bCanEndMatch) ImGui::EndDisabled();
        ImGui::SameLine();
        const bool bCanAbortMatch = GameMode != nullptr
            && MatchState != Pico::EMatchState::Aborted
            && MatchState != Pico::EMatchState::LeavingMap;
        if (!bCanAbortMatch) ImGui::BeginDisabled();
        if (ImGui::Button("Abort Match")) GameMode->AbortMatch();
        if (!bCanAbortMatch) ImGui::EndDisabled();

        const bool bCanRestart = World != nullptr
            && World->GetGameMode() != nullptr
            && Controller != nullptr;
        if (!bCanRestart) ImGui::BeginDisabled();
        if (ImGui::Button("Restart Player"))
        {
            World->GetGameMode()->RestartPlayer(Controller);
        }
        ImGui::SameLine();
        if (ImGui::Button("Destroy Pawn") && Pawn != nullptr)
        {
            Pawn->Destroy();
        }
        if (!bCanRestart) ImGui::EndDisabled();

        const bool bCanUnPossess = Controller != nullptr && Pawn != nullptr;
        if (!bCanUnPossess) ImGui::BeginDisabled();
        if (ImGui::Button("UnPossess"))
        {
            Controller->UnPossess();
            if (PlayerState != nullptr) PlayerState->SetIsSpectator(true);
        }
        if (!bCanUnPossess) ImGui::EndDisabled();
        ImGui::SameLine();
        Pico::PObject* LastPawnObject = Pico::ResolveObject(LastPawnHandle);
        Pico::PPawn* AvailablePawn = LastPawnObject != nullptr
                && LastPawnObject->IsA(Pico::PPawn::StaticClass())
                && !static_cast<Pico::PPawn*>(LastPawnObject)->IsPendingDestroy()
            ? static_cast<Pico::PPawn*>(LastPawnObject)
            : nullptr;
        const bool bCanPossess = Controller != nullptr
            && Controller->GetPawn() == nullptr
            && AvailablePawn != nullptr;
        if (!bCanPossess) ImGui::BeginDisabled();
        if (ImGui::Button("Possess Last Pawn"))
        {
            Controller->Possess(AvailablePawn);
            if (PlayerState != nullptr) PlayerState->SetIsSpectator(false);
        }
        if (!bCanPossess) ImGui::EndDisabled();

        if (ImGui::Button("Reload Map"))
        {
            GameEngine.LoadMap(GameEngine.GetDefaultMapPath());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Events"))
        {
            Events.clear();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Lifecycle events");
        ImGui::BeginChild("GameplayEvents", ImVec2(0.0f, 0.0f), true);
        for (const std::string& Event : Events)
        {
            ImGui::TextUnformatted(Event.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    bool bVisible = true;
    bool bLastRaycastHit = false;
    Pico::FHitResult LastRaycastHit;

private:
    static Pico::PPlayerStart* FindPlayerStart(Pico::PWorld* World)
    {
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
                    && Actor->IsA(Pico::PPlayerStart::StaticClass())
                    && !Actor->IsPendingDestroy())
                {
                    return static_cast<Pico::PPlayerStart*>(Actor);
                }
            }
        }
        return nullptr;
    }

    static void DrawObject(const char* Label, Pico::PObject* Object)
    {
        if (Object == nullptr)
        {
            ImGui::TextDisabled("%-18s <none>", Label);
            return;
        }
        const Pico::FObjectHandle Handle = Object->GetHandle();
        ImGui::Text("%-18s %s [%u:%u]",
            Label,
            Object->GetName().ToString().c_str(),
            Handle.Index,
            Handle.Serial);
        if (Object->IsA(Pico::PActor::StaticClass()))
        {
            const auto* Actor = static_cast<const Pico::PActor*>(Object);
            ImGui::SameLine();
            ImGui::TextDisabled("role %s / remote %s  NetId %u",
                Pico::ToString(Actor->GetLocalRole()),
                Pico::ToString(Actor->GetRemoteRole()),
                Actor->GetNetObjectId().Value);
        }
    }

    void Observe(
        const char* Label,
        Pico::PObject* Object,
        Pico::FObjectHandle& Previous)
    {
        const Pico::FObjectHandle Current = Object != nullptr
            ? Object->GetHandle()
            : Pico::FObjectHandle {};
        if (Current == Previous)
        {
            return;
        }
        if (Previous.IsValid())
        {
            Events.emplace_back(std::string(Label) + " released ["
                + std::to_string(Previous.Index) + ":"
                + std::to_string(Previous.Serial) + "]");
        }
        if (Object != nullptr)
        {
            Events.emplace_back(std::string(Label) + " -> "
                + Object->GetName().ToString() + " ["
                + std::to_string(Current.Index) + ":"
                + std::to_string(Current.Serial) + "]");
        }
        Previous = Current;
        if (Events.size() > 128)
        {
            Events.erase(Events.begin(), Events.begin() + 32);
        }
    }

    std::vector<std::string> Events;
    Pico::FObjectHandle WorldHandle;
    Pico::FObjectHandle GameModeHandle;
    Pico::FObjectHandle GameStateHandle;
    Pico::FObjectHandle LocalPlayerHandle;
    Pico::FObjectHandle ControllerHandle;
    Pico::FObjectHandle PlayerStateHandle;
    Pico::FObjectHandle PawnHandle;
    Pico::FObjectHandle LastPawnHandle;
    Pico::EMatchState LastMatchState = Pico::EMatchState::EnteringMap;
    int32_t LastSpawnEventCount = 0;
};

std::filesystem::path FindProjectFile(
    int Argc,
    char** Argv,
    const std::filesystem::path& DefaultProjectFile)
{
    constexpr std::string_view ProjectPrefix = "-project=";
    constexpr std::string_view StageRootPrefix = "-stageroot=";
    bool bHasExplicitStageRoot = false;
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string_view Argument = Argv[Index];
        if (Argument.starts_with(ProjectPrefix))
        {
            return std::filesystem::path(
                std::string(Argument.substr(ProjectPrefix.size())));
        }
        bHasExplicitStageRoot |= Argument.starts_with(StageRootPrefix);
        const std::filesystem::path PositionalPath { std::string(Argument) };
        if (!Argument.starts_with("-") && PositionalPath.extension() == ".pico")
        {
            return PositionalPath;
        }
    }

    if (bHasExplicitStageRoot)
    {
        return {};
    }

    std::error_code ErrorCode;
    std::filesystem::path Directory = Argc > 0
        ? std::filesystem::absolute(Argv[0], ErrorCode).parent_path()
        : std::filesystem::path {};
    while (!Directory.empty())
    {
        if (std::filesystem::is_regular_file(
                Directory / "PicoStage.manifest", ErrorCode))
        {
            return {};
        }
        const std::filesystem::path Parent = Directory.parent_path();
        if (Parent == Directory)
        {
            break;
        }
        Directory = Parent;
    }
    return DefaultProjectFile;
}

Pico::FOpenGLProcedure LoadOpenGLProcedure(const char* Name)
{
    return reinterpret_cast<Pico::FOpenGLProcedure>(glfwGetProcAddress(Name));
}

Pico::EKey TranslateKey(int Key)
{
    if (Key >= GLFW_KEY_A && Key <= GLFW_KEY_Z)
    {
        return static_cast<Pico::EKey>(
            static_cast<int>(Pico::EKey::A) + (Key - GLFW_KEY_A));
    }
    switch (Key)
    {
    case GLFW_KEY_SPACE: return Pico::EKey::Space;
    case GLFW_KEY_ESCAPE: return Pico::EKey::Escape;
    case GLFW_KEY_ENTER: return Pico::EKey::Enter;
    case GLFW_KEY_TAB: return Pico::EKey::Tab;
    case GLFW_KEY_LEFT_SHIFT: return Pico::EKey::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT: return Pico::EKey::RightShift;
    case GLFW_KEY_LEFT_CONTROL: return Pico::EKey::LeftControl;
    case GLFW_KEY_RIGHT_CONTROL: return Pico::EKey::RightControl;
    case GLFW_KEY_UP: return Pico::EKey::Up;
    case GLFW_KEY_DOWN: return Pico::EKey::Down;
    case GLFW_KEY_LEFT: return Pico::EKey::Left;
    case GLFW_KEY_RIGHT: return Pico::EKey::Right;
    default: return Pico::EKey::Unknown;
    }
}

Pico::EKey TranslateMouseButton(int Button)
{
    switch (Button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: return Pico::EKey::MouseLeft;
    case GLFW_MOUSE_BUTTON_RIGHT: return Pico::EKey::MouseRight;
    case GLFW_MOUSE_BUTTON_MIDDLE: return Pico::EKey::MouseMiddle;
    default: return Pico::EKey::Unknown;
    }
}

FWindowInputContext* GetInputContext(GLFWwindow* Window)
{
    return static_cast<FWindowInputContext*>(glfwGetWindowUserPointer(Window));
}

void OnKey(GLFWwindow* Window, int Key, int, int Action, int)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context == nullptr || Context->InputSystem == nullptr || Action == GLFW_REPEAT)
    {
        return;
    }
    Context->InputSystem->SetKeyState(TranslateKey(Key), Action == GLFW_PRESS);
    if (Key == GLFW_KEY_ESCAPE && Action == GLFW_PRESS)
    {
        if (Context->bMouseCaptured) SetMouseCaptured(Window, *Context, false);
        else glfwSetWindowShouldClose(Window, GLFW_TRUE);
    }
}

void OnMouseButton(GLFWwindow* Window, int Button, int Action, int)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context != nullptr && Context->InputSystem != nullptr)
    {
        Context->InputSystem->SetKeyState(
            TranslateMouseButton(Button), Action == GLFW_PRESS);
        if (Button == GLFW_MOUSE_BUTTON_RIGHT && Action == GLFW_PRESS)
            SetMouseCaptured(Window, *Context, true);
    }
}

void OnCursorPosition(GLFWwindow* Window, double X, double Y)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context != nullptr && Context->InputSystem != nullptr)
    {
        Context->InputSystem->SetMousePosition(X, Y);
    }
}

void OnScroll(GLFWwindow* Window, double, double YOffset)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context != nullptr && Context->InputSystem != nullptr)
    {
        Context->InputSystem->AddMouseWheelDelta(YOffset);
    }
}

void OnFocus(GLFWwindow* Window, int Focused)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context == nullptr)
    {
        return;
    }
    if (Context->InputSystem != nullptr)
    {
        Context->InputSystem->SetFocused(Focused == GLFW_TRUE);
    }
    Context->TextInputContext.SetTextInputEnabled(Focused != GLFW_TRUE);
}
}

namespace Pico
{
int RunPicoGame(
    int Argc,
    char** Argv,
    IGameModule* GameModule,
    const std::filesystem::path& DefaultProjectFile)
{
    FCommandLine::Init(Argc, Argv);
    const int WindowWidth = std::clamp(
        FCommandLine::GetInt("windowwidth").value_or(1280), 320, 3840);
    const int WindowHeight = std::clamp(
        FCommandLine::GetInt("windowheight").value_or(720), 240, 2160);
    const std::string InstanceLabel =
        FCommandLine::GetValue("instance").value_or("");
    const std::string WindowTitle = InstanceLabel.empty()
        ? "Pico Game" : "Pico Game - " + InstanceLabel;
    if (!glfwInit())
    {
        std::fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* Window = glfwCreateWindow(
        WindowWidth, WindowHeight, WindowTitle.c_str(), nullptr, nullptr);
    if (Window == nullptr)
    {
        glfwTerminate();
        return 1;
    }
    const int WindowX = FCommandLine::GetInt("windowx").value_or(-1);
    const int WindowY = FCommandLine::GetInt("windowy").value_or(-1);
    if (WindowX >= 0 && WindowY >= 0) glfwSetWindowPos(Window, WindowX, WindowY);
    glfwMakeContextCurrent(Window);
    glfwSwapInterval(1);

    FGameEngine GameEngine(GameModule);
    FSceneViewportRenderer Renderer;
    bool bRendererInitialized = false;
    bool bImGuiContextCreated = false;
    bool bImGuiGlfwInitialized = false;
    bool bImGuiOpenGLInitialized = false;
    int ExitCode = 1;

    try
    {
        bRendererInitialized = Renderer.Initialize(&LoadOpenGLProcedure);
        if (!bRendererInitialized)
        {
            throw std::runtime_error("scene renderer initialization failed");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        bImGuiContextCreated = true;
        ImGuiIO& ImGuiIO = ImGui::GetIO();
        const std::filesystem::path InterfaceFont = "C:/Windows/Fonts/segoeui.ttf";
        if (std::filesystem::is_regular_file(InterfaceFont))
        {
            ImGuiIO.FontDefault = ImGuiIO.Fonts->AddFontFromFileTTF(
                InterfaceFont.string().c_str(), 20.0f);
        }
        if (ImGuiIO.FontDefault == nullptr)
        {
            ImFontConfig FontConfig;
            FontConfig.SizePixels = 20.0f;
            ImGuiIO.FontDefault = ImGuiIO.Fonts->AddFontDefault(&FontConfig);
        }
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 3.0f;
        ImGui::GetStyle().FrameRounding = 2.0f;

        const std::filesystem::path ProjectFile =
            FindProjectFile(Argc, Argv, DefaultProjectFile);
        ExitCode = GameEngine.PreInit(Argc, Argv, ProjectFile);
        if (ExitCode == 0)
        {
            ExitCode = GameEngine.Init();
        }

        FWindowInputContext InputContext(
            &GameEngine.GetInputSystem(), GetNativeWindowHandle(Window));
        InputContext.TextInputContext.SetTextInputEnabled(false);
        glfwSetWindowUserPointer(Window, &InputContext);
        glfwSetKeyCallback(Window, &OnKey);
        glfwSetMouseButtonCallback(Window, &OnMouseButton);
        glfwSetCursorPosCallback(Window, &OnCursorPosition);
        glfwSetScrollCallback(Window, &OnScroll);
        glfwSetWindowFocusCallback(Window, &OnFocus);

        bImGuiGlfwInitialized = ImGui_ImplGlfw_InitForOpenGL(Window, true);
        bImGuiOpenGLInitialized = ImGui_ImplOpenGL3_Init("#version 130");
        if (!bImGuiGlfwInitialized || !bImGuiOpenGLInitialized)
        {
            throw std::runtime_error("game debug UI initialization failed");
        }

        FGameplayDebugPanel GameplayDebug;
        bool bF1WasDown = false;

        while (ExitCode == 0
            && !glfwWindowShouldClose(Window)
            && !GameEngine.ShouldExit())
        {
            FInputSystem& Input = GameEngine.GetInputSystem();
            Input.BeginFrame();
            glfwPollEvents();
            const bool bF1Down = glfwGetKey(Window, GLFW_KEY_F1) == GLFW_PRESS;
            if (bF1Down && !bF1WasDown)
            {
                GameplayDebug.bVisible = !GameplayDebug.bVisible;
            }
            bF1WasDown = bF1Down;
            GameEngine.Tick();
            GameplayDebug.Update(GameEngine);

            int Width = 0;
            int Height = 0;
            glfwGetFramebufferSize(Window, &Width, &Height);
            if (Width > 0 && Height > 0)
            {
                Renderer.Resize(
                    static_cast<uint32>(Width),
                    static_cast<uint32>(Height));
                FSceneView View;
                PGameInstance* Instance = GameEngine.GetGameInstance();
                PLocalPlayer* LocalPlayer = Instance != nullptr
                    ? Instance->GetPrimaryLocalPlayer() : nullptr;
                PPlayerController* Controller = LocalPlayer != nullptr
                    ? LocalPlayer->GetPlayerController() : nullptr;
                PActor* ViewTarget = Controller != nullptr ? Controller->GetViewTarget() : nullptr;
                if (!TryBuildActorCameraView(ViewTarget, View, true))
                    TryBuildActiveCameraView(
                        GameEngine.GetEngineLoop().GetWorld(), View, true);
                FSceneViewportRenderOptions RenderOptions;
                RenderOptions.bDrawGrid = false;
                RenderOptions.bDrawComponentVisualizations = false;
                Renderer.Render(
                    GameEngine.GetEngineLoop().GetWorld(),
                    GameEngine.GetEngineLoop().GetAssetRegistry(),
                    GameEngine.GetEngineLoop().GetAssetManager(),
                    View,
                    {},
                    RenderOptions);
                Renderer.PresentToBackBuffer(
                    static_cast<uint32>(Width),
                    static_cast<uint32>(Height));

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                GameplayDebug.Draw(GameEngine);
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(Window);
            }
            Input.EndFrame();
        }
    }
    catch (const std::exception& Exception)
    {
        std::fprintf(stderr, "Pico Game fatal error: %s\n", Exception.what());
        ExitCode = 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "Pico Game fatal error: unknown exception\n");
        ExitCode = 1;
    }

    glfwSetWindowUserPointer(Window, nullptr);
    GameEngine.Exit();
    if (bRendererInitialized)
    {
        Renderer.Shutdown();
    }
    if (bImGuiOpenGLInitialized) ImGui_ImplOpenGL3_Shutdown();
    if (bImGuiGlfwInitialized) ImGui_ImplGlfw_Shutdown();
    if (bImGuiContextCreated) ImGui::DestroyContext();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return ExitCode;
}
}
