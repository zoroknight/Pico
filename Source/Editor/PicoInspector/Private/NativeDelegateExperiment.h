#pragma once

#include "InspectorExperiment.h"

#include "Pico/Core/Delegate.h"
#include "Pico/Object/ObjectTypes.h"

#include <string>
#include <vector>

namespace Pico
{
class PDemoCharacter;
class PDemoHealthObserver;

class FNativeDelegateExperiment final : public IInspectorExperiment
{
public:
    ~FNativeDelegateExperiment() override;

    const char* GetName() const override;
    bool SetUp() override;
    void Draw() override;
    void Reset() override;
    void TearDown() override;

private:
    PDemoCharacter* GetCharacter() const;
    PDemoHealthObserver* GetObserver() const;
    void AddLambdaListener();
    void AddObjectListener();
    void RemoveLambdaListener();
    void RemoveObjectListeners();
    void DestroyObserver();
    void ClearListeners();
    void InvokeApplyDamage();
    void InvokeApplyDamageWithWrongType();
    void BroadcastManualEvent();
    void SyncObjectNotifications();
    void AddLog(std::string Message);

    FObjectHandle CharacterHandle;
    FObjectHandle ObserverHandle;
    FDelegateHandle LambdaHandle;
    FDelegateHandle ObjectHandle;
    std::vector<std::string> EventLog;
    int LambdaCallCount = 0;
    int LastObservedObjectCallCount = 0;
    int ObjectCallCountBeforeDestroy = 0;
    int Damage = 20;
    int ManualOldHealth = 100;
    int ManualNewHealth = 75;
    bool bObjectBindingWasAdded = false;
};
}
