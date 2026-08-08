#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Object/Property.h"

#include <cstddef>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Pico
{
class PClass;
class PObject;

enum class EFunctionValueType : uint8
{
    Void,
    Int32,
    Float,
    Bool,
    Name,
    String,
    Vector3,
    Rotator,
    Transform,
    AssetPath,
    Object
};

using FFunctionValue = std::variant<
    std::monostate,
    int32,
    float,
    bool,
    FName,
    std::string,
    FVector3,
    FRotator,
    FTransform,
    FAssetPath,
    PObject*>;

enum class EFunctionFlags : uint16
{
    None = 0,
    Native = 1 << 0,
    Callable = 1 << 1,
    Pure = 1 << 2,
    Const = 1 << 3,
    Server = 1 << 4,
    Client = 1 << 5,
    NetMulticast = 1 << 6,
    Reliable = 1 << 7
};

constexpr EFunctionFlags operator|(EFunctionFlags Left, EFunctionFlags Right)
{
    return static_cast<EFunctionFlags>(
        static_cast<uint16>(Left) | static_cast<uint16>(Right));
}

constexpr EFunctionFlags operator&(EFunctionFlags Left, EFunctionFlags Right)
{
    return static_cast<EFunctionFlags>(
        static_cast<uint16>(Left) & static_cast<uint16>(Right));
}

constexpr EFunctionFlags& operator|=(EFunctionFlags& Left, EFunctionFlags Right)
{
    Left = Left | Right;
    return Left;
}

constexpr bool HasAnyFlags(EFunctionFlags Value, EFunctionFlags Flags)
{
    return (Value & Flags) != EFunctionFlags::None;
}

enum class EFunctionInvokeResult : uint8
{
    Success,
    InvalidFunction,
    InvalidTarget,
    ArgumentCountMismatch,
    ArgumentTypeMismatch,
    InvalidObjectArgument,
    MissingReturnStorage,
    InvocationFailed
};

struct FFunctionValueDescriptor
{
    EFunctionValueType Type = EFunctionValueType::Void;
    const PClass* ObjectClass = nullptr;
};

struct FFunctionParameter
{
    FName Name;
    FFunctionValueDescriptor Value;
};

namespace Detail
{
template <typename T>
struct TMemberFunctionTraits;

template <typename TOwner, typename TReturn, typename... TArgs>
struct TMemberFunctionTraits<TReturn (TOwner::*)(TArgs...)>
{
    using Owner = TOwner;
    using Return = TReturn;
    using Arguments = std::tuple<TArgs...>;
    static constexpr bool IsConst = false;
};

template <typename TOwner, typename TReturn, typename... TArgs>
struct TMemberFunctionTraits<TReturn (TOwner::*)(TArgs...) const>
{
    using Owner = TOwner;
    using Return = TReturn;
    using Arguments = std::tuple<TArgs...>;
    static constexpr bool IsConst = true;
};

template <typename T>
using TFunctionBaseType = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool IsSupportedObjectPointer =
    std::is_pointer_v<TFunctionBaseType<T>>
    && std::is_base_of_v<PObject, std::remove_pointer_t<TFunctionBaseType<T>>>;

template <typename T>
constexpr EFunctionValueType GetFunctionValueType()
{
    using TValue = TFunctionBaseType<T>;
    if constexpr (std::is_void_v<TValue>) return EFunctionValueType::Void;
    else if constexpr (std::is_same_v<TValue, int32>) return EFunctionValueType::Int32;
    else if constexpr (std::is_same_v<TValue, float>) return EFunctionValueType::Float;
    else if constexpr (std::is_same_v<TValue, bool>) return EFunctionValueType::Bool;
    else if constexpr (std::is_same_v<TValue, FName>) return EFunctionValueType::Name;
    else if constexpr (std::is_same_v<TValue, std::string>) return EFunctionValueType::String;
    else if constexpr (std::is_same_v<TValue, FVector3>) return EFunctionValueType::Vector3;
    else if constexpr (std::is_same_v<TValue, FRotator>) return EFunctionValueType::Rotator;
    else if constexpr (std::is_same_v<TValue, FTransform>) return EFunctionValueType::Transform;
    else if constexpr (std::is_same_v<TValue, FAssetPath>) return EFunctionValueType::AssetPath;
    else if constexpr (IsSupportedObjectPointer<T>) return EFunctionValueType::Object;
    else return static_cast<EFunctionValueType>(255);
}

template <typename T>
constexpr bool IsSupportedFunctionType =
    GetFunctionValueType<T>() != static_cast<EFunctionValueType>(255);

template <typename T>
FFunctionValueDescriptor MakeFunctionValueDescriptor()
{
    FFunctionValueDescriptor Result;
    Result.Type = GetFunctionValueType<T>();
    if constexpr (IsSupportedObjectPointer<T>)
    {
        Result.ObjectClass = std::remove_pointer_t<TFunctionBaseType<T>>::StaticClass();
    }
    return Result;
}

template <typename T>
decltype(auto) ReadFunctionArgument(const FFunctionValue& Value)
{
    using TValue = TFunctionBaseType<T>;
    if constexpr (IsSupportedObjectPointer<T>)
    {
        return static_cast<TValue>(std::get<PObject*>(Value));
    }
    else if constexpr (std::is_reference_v<T>)
    {
        return static_cast<T>(std::get<TValue>(Value));
    }
    else
    {
        return std::get<TValue>(Value);
    }
}
}

class PFunction
{
public:
    template <auto Method>
    static PFunction Create(
        FName InName,
        EFunctionFlags InFlags = EFunctionFlags::Callable,
        std::vector<FName> InParameterNames = {})
    {
        using Traits = Detail::TMemberFunctionTraits<decltype(Method)>;
        using TOwner = typename Traits::Owner;
        using TReturn = typename Traits::Return;
        using TArguments = typename Traits::Arguments;
        static_assert(std::is_base_of_v<PObject, TOwner>, "Reflected functions must belong to a PObject type");
        static_assert(Detail::IsSupportedFunctionType<TReturn>, "Unsupported reflected return type");
        static_assert(!std::is_reference_v<TReturn>, "Reflected functions cannot return references");
        static_assert(AreSupportedArguments<TArguments>(), "Unsupported reflected parameter type");

        constexpr std::size_t ArgumentCount = std::tuple_size_v<TArguments>;
        PFunction Result;
        Result.Name = InName;
        Result.Flags = InFlags | EFunctionFlags::Native;
        if constexpr (Traits::IsConst)
        {
            Result.Flags |= EFunctionFlags::Const;
        }
        Result.OwnerTypeToken = Detail::GetNativeTypeToken<TOwner>();
        Result.ReturnValue = Detail::MakeFunctionValueDescriptor<TReturn>();
        Result.Parameters = MakeParameters<TArguments>(std::move(InParameterNames));
        Result.InvokeThunk = &InvokeMember<Method>;
        Result.bMetadataValid = Result.ValidateCreatedMetadata(ArgumentCount, Traits::IsConst);
        return Result;
    }

    FName GetName() const;
    EFunctionFlags GetFlags() const;
    bool HasAnyFlags(EFunctionFlags InFlags) const;
    const PClass* GetOwnerClass() const;
    const std::vector<FFunctionParameter>& GetParameters() const;
    const FFunctionValueDescriptor& GetReturnValue() const;
    bool IsMetadataValid() const;
    EFunctionInvokeResult Invoke(
        PObject* Target,
        std::span<const FFunctionValue> Arguments = {},
        FFunctionValue* OutReturnValue = nullptr) const;

private:
    using FInvokeThunk = void (*)(PObject*, std::span<const FFunctionValue>, FFunctionValue*);

    template <typename TTuple, std::size_t... Indices>
    static consteval bool AreSupportedArgumentsImpl(std::index_sequence<Indices...>)
    {
        return ((Detail::IsSupportedFunctionType<std::tuple_element_t<Indices, TTuple>>
            && (!std::is_reference_v<std::tuple_element_t<Indices, TTuple>>
                || std::is_const_v<std::remove_reference_t<std::tuple_element_t<Indices, TTuple>>>)) && ...);
    }

    template <typename TTuple>
    static consteval bool AreSupportedArguments()
    {
        return AreSupportedArgumentsImpl<TTuple>(
            std::make_index_sequence<std::tuple_size_v<TTuple>> {});
    }

    template <typename TTuple, std::size_t... Indices>
    static std::vector<FFunctionParameter> MakeParametersImpl(
        std::vector<FName> Names,
        std::index_sequence<Indices...>)
    {
        std::vector<FFunctionParameter> Result;
        Result.reserve(sizeof...(Indices));
        (Result.push_back(FFunctionParameter {
            Indices < Names.size() ? Names[Indices] : FName(),
            Detail::MakeFunctionValueDescriptor<std::tuple_element_t<Indices, TTuple>>() }), ...);
        return Result;
    }

    template <typename TTuple>
    static std::vector<FFunctionParameter> MakeParameters(std::vector<FName> Names)
    {
        return MakeParametersImpl<TTuple>(
            std::move(Names),
            std::make_index_sequence<std::tuple_size_v<TTuple>> {});
    }

    template <auto Method, std::size_t... Indices>
    static void InvokeMemberImpl(
        PObject* Target,
        std::span<const FFunctionValue> Arguments,
        FFunctionValue* OutReturnValue,
        std::index_sequence<Indices...>)
    {
        using Traits = Detail::TMemberFunctionTraits<decltype(Method)>;
        using TOwner = typename Traits::Owner;
        using TReturn = typename Traits::Return;
        using TArguments = typename Traits::Arguments;
        TOwner* TypedTarget = static_cast<TOwner*>(Target);
        if constexpr (std::is_void_v<TReturn>)
        {
            (TypedTarget->*Method)(
                Detail::ReadFunctionArgument<std::tuple_element_t<Indices, TArguments>>(Arguments[Indices])...);
            if (OutReturnValue != nullptr)
            {
                *OutReturnValue = std::monostate {};
            }
        }
        else if constexpr (Detail::IsSupportedObjectPointer<TReturn>)
        {
            *OutReturnValue = static_cast<PObject*>((TypedTarget->*Method)(
                Detail::ReadFunctionArgument<std::tuple_element_t<Indices, TArguments>>(Arguments[Indices])...));
        }
        else
        {
            *OutReturnValue = (TypedTarget->*Method)(
                Detail::ReadFunctionArgument<std::tuple_element_t<Indices, TArguments>>(Arguments[Indices])...);
        }
    }

    template <auto Method>
    static void InvokeMember(
        PObject* Target,
        std::span<const FFunctionValue> Arguments,
        FFunctionValue* OutReturnValue)
    {
        using TArguments = typename Detail::TMemberFunctionTraits<decltype(Method)>::Arguments;
        InvokeMemberImpl<Method>(
            Target,
            Arguments,
            OutReturnValue,
            std::make_index_sequence<std::tuple_size_v<TArguments>> {});
    }

    bool ValidateCreatedMetadata(std::size_t ArgumentCount, bool bMethodIsConst) const;
    bool IsValueCompatible(const FFunctionValue& Value, const FFunctionValueDescriptor& Descriptor) const;

    friend class PClass;

    FName Name;
    EFunctionFlags Flags = EFunctionFlags::None;
    const PClass* OwnerClass = nullptr;
    const void* OwnerTypeToken = nullptr;
    std::vector<FFunctionParameter> Parameters;
    FFunctionValueDescriptor ReturnValue;
    FInvokeThunk InvokeThunk = nullptr;
    bool bMetadataValid = false;
};
}
