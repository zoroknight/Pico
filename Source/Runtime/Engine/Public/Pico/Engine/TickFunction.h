#pragma once

#include "Pico/Core/Types.h"
#include "Pico/Object/ObjectTypes.h"

#include <vector>

namespace Pico
{
class FTickTaskManager;
class PActor;
class PActorComponent;

enum class ETickGroup : uint8
{
    PrePhysics,
    DuringPhysics,
    PostPhysics,
    PostUpdateWork
};

class FTickFunction
{
public:
    virtual ~FTickFunction() = default;

    void SetCanEverTick(bool bValue);
    bool CanEverTick() const;
    void SetTickEnabled(bool bValue);
    bool IsTickEnabled() const;
    void SetStartWithTickEnabled(bool bValue);
    bool ShouldStartWithTickEnabled() const;
    void SetTickGroup(ETickGroup Group);
    ETickGroup GetTickGroup() const;
    void SetTickInterval(float Seconds);
    float GetTickInterval() const;
    bool IsRegistered() const;
    uint64 GetRegistrationId() const;

    bool AddPrerequisite(FTickFunction& Prerequisite);
    bool RemovePrerequisite(const FTickFunction& Prerequisite);
    void ClearPrerequisites();

protected:
    virtual void ExecuteTick(float DeltaSeconds) = 0;
    FObjectHandle GetOwnerHandle() const;

private:
    friend class FTickTaskManager;

    FObjectHandle OwnerHandle;
    FTickTaskManager* Manager = nullptr;
    std::vector<uint64> PrerequisiteIds;
    uint64 RegistrationId = 0;
    float TickInterval = 0.0f;
    float AccumulatedSeconds = 0.0f;
    ETickGroup TickGroup = ETickGroup::PrePhysics;
    bool bCanEverTick = false;
    bool bStartWithTickEnabled = true;
    bool bTickEnabled = true;
};

class FActorTickFunction final : public FTickFunction
{
protected:
    void ExecuteTick(float DeltaSeconds) override;
};

class FActorComponentTickFunction final : public FTickFunction
{
protected:
    void ExecuteTick(float DeltaSeconds) override;
};
}
