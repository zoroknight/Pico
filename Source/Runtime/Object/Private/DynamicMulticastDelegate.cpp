#include "Pico/Object/DynamicMulticastDelegate.h"

#include "Pico/Core/ScopeExit.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>

namespace Pico
{
namespace
{
bool IsDynamicDelegateTargetLive(PObject* Target)
{
    return Target != nullptr && !Target->IsBeginningDestroy();
}
}

FDynamicMulticastDelegate::FDynamicMulticastDelegate(
    std::vector<FFunctionValueDescriptor> InParameters)
    : Parameters(std::move(InParameters))
{
}

FDynamicDelegateBindingResult FDynamicMulticastDelegate::AddDynamic(
    PObject* Target,
    FName FunctionName)
{
    return AddDynamicInternal(Target, FunctionName, false);
}

FDynamicDelegateBindingResult FDynamicMulticastDelegate::AddUniqueDynamic(
    PObject* Target,
    FName FunctionName)
{
    return AddDynamicInternal(Target, FunctionName, true);
}

FDynamicDelegateBindingResult FDynamicMulticastDelegate::AddDynamicInternal(
    PObject* Target,
    FName FunctionName,
    bool bUnique)
{
    if (Target == nullptr
        || ResolveObject(Target->GetHandle()) != Target
        || Target->IsBeginningDestroy())
    {
        return {EDynamicDelegateBindResult::InvalidTarget, {}};
    }

    const PFunction* Function = Target->GetClass()->FindFunction(FunctionName);
    if (Function == nullptr)
    {
        return {EDynamicDelegateBindResult::FunctionNotFound, {}};
    }
    if (!Function->HasAnyFlags(EFunctionFlags::Callable))
    {
        return {EDynamicDelegateBindResult::FunctionNotCallable, {}};
    }
    if (!IsFunctionCompatible(*Function))
    {
        return {EDynamicDelegateBindResult::SignatureMismatch, {}};
    }
    if (bUnique && IsBoundTo(Target, FunctionName))
    {
        return {EDynamicDelegateBindResult::AlreadyBound, {}};
    }

    const FDelegateHandle Handle = FDelegateHandle::Generate();
    Bindings.push_back(FBinding {Handle, Target, FunctionName, true});
    return {EDynamicDelegateBindResult::Success, Handle};
}

bool FDynamicMulticastDelegate::Remove(FDelegateHandle Handle)
{
    if (!Handle.IsValid())
    {
        return false;
    }
    FBinding* Binding = FindBinding(Handle);
    if (Binding == nullptr || !Binding->bActive)
    {
        return false;
    }
    Binding->bActive = false;
    CompactIfIdle();
    return true;
}

std::size_t FDynamicMulticastDelegate::RemoveAll(PObject* Target)
{
    if (Target == nullptr)
    {
        return 0;
    }
    const FObjectHandle TargetHandle = Target->GetHandle();
    std::size_t RemovedCount = 0;
    for (FBinding& Binding : Bindings)
    {
        if (Binding.bActive && Binding.Target.GetHandle() == TargetHandle)
        {
            Binding.bActive = false;
            ++RemovedCount;
        }
    }
    CompactIfIdle();
    return RemovedCount;
}

void FDynamicMulticastDelegate::Clear()
{
    if (BroadcastDepth == 0)
    {
        Bindings.clear();
        return;
    }
    for (FBinding& Binding : Bindings)
    {
        Binding.bActive = false;
    }
}

bool FDynamicMulticastDelegate::IsBound() const
{
    return Num() != 0;
}

bool FDynamicMulticastDelegate::IsBoundTo(
    const PObject* Target,
    FName FunctionName) const
{
    if (Target == nullptr)
    {
        return false;
    }
    const FObjectHandle TargetHandle = Target->GetHandle();
    return std::any_of(
        Bindings.begin(),
        Bindings.end(),
        [TargetHandle, FunctionName](const FBinding& Binding)
        {
            return Binding.bActive
                && Binding.Target.GetHandle() == TargetHandle
                && (FunctionName.IsNone() || Binding.FunctionName == FunctionName)
                && IsDynamicDelegateTargetLive(Binding.Target.Get());
        });
}

std::size_t FDynamicMulticastDelegate::Num() const
{
    return static_cast<std::size_t>(std::count_if(
        Bindings.begin(),
        Bindings.end(),
        [](const FBinding& Binding)
        {
            return Binding.bActive
                && IsDynamicDelegateTargetLive(Binding.Target.Get());
        }));
}

std::size_t FDynamicMulticastDelegate::CompactInvalidBindings()
{
    std::size_t RemovedCount = 0;
    for (FBinding& Binding : Bindings)
    {
        if (Binding.bActive
            && !IsDynamicDelegateTargetLive(Binding.Target.Get()))
        {
            Binding.bActive = false;
            ++RemovedCount;
        }
    }
    CompactIfIdle();
    return RemovedCount;
}

const std::vector<FFunctionValueDescriptor>&
FDynamicMulticastDelegate::GetParameters() const
{
    return Parameters;
}

std::vector<FDynamicDelegateBindingView>
FDynamicMulticastDelegate::GetBindings() const
{
    std::vector<FDynamicDelegateBindingView> Result;
    Result.reserve(Bindings.size());
    for (const FBinding& Binding : Bindings)
    {
        if (!Binding.bActive)
        {
            continue;
        }
        Result.push_back(FDynamicDelegateBindingView {
            Binding.Handle,
            Binding.Target.GetHandle(),
            Binding.FunctionName,
            IsDynamicDelegateTargetLive(Binding.Target.Get())});
    }
    return Result;
}

FDynamicDelegateBroadcastReport FDynamicMulticastDelegate::Broadcast(
    std::span<const FFunctionValue> Arguments)
{
    FDynamicDelegateBroadcastReport Report;
    if (Arguments.size() != Parameters.size())
    {
        Report.Result = EDynamicDelegateBroadcastResult::ArgumentCountMismatch;
        return Report;
    }
    if (!AreArgumentsCompatible(Arguments))
    {
        Report.Result = EDynamicDelegateBroadcastResult::ArgumentTypeMismatch;
        return Report;
    }

    std::vector<FDelegateHandle> Snapshot;
    Snapshot.reserve(Bindings.size());
    for (const FBinding& Binding : Bindings)
    {
        if (Binding.bActive)
        {
            Snapshot.push_back(Binding.Handle);
        }
    }
    Report.SnapshotBindingCount = Snapshot.size();

    ++BroadcastDepth;
    auto Finish = MakeScopeExit([this]() { FinishBroadcast(); });
    for (const FDelegateHandle Handle : Snapshot)
    {
        FBinding* Binding = FindBinding(Handle);
        if (Binding == nullptr || !Binding->bActive)
        {
            continue;
        }

        PObject* Target = Binding->Target.Get();
        const FName FunctionName = Binding->FunctionName;
        if (!IsDynamicDelegateTargetLive(Target))
        {
            Binding->bActive = false;
            ++Report.RemovedInvalidBindingCount;
            continue;
        }

        const PFunction* Function = Target->GetClass()->FindFunction(FunctionName);
        if (Function == nullptr || !IsFunctionCompatible(*Function))
        {
            Binding->bActive = false;
            ++Report.RemovedInvalidBindingCount;
            continue;
        }

        const EFunctionInvokeResult InvokeResult =
            Target->ProcessEvent(Function, Arguments, nullptr);
        if (InvokeResult == EFunctionInvokeResult::Success)
        {
            ++Report.InvokedBindingCount;
        }
        else
        {
            ++Report.FailedBindingCount;
            Report.LastInvokeFailure = InvokeResult;
        }
    }
    return Report;
}

bool FDynamicMulticastDelegate::IsFunctionCompatible(
    const PFunction& Function) const
{
    if (Function.GetReturnValue().Type != EFunctionValueType::Void
        || Function.GetParameters().size() != Parameters.size())
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
    {
        const FFunctionValueDescriptor& Expected = Parameters[Index];
        const FFunctionValueDescriptor& Actual =
            Function.GetParameters()[Index].Value;
        if (Expected.Type != Actual.Type
            || (Expected.Type == EFunctionValueType::Object
                && Expected.ResolveObjectClass() != Actual.ResolveObjectClass()))
        {
            return false;
        }
    }
    return true;
}

bool FDynamicMulticastDelegate::AreArgumentsCompatible(
    std::span<const FFunctionValue> Arguments) const
{
    for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
    {
        if (!IsFunctionValueCompatible(Arguments[Index], Parameters[Index]))
        {
            return false;
        }
    }
    return true;
}

FDynamicMulticastDelegate::FBinding*
FDynamicMulticastDelegate::FindBinding(FDelegateHandle Handle)
{
    const auto Found = std::find_if(
        Bindings.begin(),
        Bindings.end(),
        [Handle](const FBinding& Binding)
        {
            return Binding.Handle == Handle;
        });
    return Found != Bindings.end() ? &*Found : nullptr;
}

const FDynamicMulticastDelegate::FBinding*
FDynamicMulticastDelegate::FindBinding(FDelegateHandle Handle) const
{
    const auto Found = std::find_if(
        Bindings.begin(),
        Bindings.end(),
        [Handle](const FBinding& Binding)
        {
            return Binding.Handle == Handle;
        });
    return Found != Bindings.end() ? &*Found : nullptr;
}

void FDynamicMulticastDelegate::FinishBroadcast()
{
    if (BroadcastDepth > 0)
    {
        --BroadcastDepth;
    }
    CompactIfIdle();
}

void FDynamicMulticastDelegate::CompactIfIdle()
{
    if (BroadcastDepth == 0)
    {
        std::erase_if(
            Bindings,
            [](const FBinding& Binding)
            {
                return !Binding.bActive;
            });
    }
}
}
