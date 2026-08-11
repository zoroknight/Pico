#pragma once

#include "Pico/Core/Delegate.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/ObjectPtr.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Pico
{
enum class EDynamicDelegateBindResult : uint8
{
    Success,
    InvalidTarget,
    FunctionNotFound,
    FunctionNotCallable,
    SignatureMismatch,
    AlreadyBound
};

struct FDynamicDelegateBindingResult
{
    EDynamicDelegateBindResult Result = EDynamicDelegateBindResult::InvalidTarget;
    FDelegateHandle Handle;

    bool IsSuccess() const { return Result == EDynamicDelegateBindResult::Success; }
};

enum class EDynamicDelegateBroadcastResult : uint8
{
    Success,
    ArgumentCountMismatch,
    ArgumentTypeMismatch
};

struct FDynamicDelegateBroadcastReport
{
    EDynamicDelegateBroadcastResult Result = EDynamicDelegateBroadcastResult::Success;
    std::size_t SnapshotBindingCount = 0;
    std::size_t InvokedBindingCount = 0;
    std::size_t FailedBindingCount = 0;
    std::size_t RemovedInvalidBindingCount = 0;
    EFunctionInvokeResult LastInvokeFailure = EFunctionInvokeResult::Success;
};

struct FDynamicDelegateBindingView
{
    FDelegateHandle Handle;
    FObjectHandle TargetHandle;
    FName FunctionName;
    bool bTargetAlive = false;
};

class FDynamicMulticastDelegate
{
public:
    explicit FDynamicMulticastDelegate(
        std::vector<FFunctionValueDescriptor> InParameters = {});

    FDynamicMulticastDelegate(const FDynamicMulticastDelegate&) = delete;
    FDynamicMulticastDelegate& operator=(const FDynamicMulticastDelegate&) = delete;
    FDynamicMulticastDelegate(FDynamicMulticastDelegate&&) = delete;
    FDynamicMulticastDelegate& operator=(FDynamicMulticastDelegate&&) = delete;

    FDynamicDelegateBindingResult AddDynamic(PObject* Target, FName FunctionName);
    FDynamicDelegateBindingResult AddUniqueDynamic(PObject* Target, FName FunctionName);
    bool Remove(FDelegateHandle Handle);
    std::size_t RemoveAll(PObject* Target);
    void Clear();

    bool IsBound() const;
    bool IsBoundTo(const PObject* Target, FName FunctionName = {}) const;
    std::size_t Num() const;
    std::size_t CompactInvalidBindings();
    const std::vector<FFunctionValueDescriptor>& GetParameters() const;
    std::vector<FDynamicDelegateBindingView> GetBindings() const;
    bool IsFunctionCompatible(const PFunction& Function) const;

    FDynamicDelegateBroadcastReport Broadcast(
        std::span<const FFunctionValue> Arguments = {});

private:
    struct FBinding
    {
        FDelegateHandle Handle;
        TWeakObjectPtr<PObject> Target;
        FName FunctionName;
        bool bActive = true;
    };

    FDynamicDelegateBindingResult AddDynamicInternal(
        PObject* Target,
        FName FunctionName,
        bool bUnique);
    bool AreArgumentsCompatible(std::span<const FFunctionValue> Arguments) const;
    FBinding* FindBinding(FDelegateHandle Handle);
    const FBinding* FindBinding(FDelegateHandle Handle) const;
    void FinishBroadcast();
    void CompactIfIdle();

    std::vector<FFunctionValueDescriptor> Parameters;
    std::vector<FBinding> Bindings;
    std::size_t BroadcastDepth = 0;
};

namespace Detail
{
template <typename T>
FFunctionValue MakeDynamicDelegateArgument(T&& Value)
{
    using TValue = TFunctionBaseType<T>;
    if constexpr (IsSupportedObjectPointer<T>)
    {
        return static_cast<PObject*>(Value);
    }
    else
    {
        return FFunctionValue(TValue(std::forward<T>(Value)));
    }
}
}

template <typename TSignature>
class TDynamicMulticastDelegate;

template <typename... TArgs>
class TDynamicMulticastDelegate<void(TArgs...)>
{
public:
    TDynamicMulticastDelegate()
        : Delegate(MakeParameters())
    {
        static_assert(
            (Detail::IsSupportedFunctionType<TArgs> && ...),
            "Dynamic delegate contains an unsupported parameter type");
        static_assert(
            ((!std::is_reference_v<TArgs>
                || std::is_const_v<std::remove_reference_t<TArgs>>) && ...),
            "Dynamic delegate parameters cannot use mutable references");
    }

    FDynamicDelegateBindingResult AddDynamic(PObject* Target, FName FunctionName)
    {
        return Delegate.AddDynamic(Target, FunctionName);
    }

    FDynamicDelegateBindingResult AddUniqueDynamic(PObject* Target, FName FunctionName)
    {
        return Delegate.AddUniqueDynamic(Target, FunctionName);
    }

    bool Remove(FDelegateHandle Handle) { return Delegate.Remove(Handle); }
    std::size_t RemoveAll(PObject* Target) { return Delegate.RemoveAll(Target); }
    void Clear() { Delegate.Clear(); }
    bool IsBound() const { return Delegate.IsBound(); }
    bool IsBoundTo(const PObject* Target, FName FunctionName = {}) const
    {
        return Delegate.IsBoundTo(Target, FunctionName);
    }
    std::size_t Num() const { return Delegate.Num(); }
    std::size_t CompactInvalidBindings() { return Delegate.CompactInvalidBindings(); }
    const std::vector<FFunctionValueDescriptor>& GetParameters() const
    {
        return Delegate.GetParameters();
    }
    std::vector<FDynamicDelegateBindingView> GetBindings() const
    {
        return Delegate.GetBindings();
    }

    FDynamicDelegateBroadcastReport Broadcast(TArgs... Arguments)
    {
        const std::array<FFunctionValue, sizeof...(TArgs)> Values {
            Detail::MakeDynamicDelegateArgument<TArgs>(std::forward<TArgs>(Arguments))...
        };
        return Delegate.Broadcast(Values);
    }

    FDynamicMulticastDelegate& GetRuntimeDelegate() { return Delegate; }
    const FDynamicMulticastDelegate& GetRuntimeDelegate() const { return Delegate; }

private:
    static std::vector<FFunctionValueDescriptor> MakeParameters()
    {
        return {Detail::MakeFunctionValueDescriptor<TArgs>()...};
    }

    FDynamicMulticastDelegate Delegate;
};
}
