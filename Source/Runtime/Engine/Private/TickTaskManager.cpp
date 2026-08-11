#include "Pico/Engine/TickTaskManager.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Core/Log.h"
#include "Pico/Engine/TickFunction.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Pico
{
bool FTickTaskManager::RegisterTickFunction(FTickFunction& TickFunction, PObject* Owner)
{
    if (!CheckGameThread("FTickTaskManager::RegisterTickFunction")
        || Owner == nullptr
        || ResolveObject(Owner->GetHandle()) != Owner
        || !TickFunction.CanEverTick())
    {
        return false;
    }
    if (TickFunction.IsRegistered())
    {
        return TickFunction.Manager == this;
    }
    const uint64 Id = NextRegistrationId++;
    if (Id == 0)
    {
        return false;
    }
    TickFunction.OwnerHandle = Owner->GetHandle();
    TickFunction.Manager = this;
    TickFunction.RegistrationId = Id;
    TickFunction.AccumulatedSeconds = 0.0f;
    RegisteredTicks.push_back({Id, &TickFunction});
    return true;
}

void FTickTaskManager::UnregisterTickFunction(FTickFunction& TickFunction)
{
    if (!CheckGameThread("FTickTaskManager::UnregisterTickFunction")
        || TickFunction.Manager != this)
    {
        return;
    }
    const uint64 RemovedId = TickFunction.RegistrationId;
    std::erase_if(RegisteredTicks,
        [RemovedId](const FRegisteredTick& Entry) { return Entry.Id == RemovedId; });
    for (FRegisteredTick& Entry : RegisteredTicks)
    {
        if (Entry.Function != nullptr)
        {
            std::erase(Entry.Function->PrerequisiteIds, RemovedId);
        }
    }
    TickFunction.Manager = nullptr;
    TickFunction.OwnerHandle = {};
    TickFunction.RegistrationId = 0;
    TickFunction.AccumulatedSeconds = 0.0f;
    TickFunction.PrerequisiteIds.clear();
}

void FTickTaskManager::Tick(float DeltaSeconds)
{
    if (!BeginFrame(DeltaSeconds))
    {
        return;
    }
    try
    {
        for (int Group = static_cast<int>(ETickGroup::PrePhysics);
             Group <= static_cast<int>(ETickGroup::PostUpdateWork);
             ++Group)
        {
            RunTickGroup(static_cast<ETickGroup>(Group));
        }
    }
    catch (...)
    {
        EndFrame();
        throw;
    }
    EndFrame();
}

bool FTickTaskManager::BeginFrame(float DeltaSeconds)
{
    if (!CheckGameThread("FTickTaskManager::BeginFrame")
        || bTicking
        || !std::isfinite(DeltaSeconds)
        || DeltaSeconds < 0.0f)
    {
        return false;
    }
    bTicking = false;
    FrameRegistrationLimit = NextRegistrationId;
    FrameDeltaSeconds = DeltaSeconds;
    LastCompletedGroup = -1;
    bTicking = true;
    return true;
}

bool FTickTaskManager::RunTickGroup(ETickGroup Group)
{
    if (!CheckGameThread("FTickTaskManager::RunTickGroup") || !bTicking)
    {
        return false;
    }
    const int GroupIndex = static_cast<int>(Group);
    if (GroupIndex != LastCompletedGroup + 1
        || GroupIndex < static_cast<int>(ETickGroup::PrePhysics)
        || GroupIndex > static_cast<int>(ETickGroup::PostUpdateWork))
    {
        PICO_LOG(LogEngine, Error, "Tick group {} was requested out of frame order", GroupIndex);
        return false;
    }
    TickGroup(FrameRegistrationLimit, GroupIndex, FrameDeltaSeconds);
    LastCompletedGroup = GroupIndex;
    return true;
}

void FTickTaskManager::EndFrame()
{
    if (!CheckGameThread("FTickTaskManager::EndFrame") || !bTicking)
    {
        return;
    }
    bTicking = false;
    FrameRegistrationLimit = 0;
    FrameDeltaSeconds = 0.0f;
    LastCompletedGroup = -1;
}

void FTickTaskManager::Reset()
{
    if (!CheckGameThread("FTickTaskManager::Reset")) return;
    for (FRegisteredTick& Entry : RegisteredTicks)
    {
        if (Entry.Function != nullptr)
        {
            Entry.Function->Manager = nullptr;
            Entry.Function->OwnerHandle = {};
            Entry.Function->RegistrationId = 0;
            Entry.Function->AccumulatedSeconds = 0.0f;
            Entry.Function->PrerequisiteIds.clear();
        }
    }
    RegisteredTicks.clear();
    bTicking = false;
    FrameRegistrationLimit = 0;
    FrameDeltaSeconds = 0.0f;
    LastCompletedGroup = -1;
}

std::size_t FTickTaskManager::GetRegisteredTickFunctionCount() const
{
    return RegisteredTicks.size();
}

bool FTickTaskManager::IsTicking() const { return bTicking; }

FTickTaskManager::FRegisteredTick* FTickTaskManager::FindRegisteredTick(uint64 Id)
{
    const auto Found = std::find_if(RegisteredTicks.begin(), RegisteredTicks.end(),
        [Id](const FRegisteredTick& Entry) { return Entry.Id == Id; });
    return Found != RegisteredTicks.end() ? &*Found : nullptr;
}

const FTickTaskManager::FRegisteredTick* FTickTaskManager::FindRegisteredTick(uint64 Id) const
{
    const auto Found = std::find_if(RegisteredTicks.begin(), RegisteredTicks.end(),
        [Id](const FRegisteredTick& Entry) { return Entry.Id == Id; });
    return Found != RegisteredTicks.end() ? &*Found : nullptr;
}

void FTickTaskManager::TickGroup(uint64 RegistrationLimit, int GroupIndex, float DeltaSeconds)
{
    const ETickGroup Group = static_cast<ETickGroup>(GroupIndex);
    std::vector<uint64> Nodes;
    for (const FRegisteredTick& Entry : RegisteredTicks)
    {
        FTickFunction* Function = Entry.Function;
        if (Entry.Id < RegistrationLimit
            && Function != nullptr
            && Function->GetTickGroup() == Group
            && Function->IsTickEnabled()
            && ResolveObject(Function->OwnerHandle) != nullptr)
        {
            Nodes.push_back(Entry.Id);
        }
    }

    std::unordered_map<uint64, std::size_t> InDegree;
    std::unordered_map<uint64, std::vector<uint64>> Dependents;
    for (uint64 Id : Nodes) InDegree.emplace(Id, 0);
    for (uint64 Id : Nodes)
    {
        const FRegisteredTick* Entry = FindRegisteredTick(Id);
        if (Entry == nullptr || Entry->Function == nullptr) continue;
        for (uint64 PrerequisiteId : Entry->Function->PrerequisiteIds)
        {
            const FRegisteredTick* Prerequisite = FindRegisteredTick(PrerequisiteId);
            if (Prerequisite == nullptr || Prerequisite->Function == nullptr) continue;
            if (static_cast<int>(Prerequisite->Function->GetTickGroup()) > GroupIndex)
            {
                PICO_LOG(LogEngine, Warning, "Tick prerequisite {} belongs to a later group", PrerequisiteId);
                continue;
            }
            if (Prerequisite->Function->GetTickGroup() == Group && InDegree.contains(PrerequisiteId))
            {
                ++InDegree[Id];
                Dependents[PrerequisiteId].push_back(Id);
            }
        }
    }

    std::vector<uint64> Ready;
    for (const auto& [Id, Degree] : InDegree) if (Degree == 0) Ready.push_back(Id);
    std::sort(Ready.begin(), Ready.end(), std::greater<>());
    std::vector<uint64> Ordered;
    while (!Ready.empty())
    {
        const uint64 Id = Ready.back();
        Ready.pop_back();
        Ordered.push_back(Id);
        for (uint64 DependentId : Dependents[Id])
        {
            if (--InDegree[DependentId] == 0)
            {
                Ready.push_back(DependentId);
                std::sort(Ready.begin(), Ready.end(), std::greater<>());
            }
        }
    }
    if (Ordered.size() != Nodes.size())
    {
        PICO_LOG(LogEngine, Error, "Tick prerequisite cycle detected in group {}", GroupIndex);
        for (uint64 Id : Nodes)
        {
            if (std::find(Ordered.begin(), Ordered.end(), Id) == Ordered.end()) Ordered.push_back(Id);
        }
    }

    for (uint64 Id : Ordered)
    {
        FRegisteredTick* LiveEntry = FindRegisteredTick(Id);
        if (LiveEntry == nullptr || LiveEntry->Function == nullptr) continue;
        FTickFunction* Function = LiveEntry->Function;
        if (!Function->IsTickEnabled() || ResolveObject(Function->OwnerHandle) == nullptr) continue;
        float ExecutionDelta = DeltaSeconds;
        if (Function->TickInterval > 0.0f)
        {
            Function->AccumulatedSeconds += DeltaSeconds;
            if (Function->AccumulatedSeconds + 0.000001f < Function->TickInterval) continue;
            ExecutionDelta = Function->AccumulatedSeconds;
            Function->AccumulatedSeconds = 0.0f;
        }
        Function->ExecuteTick(ExecutionDelta);
    }
}
}
