#pragma once

#include "InspectorExperiment.h"

#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectTypes.h"

#include <array>
#include <string>
#include <vector>

namespace Pico
{
class FGarbageCollectionExperiment final : public IInspectorExperiment
{
public:
    ~FGarbageCollectionExperiment() override;

    const char* GetName() const override;
    bool SetUp() override;
    void Draw() override;
    void Reset() override;
    void TearDown() override;

private:
    void RequestCollection(EGarbageCollectionReason Reason, const char* ReasonName);
    void RunSafePoint();
    void CollectNow();
    void RunCollection(bool bRequireRequest, const char* TriggerName);
    void AddLog(std::string Message);
    bool OwnsHandle(FObjectHandle Handle) const;

    std::array<FObjectHandle, 5> Handles {};
    FGarbageCollectionResult LastResult;
    EGarbageCollectionReason LastExecutedReasons = EGarbageCollectionReason::None;
    std::vector<std::string> EventLog;
    std::string LastTrigger;
    bool bHasResult = false;
};
}
