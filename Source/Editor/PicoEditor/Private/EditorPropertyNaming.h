#pragma once

#include "Pico/Object/Property.h"

#include <cctype>
#include <string>

namespace Pico
{
inline std::string MakePropertyFriendlyName(const PProperty& Property)
{
    if (!Property.GetMetadata().DisplayName.empty())
        return Property.GetMetadata().DisplayName;

    std::string Source = Property.GetName().ToString();
    if (Property.GetType() == EPropertyType::Bool
        && Source.size() > 1 && Source[0] == 'b'
        && std::isupper(static_cast<unsigned char>(Source[1])) != 0)
        Source.erase(Source.begin());
    if (Property.GetType() == EPropertyType::Int32
        && Source == "MovementReferenceValue")
        Source = "MovementReference";

    std::string Result;
    Result.reserve(Source.size() + 8);
    for (std::size_t Index = 0; Index < Source.size(); ++Index)
    {
        const unsigned char Current = static_cast<unsigned char>(Source[Index]);
        const unsigned char Previous = Index > 0
            ? static_cast<unsigned char>(Source[Index - 1]) : 0;
        const unsigned char Next = Index + 1 < Source.size()
            ? static_cast<unsigned char>(Source[Index + 1]) : 0;
        const bool bWordBoundary = Index > 0
            && ((std::isupper(Current) != 0
                    && (std::islower(Previous) != 0
                        || (Next != 0 && std::islower(Next) != 0)))
                || (std::isdigit(Current) != 0 && std::isdigit(Previous) == 0)
                || (std::isdigit(Current) == 0 && std::isdigit(Previous) != 0));
        if (bWordBoundary && !Result.empty() && Result.back() != ' ')
            Result.push_back(' ');
        Result.push_back(static_cast<char>(Current));
    }
    return Result;
}

inline std::string MakeReflectedPropertyLabel(const PProperty& Property)
{
    const std::string ReflectedName = Property.GetName().ToString();
    const std::string FriendlyName = MakePropertyFriendlyName(Property);
    return FriendlyName == ReflectedName
        ? ReflectedName : FriendlyName + " [" + ReflectedName + "]";
}
}
