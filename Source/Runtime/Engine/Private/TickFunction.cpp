#include "Pico/Engine/TickFunction.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/TickTaskManager.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
void FTickFunction::SetCanEverTick(bool bValue)
{
    bCanEverTick = bValue;
    if (!bCanEverTick)
    {
        bTickEnabled = false;
    }
    else if (!IsRegistered())
    {
        bTickEnabled = bStartWithTickEnabled;
    }
}

bool FTickFunction::CanEverTick() const { return bCanEverTick; }

void FTickFunction::SetTickEnabled(bool bValue)
{
    bTickEnabled = bCanEverTick && bValue;
}

bool FTickFunction::IsTickEnabled() const { return bCanEverTick && bTickEnabled; }

void FTickFunction::SetStartWithTickEnabled(bool bValue)
{
    bStartWithTickEnabled = bValue;
    if (!IsRegistered())
    {
        bTickEnabled = bCanEverTick && bStartWithTickEnabled;
    }
}

bool FTickFunction::ShouldStartWithTickEnabled() const
{
    return bStartWithTickEnabled;
}

void FTickFunction::SetTickGroup(ETickGroup Group)
{
    if (TickGroup == Group) return;
    const ETickGroup OldGroup = TickGroup;
    TickGroup = Group;
    if (Manager != nullptr)
    {
        Manager->NotifyTickGroupChanged(RegistrationId, OldGroup, Group);
    }
}
ETickGroup FTickFunction::GetTickGroup() const { return TickGroup; }

void FTickFunction::SetTickInterval(float Seconds)
{
    TickInterval = std::isfinite(Seconds) && Seconds > 0.0f ? Seconds : 0.0f;
    AccumulatedSeconds = 0.0f;
}

float FTickFunction::GetTickInterval() const { return TickInterval; }
bool FTickFunction::IsRegistered() const { return Manager != nullptr && RegistrationId != 0; }
uint64 FTickFunction::GetRegistrationId() const { return RegistrationId; }

bool FTickFunction::AddPrerequisite(FTickFunction& Prerequisite)
{
    if (&Prerequisite == this
        || !IsRegistered()
        || !Prerequisite.IsRegistered()
        || Manager != Prerequisite.Manager)
    {
        return false;
    }
    if (std::find(PrerequisiteIds.begin(), PrerequisiteIds.end(), Prerequisite.RegistrationId)
        == PrerequisiteIds.end())
    {
        PrerequisiteIds.push_back(Prerequisite.RegistrationId);
        Manager->NotifyPrerequisitesChanged(TickGroup);
    }
    return true;
}

bool FTickFunction::RemovePrerequisite(const FTickFunction& Prerequisite)
{
    const bool bRemoved = std::erase(PrerequisiteIds, Prerequisite.RegistrationId) > 0;
    if (bRemoved && Manager != nullptr)
    {
        Manager->NotifyPrerequisitesChanged(TickGroup);
    }
    return bRemoved;
}

bool FTickFunction::HasPrerequisite(const FTickFunction& Prerequisite) const
{
    return Prerequisite.RegistrationId != 0
        && std::find(PrerequisiteIds.begin(), PrerequisiteIds.end(),
            Prerequisite.RegistrationId) != PrerequisiteIds.end();
}

void FTickFunction::ClearPrerequisites()
{
    if (PrerequisiteIds.empty()) return;
    PrerequisiteIds.clear();
    if (Manager != nullptr)
    {
        Manager->NotifyPrerequisitesChanged(TickGroup);
    }
}
FObjectHandle FTickFunction::GetOwnerHandle() const { return OwnerHandle; }

void FActorTickFunction::ExecuteTick(float DeltaSeconds)
{
    PObject* Object = ResolveObject(GetOwnerHandle());
    if (Object != nullptr && Object->IsA(PActor::StaticClass()))
    {
        static_cast<PActor*>(Object)->DispatchTick(DeltaSeconds);
    }
}

void FActorComponentTickFunction::ExecuteTick(float DeltaSeconds)
{
    PObject* Object = ResolveObject(GetOwnerHandle());
    if (Object != nullptr && Object->IsA(PActorComponent::StaticClass()))
    {
        static_cast<PActorComponent*>(Object)->DispatchTickComponent(DeltaSeconds);
    }
}
}
