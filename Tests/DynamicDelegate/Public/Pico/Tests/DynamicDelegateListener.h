#pragma once

#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Tests/DynamicDelegateListener.generated.h"

namespace PicoTest
{
PCLASS()
class PDynamicDelegateListener final : public Pico::PObject
{
    GENERATED_BODY()

public:
    PFUNCTION(Callable)
    void OnValue(Pico::int32 Value);
    PFUNCTION(Callable)
    void OnLateValue(Pico::int32 Value);
    PFUNCTION(Callable)
    void OnPair(Pico::int32 First, float Second);
    PFUNCTION(Callable)
    Pico::int32 ReturnValue(Pico::int32 Value);
    PFUNCTION(Callable)
    void Throwing(Pico::int32 Value);
    PFUNCTION(Callable)
    void OnTypedObject(PDynamicDelegateListener* Value);
    PFUNCTION(Callable)
    void OnBaseObject(Pico::PObject* Value);

    void ConfigureRemoveSelf(
        Pico::TDynamicMulticastDelegate<void(Pico::int32)>* InDelegate,
        Pico::FDelegateHandle InHandle);
    void ConfigureAddLate(
        Pico::TDynamicMulticastDelegate<void(Pico::int32)>* InDelegate,
        PDynamicDelegateListener* InTarget);

    Pico::int32 GetValueCallCount() const;
    Pico::int32 GetLateCallCount() const;
    Pico::int32 GetLastValue() const;
    Pico::int32 GetObjectCallCount() const;

protected:
    explicit PDynamicDelegateListener(
        const Pico::FObjectConstructionParams& Params);

private:
    PPROPERTY(NotEditable)
    Pico::TDynamicMulticastDelegate<void(Pico::int32)> PersistedEvent;
    Pico::TDynamicMulticastDelegate<void(Pico::int32)>* MutationDelegate = nullptr;
    PDynamicDelegateListener* LateTarget = nullptr;
    Pico::FDelegateHandle SelfHandle;
    Pico::int32 ValueCallCount = 0;
    Pico::int32 LateCallCount = 0;
    Pico::int32 LastValue = 0;
    Pico::int32 ObjectCallCount = 0;
};
}
