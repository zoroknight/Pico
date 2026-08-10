#include "Pico/Engine/GameStateBase.h"

#include "Pico/Engine/PlayerState.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ReferenceCollector.h"

#include <algorithm>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PGameStateBase)

PGameStateBase::PGameStateBase(const FObjectConstructionParams& Params)
    : PActor(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
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

void PGameStateBase::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PActor::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(PlayerStateHandles);
}

void PGameStateBase::BeginDestroy()
{
    PlayerStateHandles.clear();
    PActor::BeginDestroy();
}
}
