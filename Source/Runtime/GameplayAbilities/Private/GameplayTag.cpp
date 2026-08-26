#include "Pico/GameplayAbilities/GameplayTag.h"

#include <algorithm>
#include <cctype>

namespace Pico
{
namespace
{
bool IsValidTagName(std::string_view Name)
{
    if (Name.empty() || Name.front() == '.' || Name.back() == '.')
    {
        return false;
    }

    bool bPreviousWasDot = false;
    for (const unsigned char Character : Name)
    {
        const bool bIsDot = Character == '.';
        if (bIsDot && bPreviousWasDot)
        {
            return false;
        }
        if (!bIsDot && Character != '_' && Character != '-'
            && std::isalnum(Character) == 0)
        {
            return false;
        }
        bPreviousWasDot = bIsDot;
    }
    return true;
}

std::string_view Trim(std::string_view Value)
{
    while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.front())) != 0)
    {
        Value.remove_prefix(1);
    }
    while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.back())) != 0)
    {
        Value.remove_suffix(1);
    }
    return Value;
}
}

FGameplayTag::FGameplayTag(std::string InName)
    : Name(std::move(InName))
{
}

bool FGameplayTag::IsValid() const { return !Name.empty(); }
std::string_view FGameplayTag::ToString() const { return Name; }

bool FGameplayTag::MatchesTagExact(const FGameplayTag& Other) const
{
    return IsValid() && *this == Other;
}

bool FGameplayTag::MatchesTag(const FGameplayTag& ParentOrExact) const
{
    if (!IsValid() || !ParentOrExact.IsValid())
    {
        return false;
    }
    const std::string_view Parent = ParentOrExact.ToString();
    return Name == Parent
        || (Name.size() > Parent.size()
            && Name.compare(0, Parent.size(), Parent) == 0
            && Name[Parent.size()] == '.');
}

bool FGameplayTagContainer::AddTag(const FGameplayTag& Tag)
{
    if (!Tag.IsValid())
    {
        return false;
    }
    const auto Position = std::lower_bound(Tags.begin(), Tags.end(), Tag);
    if (Position != Tags.end() && *Position == Tag)
    {
        return false;
    }
    Tags.insert(Position, Tag);
    return true;
}

bool FGameplayTagContainer::RemoveTag(const FGameplayTag& Tag)
{
    const auto Position = std::lower_bound(Tags.begin(), Tags.end(), Tag);
    if (Position == Tags.end() || *Position != Tag)
    {
        return false;
    }
    Tags.erase(Position);
    return true;
}

void FGameplayTagContainer::AppendTags(const FGameplayTagContainer& Other)
{
    for (const FGameplayTag& Tag : Other.Tags)
    {
        AddTag(Tag);
    }
}

void FGameplayTagContainer::Reset() { Tags.clear(); }

bool FGameplayTagContainer::HasTagExact(const FGameplayTag& Tag) const
{
    return std::binary_search(Tags.begin(), Tags.end(), Tag);
}

bool FGameplayTagContainer::HasTag(const FGameplayTag& ParentOrExact) const
{
    return std::any_of(
        Tags.begin(), Tags.end(),
        [&ParentOrExact](const FGameplayTag& Candidate)
        {
            return Candidate.MatchesTag(ParentOrExact);
        });
}

bool FGameplayTagContainer::HasAny(const FGameplayTagContainer& Other) const
{
    return std::any_of(
        Other.Tags.begin(), Other.Tags.end(),
        [this](const FGameplayTag& Tag) { return HasTag(Tag); });
}

bool FGameplayTagContainer::HasAll(const FGameplayTagContainer& Other) const
{
    return std::all_of(
        Other.Tags.begin(), Other.Tags.end(),
        [this](const FGameplayTag& Tag) { return HasTag(Tag); });
}

bool FGameplayTagContainer::IsEmpty() const { return Tags.empty(); }
std::size_t FGameplayTagContainer::Num() const { return Tags.size(); }
const std::vector<FGameplayTag>& FGameplayTagContainer::GetTags() const { return Tags; }

std::string FGameplayTagContainer::ExportText() const
{
    std::string Result;
    for (std::size_t Index = 0; Index < Tags.size(); ++Index)
    {
        if (Index > 0)
        {
            Result.push_back(',');
        }
        Result.append(Tags[Index].ToString());
    }
    return Result;
}

bool FGameplayTagContainer::ImportText(
    std::string_view Text,
    FGameplayTagContainer& OutContainer,
    bool bRegisterMissingTags)
{
    FGameplayTagContainer Parsed;
    std::size_t Start = 0;
    while (Start <= Text.size())
    {
        const std::size_t Separator = Text.find(',', Start);
        const std::string_view Token = Trim(Text.substr(
            Start,
            Separator == std::string_view::npos ? Text.size() - Start : Separator - Start));
        if (!Token.empty())
        {
            FGameplayTag Tag = bRegisterMissingTags
                ? FGameplayTagsManager::Get().RegisterGameplayTag(Token)
                : FGameplayTagsManager::Get().RequestGameplayTag(Token);
            if (!Tag.IsValid())
            {
                return false;
            }
            Parsed.AddTag(Tag);
        }
        if (Separator == std::string_view::npos)
        {
            break;
        }
        Start = Separator + 1;
    }
    OutContainer = std::move(Parsed);
    return true;
}

FGameplayTagsManager& FGameplayTagsManager::Get()
{
    static FGameplayTagsManager Manager;
    return Manager;
}

FGameplayTag FGameplayTagsManager::RegisterGameplayTag(std::string_view Name)
{
    if (!IsValidTagName(Name))
    {
        return {};
    }

    std::size_t End = Name.find('.');
    while (End != std::string_view::npos)
    {
        const std::string Parent(Name.substr(0, End));
        RegisteredTags.try_emplace(Parent, FGameplayTag(Parent));
        End = Name.find('.', End + 1);
    }

    const std::string CanonicalName(Name);
    return RegisteredTags.try_emplace(
        CanonicalName,
        FGameplayTag(CanonicalName)).first->second;
}

FGameplayTag FGameplayTagsManager::RequestGameplayTag(std::string_view Name) const
{
    const auto Existing = RegisteredTags.find(Name);
    return Existing != RegisteredTags.end() ? Existing->second : FGameplayTag {};
}

bool FGameplayTagsManager::IsRegistered(std::string_view Name) const
{
    return RegisteredTags.contains(Name);
}

std::vector<FGameplayTag> FGameplayTagsManager::GetRegisteredTags() const
{
    std::vector<FGameplayTag> Result;
    Result.reserve(RegisteredTags.size());
    for (const auto& [Name, Tag] : RegisteredTags)
    {
        (void)Name;
        Result.push_back(Tag);
    }
    return Result;
}
}
