#include "Pico/Core/Name.h"

#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Pico
{
namespace
{
class FNamePool
{
public:
    FNamePool()
    {
        Entries.emplace_back("None");
        NameToIndex.emplace(Entries.front(), 0);
    }

    uint32 FindOrAdd(std::string_view Name)
    {
        if (Name.empty() || Name == "None")
        {
            return 0;
        }

        const std::scoped_lock Lock(Mutex);
        const auto Existing = NameToIndex.find(std::string(Name));
        if (Existing != NameToIndex.end())
        {
            return Existing->second;
        }

        const uint32 NewIndex = static_cast<uint32>(Entries.size());
        Entries.emplace_back(Name);
        NameToIndex.emplace(Entries.back(), NewIndex);
        return NewIndex;
    }

    std::string Resolve(uint32 Index) const
    {
        const std::scoped_lock Lock(Mutex);
        return Index < Entries.size() ? Entries[Index] : Entries.front();
    }

private:
    mutable std::mutex Mutex;
    std::vector<std::string> Entries;
    std::unordered_map<std::string, uint32> NameToIndex;
};

FNamePool& GetNamePool()
{
    static FNamePool NamePool;
    return NamePool;
}
}

FName::FName(std::string_view Name)
    : ComparisonIndex(GetNamePool().FindOrAdd(Name))
{
}

bool FName::IsNone() const
{
    return ComparisonIndex == 0;
}

uint32 FName::GetComparisonIndex() const
{
    return ComparisonIndex;
}

std::string FName::ToString() const
{
    return GetNamePool().Resolve(ComparisonIndex);
}

std::size_t FNameHash::operator()(FName Name) const noexcept
{
    return std::hash<uint32> {}(Name.GetComparisonIndex());
}
}
