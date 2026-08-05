#pragma once

#include "Pico/Core/Name.h"

#include <cctype>
#include <string_view>

namespace Pico
{
inline bool IsValidObjectName(std::string_view Name)
{
    if (Name.empty())
    {
        return false;
    }

    const auto IsAlphaOrUnderscore = [](char Character)
    {
        return Character == '_'
            || std::isalpha(static_cast<unsigned char>(Character)) != 0;
    };
    if (!IsAlphaOrUnderscore(Name.front()))
    {
        return false;
    }

    for (char Character : Name)
    {
        if (!IsAlphaOrUnderscore(Character)
            && std::isdigit(static_cast<unsigned char>(Character)) == 0)
        {
            return false;
        }
    }
    return true;
}

inline bool IsValidObjectName(FName Name)
{
    return !Name.IsNone() && IsValidObjectName(Name.ToString());
}
}
