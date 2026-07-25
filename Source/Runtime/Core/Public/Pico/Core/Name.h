#pragma once

#include "Pico/Core/Types.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Pico
{
class FName
{
public:
    FName() = default;
    explicit FName(std::string_view Name);

    bool IsNone() const;
    uint32 GetComparisonIndex() const;
    std::string ToString() const;

    friend bool operator==(const FName&, const FName&) = default;

private:
    uint32 ComparisonIndex = 0;
};

struct FNameHash
{
    std::size_t operator()(FName Name) const noexcept;
};
}
