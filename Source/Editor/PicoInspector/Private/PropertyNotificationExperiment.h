#pragma once

#include "InspectorExperiment.h"

#include "Pico/Core/Delegate.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/PropertyChange.h"

#include <string>
#include <vector>

namespace Pico
{
class PDemoCharacter;
class PProperty;

class FPropertyNotificationExperiment final : public IInspectorExperiment
{
public:
    ~FPropertyNotificationExperiment() override;

    const char* GetName() const override;
    bool SetUp() override;
    void Draw() override;
    void Reset() override;
    void TearDown() override;

private:
    PDemoCharacter* GetInstance() const;
    PDemoCharacter* GetFutureInstance() const;
    PDemoCharacter* GetDefaultObject() const;
    const PProperty* GetHealthProperty() const;
    EPropertyChangeType GetSelectedChangeType() const;
    void BindNotifications();
    void SetInstanceHealth();
    void SetDefaultHealth();
    void SpawnFutureInstance();
    void AddEvent(
        const char* Phase,
        const char* Target,
        PDemoCharacter* Character,
        const FPropertyChangedEvent& Event);

    FObjectHandle InstanceHandle;
    FObjectHandle FutureInstanceHandle;
    FDelegateHandle InstancePreHandle;
    FDelegateHandle InstancePostHandle;
    FDelegateHandle DefaultPreHandle;
    FDelegateHandle DefaultPostHandle;
    std::vector<std::string> EventLog;
    int OriginalDefaultHealth = 100;
    int NewHealth = 75;
    int SelectedChangeType = 0;
    int PreEventCount = 0;
    int PostEventCount = 0;
};
}
