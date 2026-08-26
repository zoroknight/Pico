#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Name.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/Types.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/Object/PropertyChange.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace Pico
{
class PClass;
class PObject;
class FDynamicMulticastDelegate;
template <typename TSignature>
class TDynamicMulticastDelegate;

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
    AssetPath,
    Object,
    DynamicMulticastDelegate
};

enum class EAssetReferenceType : uint8
{
    None,
    StaticMesh,
    SkeletalMesh,
    AnimationClip,
    AnimationSet,
    AnimationMontage,
    CharacterProfile,
    ThirdPersonControlProfile,
    Texture,
    Material,
    Graph
};

enum class EObjectReferenceKind : uint8
{
    None,
    Strong,
    Weak
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

enum class EReplicationCondition : uint8
{
    Always,
    InitialOnly,
    OwnerOnly,
    SkipOwner
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
    EReplicationCondition ReplicationCondition =
        EReplicationCondition::Always;
    FName RepNotifyFunction;
    std::string DisplayName;
    std::string Description;
    std::string Semantic;
    std::string Units;
    std::optional<double> Minimum;
    std::optional<double> Maximum;

    struct FEnumOption
    {
        int32 Value = 0;
        std::string DisplayName;
    };
    std::vector<FEnumOption> EnumOptions;
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
    case EPropertyType::Object:
        return sizeof(FObjectHandle);
    case EPropertyType::DynamicMulticastDelegate:
        return 0;
    }

    return 0;
}

template <typename TValue>
struct TIsDynamicMulticastDelegate : std::false_type
{
};

template <typename TSignature>
struct TIsDynamicMulticastDelegate<TDynamicMulticastDelegate<TSignature>>
    : std::true_type
{
};

template <typename TValue>
inline constexpr bool TIsDynamicMulticastDelegateValue =
    TIsDynamicMulticastDelegate<std::remove_cv_t<TValue>>::value;

template <typename TValue>
inline constexpr bool TIsSupportedPropertyType =
    std::is_same_v<TValue, int32>
    || std::is_same_v<TValue, float>
    || std::is_same_v<TValue, bool>
    || std::is_same_v<TValue, FVector3>
    || std::is_same_v<TValue, FRotator>
    || std::is_same_v<TValue, FTransform>
    || std::is_same_v<TValue, FAssetPath>
    || TObjectPointerTraits<TValue>::IsObjectPointer
    || TIsDynamicMulticastDelegateValue<TValue>;

template <typename TValue>
constexpr EPropertyType GetPropertyType()
{
    static_assert(TIsSupportedPropertyType<TValue>, "Unsupported reflected property type");

    if constexpr (TIsDynamicMulticastDelegateValue<TValue>)
    {
        return EPropertyType::DynamicMulticastDelegate;
    }
    else if constexpr (TObjectPointerTraits<TValue>::IsObjectPointer)
    {
        return EPropertyType::Object;
    }
    else if constexpr (std::is_same_v<TValue, int32>)
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
            InMetadata,
            []() constexpr
            {
                if constexpr (TObjectPointerTraits<TValue>::IsObjectPointer)
                {
                    return TObjectPointerTraits<TValue>::IsStrong
                        ? EObjectReferenceKind::Strong
                        : EObjectReferenceKind::Weak;
                }
                else
                {
                    return EObjectReferenceKind::None;
                }
            }(),
            [](const PObject* Object) -> FObjectHandle
            {
                if constexpr (TObjectPointerTraits<TValue>::IsObjectPointer)
                {
                    return (static_cast<const TObject*>(Object)->*Member).GetHandle();
                }
                else
                {
                    (void)Object;
                    return {};
                }
            },
            [](PObject* Object, PObject* ReferencedObject) -> bool
            {
                if constexpr (TObjectPointerTraits<TValue>::IsObjectPointer)
                {
                    using FReferencedObject =
                        typename TObjectPointerTraits<TValue>::ObjectType;
                    static_cast<TObject*>(Object)->*Member =
                        static_cast<FReferencedObject*>(ReferencedObject);
                    return true;
                }
                else
                {
                    (void)Object;
                    (void)ReferencedObject;
                    return false;
                }
            },
            []() -> const PClass*
            {
                if constexpr (TObjectPointerTraits<TValue>::IsObjectPointer)
                {
                    using FReferencedObject =
                        typename TObjectPointerTraits<TValue>::ObjectType;
                    return FReferencedObject::StaticClass();
                }
                else
                {
                    return nullptr;
                }
            },
            [](PObject* Object) -> FDynamicMulticastDelegate*
            {
                if constexpr (TIsDynamicMulticastDelegateValue<TValue>)
                {
                    return &(static_cast<TObject*>(Object)->*Member).GetRuntimeDelegate();
                }
                else
                {
                    (void)Object;
                    return nullptr;
                }
            },
            [](const PObject* Object) -> const FDynamicMulticastDelegate*
            {
                if constexpr (TIsDynamicMulticastDelegateValue<TValue>)
                {
                    return &(static_cast<const TObject*>(Object)->*Member).GetRuntimeDelegate();
                }
                else
                {
                    (void)Object;
                    return nullptr;
                }
            });
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
    EObjectReferenceKind GetObjectReferenceKind() const;
    PObject* GetReferencedObject(const PObject* Object) const;
    FObjectHandle GetReferencedObjectHandle(const PObject* Object) const;
    const PClass* GetReferencedObjectClass() const;
    bool HasReferencedObjectClassResolver() const;
    bool SetReferencedObjectSilently(
        PObject* Object,
        PObject* ReferencedObject) const;
    FDynamicMulticastDelegate* GetDynamicMulticastDelegate(PObject* Object) const;
    const FDynamicMulticastDelegate* GetDynamicMulticastDelegate(const PObject* Object) const;
    bool NotifyPreChange(
        PObject* Object,
        EPropertyChangeType ChangeType = EPropertyChangeType::ValueSet) const;
    bool NotifyPostChange(
        PObject* Object,
        EPropertyChangeType ChangeType = EPropertyChangeType::ValueSet) const;
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
    bool SetValue(
        PObject* Object,
        const TValue& Value,
        EPropertyChangeType ChangeType = EPropertyChangeType::ValueSet) const
    {
        if constexpr (TIsDynamicMulticastDelegateValue<TValue>)
        {
            return false;
        }
        else
        {
            return SetValueAddress(
                Object,
                GetPropertyType<TValue>(),
                sizeof(TValue),
                &Value,
                [](void* Destination, const void* Source)
                {
                    *static_cast<TValue*>(Destination) =
                        *static_cast<const TValue*>(Source);
                },
                ChangeType,
                true);
        }
    }

    template <typename TValue>
    bool SetValueSilently(PObject* Object, const TValue& Value) const
    {
        if constexpr (TIsDynamicMulticastDelegateValue<TValue>)
        {
            return false;
        }
        else
        {
            return SetValueAddress(
                Object,
                GetPropertyType<TValue>(),
                sizeof(TValue),
                &Value,
                [](void* Destination, const void* Source)
                {
                    *static_cast<TValue*>(Destination) =
                        *static_cast<const TValue*>(Source);
                },
                EPropertyChangeType::ValueSet,
                false);
        }
    }

private:
    using FMutableAccessor = void* (*)(PObject*);
    using FConstAccessor = const void* (*)(const PObject*);
    using FReferenceAccessor = FObjectHandle (*)(const PObject*);
    using FReferenceSetter = bool (*)(PObject*, PObject*);
    using FObjectClassResolver = const PClass* (*)();
    using FDynamicMutableAccessor = FDynamicMulticastDelegate* (*)(PObject*);
    using FDynamicConstAccessor = const FDynamicMulticastDelegate* (*)(const PObject*);
    using FValueCopier = void (*)(void*, const void*);

    PProperty(
        FName InName,
        EPropertyType InType,
        std::size_t InSize,
        const void* InOwnerTypeToken,
        FMutableAccessor InMutableAccessor,
        FConstAccessor InConstAccessor,
        FPropertyMetadata InMetadata,
        EObjectReferenceKind InObjectReferenceKind,
        FReferenceAccessor InReferenceAccessor,
        FReferenceSetter InReferenceSetter,
        FObjectClassResolver InObjectClassResolver,
        FDynamicMutableAccessor InDynamicMutableAccessor,
        FDynamicConstAccessor InDynamicConstAccessor);

    bool HasValidAccessors() const;
    const void* GetOwnerTypeToken() const;
    void* GetValueAddress(PObject* Object, EPropertyType ExpectedType, std::size_t ExpectedSize) const;
    const void* GetValueAddress(
        const PObject* Object,
        EPropertyType ExpectedType,
        std::size_t ExpectedSize) const;
    bool SetValueAddress(
        PObject* Object,
        EPropertyType ExpectedType,
        std::size_t ExpectedSize,
        const void* Source,
        FValueCopier Copier,
        EPropertyChangeType ChangeType,
        bool bNotify) const;

    friend class PClass;

    FName Name;
    EPropertyType Type;
    FPropertyMetadata Metadata;
    std::size_t Size = 0;
    const void* OwnerTypeToken = nullptr;
    FMutableAccessor MutableAccessor = nullptr;
    FConstAccessor ConstAccessor = nullptr;
    EObjectReferenceKind ObjectReferenceKind = EObjectReferenceKind::None;
    FReferenceAccessor ReferenceAccessor = nullptr;
    FReferenceSetter ReferenceSetter = nullptr;
    FObjectClassResolver ObjectClassResolver = nullptr;
    FDynamicMutableAccessor DynamicMutableAccessor = nullptr;
    FDynamicConstAccessor DynamicConstAccessor = nullptr;
    const PClass* OwnerClass = nullptr;
};
}
