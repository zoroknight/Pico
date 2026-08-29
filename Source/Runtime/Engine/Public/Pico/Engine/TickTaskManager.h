#pragma once

#include "Pico/Core/Types.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace Pico
{
class FTickFunction;
class PObject;
enum class ETickGroup : uint8;

class FTickTaskManager
{
public:
    struct FCycleDiagnostic
    {
        ETickGroup Group;
        std::vector<uint64> RegistrationIds;
        std::string Message;
    };

    bool RegisterTickFunction(FTickFunction& TickFunction, PObject* Owner);
    void UnregisterTickFunction(FTickFunction& TickFunction);
    void Tick(float DeltaSeconds);
    bool BeginFrame(float DeltaSeconds);
    bool RunTickGroup(ETickGroup Group);
    void EndFrame();
    void Reset();
    std::size_t GetRegisteredTickFunctionCount() const;
    bool IsTicking() const;
    uint64 GetScheduleGeneration() const;
    uint64 GetScheduleBuildCount(ETickGroup Group) const;
    const std::vector<FCycleDiagnostic>& GetCycleDiagnostics() const;

private:
    friend class FTickFunction;

    struct FRegisteredTick
    {
        uint64 Id = 0;
        FTickFunction* Function = nullptr;
    };

    struct FCachedTickGroup
    {
        std::vector<uint64> OrderedIds;
        uint64 BuiltGeneration = 0;
        uint64 BuildCount = 0;
    };

    FRegisteredTick* FindRegisteredTick(uint64 Id);
    const FRegisteredTick* FindRegisteredTick(uint64 Id) const;
    void MarkGroupDirty(ETickGroup Group);
    void NotifyTickGroupChanged(
        uint64 RegistrationId,
        ETickGroup OldGroup,
        ETickGroup NewGroup);
    void NotifyPrerequisitesChanged(ETickGroup Group);
    void BuildSchedule(int GroupIndex);
    void TickGroup(uint64 FrameRegistrationLimit, int GroupIndex, float DeltaSeconds);

    std::unordered_map<uint64, FRegisteredTick> RegisteredTicks;
    std::array<FCachedTickGroup, 4> CachedGroups;
    std::array<uint64, 4> GroupGenerations {};
    std::vector<FCycleDiagnostic> CycleDiagnostics;
    uint64 NextRegistrationId = 1;
    uint64 ScheduleGeneration = 1;
    uint64 FrameRegistrationLimit = 0;
    float FrameDeltaSeconds = 0.0f;
    int LastCompletedGroup = -1;
    bool bTicking = false;
};
}
