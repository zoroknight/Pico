#pragma once

#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Engine/TickFunction.h"

namespace Pico
{
class PActor;
class PWorld;

class PActorComponent : public PObject
{
    PICO_DECLARE_CLASS(PActorComponent, PObject)

public:
    FActorComponentTickFunction PrimaryComponentTick;

    PActor* GetOwner() const;
    PWorld* GetWorld() const;
    bool IsRegistered() const;

    void RegisterComponent();
    void UnregisterComponent();
    virtual void TickComponent(float DeltaSeconds);

protected:
    explicit PActorComponent(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

    virtual void OnRegister();
    virtual void OnUnregister();

private:
    friend class FActorComponentTickFunction;
    void DispatchTickComponent(float DeltaSeconds);
    bool bRegistered = false;
};
}
