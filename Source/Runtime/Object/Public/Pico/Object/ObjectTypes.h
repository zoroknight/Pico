#pragma once

#include "Pico/Core/Types.h"

#include <limits>
#include <memory>

namespace Pico
{
class PObject;
PObject* ResolveObject(struct FObjectHandle Handle);

struct FObjectDeleter
{
    void operator()(PObject* Object) const;
};

using FObjectPtr = std::unique_ptr<PObject, FObjectDeleter>;

enum class EObjectFlags : uint32
{
    None = 0,
    Transient = 1 << 0,
    ClassDefaultObject = 1 << 1,
    DefaultSubobject = 1 << 2,
    RootSet = 1 << 3
};

constexpr EObjectFlags operator|(EObjectFlags Left, EObjectFlags Right)
{
    return static_cast<EObjectFlags>(static_cast<uint32>(Left) | static_cast<uint32>(Right));
}

constexpr bool HasAnyFlags(EObjectFlags Value, EObjectFlags Flags)
{
    return (static_cast<uint32>(Value) & static_cast<uint32>(Flags)) != 0;
}

struct FObjectHandle
{
    static constexpr uint32 InvalidIndex = std::numeric_limits<uint32>::max();

    bool IsValid() const
    {
        return Index != InvalidIndex && Serial != 0;
    }

    friend bool operator==(const FObjectHandle&, const FObjectHandle&) = default;

    uint32 Index = InvalidIndex;
    uint32 Serial = 0;
};
}
