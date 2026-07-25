#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Core/Types.h"

#include <cstddef>
#include <type_traits>

namespace Pico
{
class PClass;
class PObject;

enum class EPropertyType : uint8
{
    Int32,
    Float,
    Bool
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
    }

    return 0;
}

template <typename TValue>
inline constexpr bool TIsSupportedPropertyType =
    std::is_same_v<TValue, int32>
    || std::is_same_v<TValue, float>
    || std::is_same_v<TValue, bool>;

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
    else
    {
        return EPropertyType::Bool;
    }
}

class PProperty
{
public:
    PProperty(FName InName, EPropertyType InType, std::size_t InOffset, std::size_t InSize);

    FName GetName() const;
    EPropertyType GetType() const;
    std::size_t GetOffset() const;
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
    void* GetValueAddress(PObject* Object, EPropertyType ExpectedType, std::size_t ExpectedSize) const;
    const void* GetValueAddress(
        const PObject* Object,
        EPropertyType ExpectedType,
        std::size_t ExpectedSize) const;

    friend class PClass;

    FName Name;
    EPropertyType Type;
    std::size_t Offset = 0;
    std::size_t Size = 0;
    const PClass* OwnerClass = nullptr;
};
}
