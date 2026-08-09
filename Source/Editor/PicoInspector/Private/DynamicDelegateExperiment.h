#pragma once

#include "InspectorExperiment.h"

#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectTypes.h"

#include <array>
#include <string>
#include <vector>

namespace Pico
{
class PObject;

class FDynamicDelegateExperiment final : public IInspectorExperiment
{
public:
    ~FDynamicDelegateExperiment() override;

    const char* GetName() const override;
    bool SetUp() override;
    void Draw() override;
    void Reset() override;
    void TearDown() override;

private:
    PObject* GetSelectedTarget() const;
    std::vector<const PFunction*> GetSelectedFunctions() const;
    void AddBinding(bool bUnique);
    void RemoveSelectedBinding();
    void RemoveSelectedTargetBindings();
    void Broadcast();
    void DestroySelectedTarget();
    void RequestGC();
    void RunGCSafePoint();
    void RunIsolatedCollection();
    bool OwnsHandle(FObjectHandle Handle) const;
    void AddLog(std::string Message);

    TDynamicMulticastDelegate<void(int32, int32)> Delegate;
    std::array<FObjectHandle, 3> TargetHandles {};
    FDelegateHandle SelectedBindingHandle;
    FGarbageCollectionResult LastGCResult;
    FDynamicDelegateBroadcastReport LastBroadcastReport;
    std::vector<std::string> EventLog;
    std::size_t SelectedTargetIndex = 0;
    std::size_t SelectedFunctionIndex = 0;
    int32 OldHealth = 100;
    int32 NewHealth = 75;
    bool bHasBroadcastResult = false;
    bool bHasGCResult = false;
};
}
