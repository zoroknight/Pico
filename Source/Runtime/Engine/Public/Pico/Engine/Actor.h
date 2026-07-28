#pragma once

#include "Pico/Object/ReflectionMacros.h"

namespace Pico
{
class PLevel;
class PWorld;

class PActor : public PObject
{
    PICO_DECLARE_CLASS(PActor, PObject)

public:
    PWorld* GetWorld() const;
    PLevel* GetLevel() const;
    bool HasBegunPlay() const;
    bool IsPendingDestroy() const;
    bool Destroy();

    virtual void BeginPlay();
    virtual void Tick(float DeltaSeconds);
    virtual void EndPlay();

protected:
    explicit PActor(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

private:
    void DispatchBeginPlay();
    void DispatchTick(float DeltaSeconds);
    void DispatchEndPlay();
    void MarkPendingDestroy();

    friend class PWorld;

    bool bHasBegunPlay = false;
    bool bHasEndedPlay = false;
    bool bPendingDestroy = false;
};
}
