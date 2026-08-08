#include "Pico/Object/Function.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <exception>

namespace Pico
{
FName PFunction::GetName() const
{
    return Name;
}

EFunctionFlags PFunction::GetFlags() const
{
    return Flags;
}

bool PFunction::HasAnyFlags(EFunctionFlags InFlags) const
{
    return Pico::HasAnyFlags(Flags, InFlags);
}

const PClass* PFunction::GetOwnerClass() const
{
    return OwnerClass;
}

const std::vector<FFunctionParameter>& PFunction::GetParameters() const
{
    return Parameters;
}

const FFunctionValueDescriptor& PFunction::GetReturnValue() const
{
    return ReturnValue;
}

bool PFunction::IsMetadataValid() const
{
    return bMetadataValid;
}

bool PFunction::ValidateCreatedMetadata(std::size_t ArgumentCount, bool bMethodIsConst) const
{
    const EFunctionFlags NetworkTargets =
        Flags & (EFunctionFlags::Server | EFunctionFlags::Client | EFunctionFlags::NetMulticast);
    const uint16 NetworkTargetBits = static_cast<uint16>(NetworkTargets);
    const bool bHasAtMostOneNetworkTarget =
        NetworkTargetBits == 0 || (NetworkTargetBits & (NetworkTargetBits - 1)) == 0;
    const bool bIsNetworkFunction = NetworkTargets != EFunctionFlags::None;
    const bool bHasValidReturn = !bIsNetworkFunction || ReturnValue.Type == EFunctionValueType::Void;
    const bool bHasValidReliableFlag =
        !Pico::HasAnyFlags(Flags, EFunctionFlags::Reliable) || bIsNetworkFunction;
    const bool bHasValidPureFlag =
        !Pico::HasAnyFlags(Flags, EFunctionFlags::Pure) || bMethodIsConst;
    const bool bHasValidConstFlag =
        bMethodIsConst == Pico::HasAnyFlags(Flags, EFunctionFlags::Const);
    if (Name.IsNone()
        || InvokeThunk == nullptr
        || Parameters.size() != ArgumentCount
        || !bHasAtMostOneNetworkTarget
        || !bHasValidReturn
        || !bHasValidReliableFlag
        || !bHasValidPureFlag
        || !bHasValidConstFlag)
    {
        return false;
    }

    for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
    {
        if (Parameters[Index].Name.IsNone()
            || Parameters[Index].Value.Type == EFunctionValueType::Void
            || std::find_if(
                Parameters.begin(),
                Parameters.begin() + static_cast<std::ptrdiff_t>(Index),
                [&Parameters = Parameters, Index](const FFunctionParameter& Parameter)
                {
                    return Parameter.Name == Parameters[Index].Name;
                }) != Parameters.begin() + static_cast<std::ptrdiff_t>(Index))
        {
            return false;
        }
    }
    return true;
}

bool PFunction::IsValueCompatible(
    const FFunctionValue& Value,
    const FFunctionValueDescriptor& Descriptor) const
{
    switch (Descriptor.Type)
    {
    case EFunctionValueType::Void: return std::holds_alternative<std::monostate>(Value);
    case EFunctionValueType::Int32: return std::holds_alternative<int32>(Value);
    case EFunctionValueType::Float: return std::holds_alternative<float>(Value);
    case EFunctionValueType::Bool: return std::holds_alternative<bool>(Value);
    case EFunctionValueType::Name: return std::holds_alternative<FName>(Value);
    case EFunctionValueType::String: return std::holds_alternative<std::string>(Value);
    case EFunctionValueType::Vector3: return std::holds_alternative<FVector3>(Value);
    case EFunctionValueType::Rotator: return std::holds_alternative<FRotator>(Value);
    case EFunctionValueType::Transform: return std::holds_alternative<FTransform>(Value);
    case EFunctionValueType::AssetPath: return std::holds_alternative<FAssetPath>(Value);
    case EFunctionValueType::Object:
    {
        if (!std::holds_alternative<PObject*>(Value))
        {
            return false;
        }
        PObject* Object = std::get<PObject*>(Value);
        return Object == nullptr
            || (ResolveObject(Object->GetHandle()) == Object
                && !Object->IsBeginningDestroy()
                && Object->IsA(Descriptor.ObjectClass));
    }
    }
    return false;
}

EFunctionInvokeResult PFunction::Invoke(
    PObject* Target,
    std::span<const FFunctionValue> Arguments,
    FFunctionValue* OutReturnValue) const
{
    if (!bMetadataValid || OwnerClass == nullptr || InvokeThunk == nullptr)
    {
        return EFunctionInvokeResult::InvalidFunction;
    }
    if (Target == nullptr
        || ResolveObject(Target->GetHandle()) != Target
        || Target->IsBeginningDestroy()
        || !Target->IsA(OwnerClass))
    {
        return EFunctionInvokeResult::InvalidTarget;
    }
    if (Arguments.size() != Parameters.size())
    {
        return EFunctionInvokeResult::ArgumentCountMismatch;
    }
    for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
    {
        if (!IsValueCompatible(Arguments[Index], Parameters[Index].Value))
        {
            return Parameters[Index].Value.Type == EFunctionValueType::Object
                && std::holds_alternative<PObject*>(Arguments[Index])
                ? EFunctionInvokeResult::InvalidObjectArgument
                : EFunctionInvokeResult::ArgumentTypeMismatch;
        }
    }
    if (ReturnValue.Type != EFunctionValueType::Void && OutReturnValue == nullptr)
    {
        return EFunctionInvokeResult::MissingReturnStorage;
    }

    try
    {
        InvokeThunk(Target, Arguments, OutReturnValue);
    }
    catch (const std::exception&)
    {
        return EFunctionInvokeResult::InvocationFailed;
    }
    catch (...)
    {
        return EFunctionInvokeResult::InvocationFailed;
    }
    return EFunctionInvokeResult::Success;
}
}
