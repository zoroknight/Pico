#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class FGameplayTagsManager;

class FGameplayTag
{
public:
    FGameplayTag() = default;

    bool IsValid() const;
    std::string_view ToString() const;
    bool MatchesTagExact(const FGameplayTag& Other) const;
    bool MatchesTag(const FGameplayTag& ParentOrExact) const;

    friend bool operator==(const FGameplayTag&, const FGameplayTag&) = default;
    friend bool operator<(const FGameplayTag& Left, const FGameplayTag& Right)
    {
        return Left.Name < Right.Name;
    }

private:
    explicit FGameplayTag(std::string InName);
    friend class FGameplayTagsManager;

    std::string Name;
};

class FGameplayTagContainer
{
public:
    bool AddTag(const FGameplayTag& Tag);
    bool RemoveTag(const FGameplayTag& Tag);
    void AppendTags(const FGameplayTagContainer& Other);
    void Reset();

    bool HasTagExact(const FGameplayTag& Tag) const;
    bool HasTag(const FGameplayTag& ParentOrExact) const;
    bool HasAny(const FGameplayTagContainer& Other) const;
    bool HasAll(const FGameplayTagContainer& Other) const;
    bool IsEmpty() const;
    std::size_t Num() const;
    const std::vector<FGameplayTag>& GetTags() const;

    std::string ExportText() const;
    static bool ImportText(
        std::string_view Text,
        FGameplayTagContainer& OutContainer,
        bool bRegisterMissingTags = false);

private:
    std::vector<FGameplayTag> Tags;
};

class FGameplayTagsManager
{
public:
    static FGameplayTagsManager& Get();

    FGameplayTag RegisterGameplayTag(std::string_view Name);
    FGameplayTag RequestGameplayTag(std::string_view Name) const;
    bool IsRegistered(std::string_view Name) const;
    std::vector<FGameplayTag> GetRegisteredTags() const;

private:
    std::map<std::string, FGameplayTag, std::less<>> RegisteredTags;
};
}
