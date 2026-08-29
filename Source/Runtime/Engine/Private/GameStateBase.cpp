#include "Pico/Engine/GameStateBase.h"

#include "Pico/Engine/PlayerState.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ReferenceCollector.h"

#include <algorithm>

namespace Pico
{
PICO_DEFINE_CLASS(PGameStateBase)

bool PGameStateBase::RegisterProperties(PClass& Class)
{
    FPropertyMetadata MatchStateMetadata;
    MatchStateMetadata.Flags = EPropertyFlags::Transient
        | EPropertyFlags::Replicated
        | EPropertyFlags::ReadOnly;
    MatchStateMetadata.DisplayName = "Match State";
    MatchStateMetadata.EnumOptions = {
        {static_cast<int32>(EMatchState::EnteringMap), "Entering Map"},
        {static_cast<int32>(EMatchState::WaitingToStart), "Waiting To Start"},
        {static_cast<int32>(EMatchState::InProgress), "In Progress"},
        {static_cast<int32>(EMatchState::WaitingPostMatch), "Waiting Post Match"},
        {static_cast<int32>(EMatchState::LeavingMap), "Leaving Map"},
        {static_cast<int32>(EMatchState::Aborted), "Aborted"}
    };
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(
        Properties, MatchStateValue, MatchStateMetadata);
    FPropertyMetadata ElapsedTimeMetadata;
    ElapsedTimeMetadata.Flags = MatchStateMetadata.Flags;
    ElapsedTimeMetadata.DisplayName = "Elapsed Match Time";
    PICO_ADD_PROPERTY_METADATA(
        Properties, ElapsedMatchTime, ElapsedTimeMetadata);
    return Class.AddProperties(std::move(Properties));
}

PGameStateBase::PGameStateBase(const FObjectConstructionParams& Params)
    : PActor(Params)
{
    SetReplicates(true);
    PrimaryActorTick.SetCanEverTick(true);
    PrimaryActorTick.SetTickGroup(ETickGroup::PostUpdateWork);
}

bool PGameStateBase::AddPlayerState(PPlayerState* PlayerState)
{
    if (PlayerState == nullptr || PlayerState->GetWorld() != GetWorld())
    {
        return false;
    }
    const FObjectHandle Handle = PlayerState->GetHandle();
    if (std::find(PlayerStateHandles.begin(), PlayerStateHandles.end(), Handle)
        == PlayerStateHandles.end())
    {
        PlayerStateHandles.push_back(Handle);
    }
    return true;
}

bool PGameStateBase::RemovePlayerState(PPlayerState* PlayerState)
{
    return PlayerState != nullptr
        && std::erase(PlayerStateHandles, PlayerState->GetHandle()) > 0;
}

std::vector<PPlayerState*> PGameStateBase::GetPlayerStates() const
{
    std::vector<PPlayerState*> Result;
    for (FObjectHandle Handle : PlayerStateHandles)
    {
        PObject* Object = ResolveObject(Handle);
        if (Object != nullptr && Object->IsA(PPlayerState::StaticClass()))
        {
            Result.push_back(static_cast<PPlayerState*>(Object));
        }
    }
    return Result;
}

EMatchState PGameStateBase::GetMatchState() const
{
    return static_cast<EMatchState>(MatchStateValue);
}

bool PGameStateBase::IsMatchInProgress() const
{
    return GetMatchState() == EMatchState::InProgress;
}

float PGameStateBase::GetElapsedMatchTime() const
{
    return ElapsedMatchTime;
}

FOnGameStateMatchStateChanged& PGameStateBase::OnMatchStateChanged()
{
    return MatchStateChangedEvent;
}

void PGameStateBase::Tick(float DeltaSeconds)
{
    PActor::Tick(DeltaSeconds);
    if (IsMatchInProgress())
    {
        ElapsedMatchTime += DeltaSeconds;
        MarkReplicatedPropertyDirty(FName("ElapsedMatchTime"));
    }
}

bool PGameStateBase::SetMatchState(EMatchState NewState)
{
    const EMatchState OldState = GetMatchState();
    if (OldState == NewState)
    {
        return false;
    }
    MatchStateValue = static_cast<int32>(NewState);
    MarkReplicatedPropertyDirty(FName("MatchStateValue"));
    if (NewState == EMatchState::InProgress)
    {
        ElapsedMatchTime = 0.0f;
        MarkReplicatedPropertyDirty(FName("ElapsedMatchTime"));
    }
    MatchStateChangedEvent.Broadcast(OldState, NewState);
    return true;
}

void PGameStateBase::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PActor::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(PlayerStateHandles);
}

void PGameStateBase::BeginDestroy()
{
    MatchStateChangedEvent.Clear();
    PlayerStateHandles.clear();
    PActor::BeginDestroy();
}
}
