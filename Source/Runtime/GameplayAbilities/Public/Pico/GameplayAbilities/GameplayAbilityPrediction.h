#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/Types.h"

#include <map>
#include <string>

namespace Pico
{
struct FGameplayPredictionKey
{
    uint32 Value = 0;

    bool IsValid() const { return Value != 0; }
    friend bool operator==(const FGameplayPredictionKey&, const FGameplayPredictionKey&) = default;
};

struct FGameplayAbilityPredictionSnapshot
{
    FTransform Transform;
    float Health = 0.0f;
    float Mana = 0.0f;
    std::string OwnedTags;
};

enum class EGameplayPredictionResult : uint8
{
    Pending,
    Confirmed,
    Rejected,
    Unknown
};

struct FGameplayPredictionRecord
{
    FGameplayPredictionKey Key;
    FGameplayAbilityPredictionSnapshot Snapshot;
    EGameplayPredictionResult Result = EGameplayPredictionResult::Pending;
};

class FGameplayPredictionLedger
{
public:
    FGameplayPredictionKey BeginPrediction(
        const FGameplayAbilityPredictionSnapshot& Snapshot);
    EGameplayPredictionResult ResolvePrediction(
        FGameplayPredictionKey Key,
        bool bAccepted,
        FGameplayAbilityPredictionSnapshot* OutRollbackSnapshot = nullptr);
    const FGameplayPredictionRecord* FindPrediction(FGameplayPredictionKey Key) const;
    std::size_t NumPendingPredictions() const;
    void Reset();

private:
    uint32 NextPredictionKey = 1;
    std::map<uint32, FGameplayPredictionRecord> Records;
};
}
