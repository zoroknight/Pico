#include "Pico/Tests/DynamicDelegateListener.h"

#include <stdexcept>

namespace PicoTest
{
PDynamicDelegateListener::PDynamicDelegateListener(
    const Pico::FObjectConstructionParams& Params)
    : PObject(Params)
{
}

void PDynamicDelegateListener::OnValue(Pico::int32 Value)
{
    ++ValueCallCount;
    LastValue = Value;
    if (MutationDelegate != nullptr && SelfHandle.IsValid())
    {
        MutationDelegate->Remove(SelfHandle);
        SelfHandle.Reset();
    }
    if (MutationDelegate != nullptr && LateTarget != nullptr)
    {
        MutationDelegate->AddUniqueDynamic(
            LateTarget, Pico::FName("OnLateValue"));
    }
}

void PDynamicDelegateListener::OnLateValue(Pico::int32 Value)
{
    ++LateCallCount;
    LastValue = Value;
}

void PDynamicDelegateListener::OnPair(Pico::int32 First, float Second)
{
    LastValue = First + static_cast<Pico::int32>(Second);
}

Pico::int32 PDynamicDelegateListener::ReturnValue(Pico::int32 Value)
{
    return Value;
}

void PDynamicDelegateListener::Throwing(Pico::int32)
{
    throw std::runtime_error("Expected dynamic delegate listener failure");
}

void PDynamicDelegateListener::OnTypedObject(PDynamicDelegateListener*)
{
    ++ObjectCallCount;
}

void PDynamicDelegateListener::OnBaseObject(Pico::PObject*)
{
    ++ObjectCallCount;
}

void PDynamicDelegateListener::ConfigureRemoveSelf(
    Pico::TDynamicMulticastDelegate<void(Pico::int32)>* InDelegate,
    Pico::FDelegateHandle InHandle)
{
    MutationDelegate = InDelegate;
    SelfHandle = InHandle;
}

void PDynamicDelegateListener::ConfigureAddLate(
    Pico::TDynamicMulticastDelegate<void(Pico::int32)>* InDelegate,
    PDynamicDelegateListener* InTarget)
{
    MutationDelegate = InDelegate;
    LateTarget = InTarget;
}

Pico::int32 PDynamicDelegateListener::GetValueCallCount() const
{
    return ValueCallCount;
}

Pico::int32 PDynamicDelegateListener::GetLateCallCount() const
{
    return LateCallCount;
}

Pico::int32 PDynamicDelegateListener::GetLastValue() const
{
    return LastValue;
}

Pico::int32 PDynamicDelegateListener::GetObjectCallCount() const
{
    return ObjectCallCount;
}
}
