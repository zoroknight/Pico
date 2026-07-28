#pragma once

#include "Pico/Object/ReflectionMacros.h"

namespace Pico
{
class PActor;
class PWorld;

class PActorComponent : public PObject
{
    PICO_DECLARE_CLASS(PActorComponent, PObject)

public:
    PActor* GetOwner() const;
    PWorld* GetWorld() const;
    bool IsRegistered() const;

    void RegisterComponent();
    void UnregisterComponent();

protected:
    explicit PActorComponent(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

    virtual void OnRegister();
    virtual void OnUnregister();

private:
    bool bRegistered = false;
};
}
