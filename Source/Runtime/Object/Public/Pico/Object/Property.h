#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Name.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/Types.h"

#include <cstddef>
#include <memory>
#include <type_traits>

namespace Pico
{
class PClass;
class PObject;

namespace Detail
{
template <typename T>
const void* GetNativeTypeToken()
{
    static const int Token = 0;
    return &Token;
}
}

template <typename TMemberPointer>
struct TMemberPointerTraits;

template <typename TObject, typename TValue>
struct TMemberPointerTraits<TValue TObject::*>
{
    using ObjectType = TObject;
    using ValueType = TValue;
};

enum class EPropertyType : uint8
{
    Int32,
    Float,
    Bool,
    Vector3,
    Rotator,
    Transform,
    AssetPath
};

enum class EAssetReferenceType : uint8
{
    None,
    StaticMesh,
    Texture,
    Material
};

enum class EPropertyFlags : uint32
{
    None = 0,
    Editable = 1 << 0,
    Serializable = 1 << 1,
    Transient = 1 << 2,
    Replicated = 1 << 3,
    ReadOnly = 1 << 4
};

constexpr EPropertyFlags operator|(EPropertyFlags Left, EPropertyFlags Right)
{
    return static_cast<EPropertyFlags>(
        static_cast<uint32>(Left) | static_cast<uint32>(Right));
}

constexpr EPropertyFlags operator&(EPropertyFlags Left, EPropertyFlags Right)
{
    return static_cast<EPropertyFlags>(
        static_cast<uint32>(Left) & static_cast<uint32>(Right));
}

constexpr bool HasAnyPropertyFlags(
    EPropertyFlags Value,
    EPropertyFlags Flags)
{
    return (Value & Flags) != EPropertyFlags::None;
}

struct FPropertyMetadata
{
    EPropertyFlags Flags =
        EPropertyFlags::Editable | EPropertyFlags::Serializable;
    EAssetReferenceType AssetReferenceType = EAssetReferenceType::None;
};

constexpr std::size_t GetPropertyTypeSize(EPropertyType Type)
{
    switch (Type)
    {
    case EPropertyType::Int32:
        return sizeof(int32);
    case EPropertyType::Float:
        return sizeof(float);
    case EPropertyType::Bool:
        return sizeof(bool);
    case EPropertyType::Vector3:
        return sizeof(FVector3);
    case EPropertyType::Rotator:
        return sizeof(FRotator);
    case EPropertyType::Transform:
        return sizeof(FTransform);
    case EPropertyType::AssetPath:
        return sizeof(FAssetPath);
    }

    return 0;
}

template <typename TValue>
inline constexpr bool TIsSupportedPropertyType =
    std::is_same_v<TValue, int32>
    || std::is_same_v<TValue, float>
    || std::is_same_v<TValue, bool>
    || std::is_same_v<TValue, FVector3>
    || std::is_same_v<TValue, FRotator>
    || std::is_same_v<TValue, FTransform>
    || std::is_same_v<TValue, FAssetPath>;

template <typename TValue>
constexpr EPropertyType GetPropertyType()
{
    static_assert(TIsSupportedPropertyType<TValue>, "Unsupported reflected property type");

    if constexpr (std::is_same_v<TValue, int32>)
    {
        return EPropertyType::Int32;
    }
    else if constexpr (std::is_same_v<TValue, float>)
    {
        return EPropertyType::Float;
    }
    else if constexpr (std::is_same_v<TValue, bool>)
    {
        return EPropertyType::Bool;
    }
    else if constexpr (std::is_same_v<TValue, FVector3>)
    {
        return EPropertyType::Vector3;
    }
    else if constexpr (std::is_same_v<TValue, FRotator>)
    {
        return EPropertyType::Rotator;
    }
    else if constexpr (std::is_same_v<TValue, FTransform>)
    {
        return EPropertyType::Transform;
    }
    else
    {
        return EPropertyType::AssetPath;
    }
}

class PProperty
{
public:
    template <auto Member>
    static PProperty Create(
        FName InName,
        FPropertyMetadata InMetadata = {})
    {
        static_assert(std::is_member_object_pointer_v<decltype(Member)>, "Reflected properties require a data member");

        using FMemberTraits = TMemberPointerTraits<decltype(Member)>;
        using TObject = typename FMemberTraits::ObjectType;
        using TValue = typename FMemberTraits::ValueType;
        static_assert(std::is_base_of_v<PObject, TObject>, "Reflected properties require a PObject-derived owner");
        static_assert(!std::is_const_v<TValue>, "Reflected properties cannot target const data members");
        static_assert(TIsSupportedPropertyType<TValue>, "Unsupported reflected property type");

        return PProperty(
            InName,
            GetPropertyType<TValue>(),
            sizeof(TValue),
            Detail::GetNativeTypeToken<TObject>(),
            [](PObject* Object) -> void*
            {
                return std::addressof(static_cast<TObject*>(Object)->*Member);
            },
            [](const PObject* Object) -> const void*
            {
                return std::addressof(static_cast<const TObject*>(Object)->*Member);
            },
            InMetadata);
    }

    template <auto Member>
    static PProperty CreateAssetReference(
        FName InName,
        EAssetReferenceType InAssetReferenceType)
    {
        using FMemberTraits = TMemberPointerTraits<decltype(Member)>;
        using TValue = typename FMemberTraits::ValueType;
        static_assert(
            std::is_same_v<TValue, FAssetPath>,
            "Asset reference metadata requires an FAssetPath property");

        FPropertyMetadata Metadata;
        Metadata.AssetReferenceType = InAssetReferenceType;
        return Create<Member>(InName, Metadata);
    }

    FName GetName() const;
    EPropertyType GetType() const;
    const FPropertyMetadata& GetMetadata() const;
    EPropertyFlags GetFlags() const;
    bool HasAnyFlags(EPropertyFlags Flags) const;
    EAssetReferenceType GetAssetReferenceType() const;
    std::size_t GetSize() const;
    const PClass* GetOwnerClass() const;

    template <typename TValue>
    TValue* GetValuePtr(PObject* Object) const
    {
        static_assert(TIsSupportedPropertyType<TValue>, "Unsupported reflected property type");
        return static_cast<TValue*>(GetValueAddress(Object, GetPropertyType<TValue>(), sizeof(TValue)));
    }

    template <typename TValue>
    const TValue* GetValuePtr(const PObject* Object) const
    {
        static_assert(TIsSupportedPropertyType<TValue>, "Unsupported reflected property type");
        return static_cast<const TValue*>(GetValueAddress(Object, GetPropertyType<TValue>(), sizeof(TValue)));
    }

    template <typename TValue>
    bool GetValue(const PObject* Object, TValue& OutValue) const
    {
        const TValue* Value = GetValuePtr<TValue>(Object);
        if (Value == nullptr)
        {
            return false;
        }

        OutValue = *Value;
        return true;
    }

    template <typename TValue>
    bool SetValue(PObject* Object, const TValue& Value) const
    {
        TValue* Destination = GetValuePtr<TValue>(Object);
        if (Destination == nullptr)
        {
            return false;
        }

        *Destination = Value;
        return true;
    }

private:
    using FMutableAccessor = void* (*)(PObject*);
    using FConstAccessor = const void* (*)(const PObject*);

    PProperty(
        FName InName,
        EPropertyType InType,
        std::size_t InSize,
        const void* InOwnerTypeToken,
        FMutableAccessor InMutableAccessor,
        FConstAccessor InConstAccessor,
        FPropertyMetadata InMetadata);

    bool HasValidAccessors() const;
    const void* GetOwnerTypeToken() const;
    void* GetValueAddress(PObject* Object, EPropertyType ExpectedType, std::size_t ExpectedSize) const;
    const void* GetValueAddress(
        const PObject* Object,
        EPropertyType ExpectedType,
        std::size_t ExpectedSize) const;

    friend class PClass;

    FName Name;
    EPropertyType Type;
    FPropertyMetadata Metadata;
    std::size_t Size = 0;
    const void* OwnerTypeToken = nullptr;
    FMutableAccessor MutableAccessor = nullptr;
    FConstAccessor ConstAccessor = nullptr;
    const PClass* OwnerClass = nullptr;
};
}
