#include "Pico/GameplayAbilities/GameplayAbilityPrediction.h"

#include <algorithm>

namespace Pico
{
FGameplayPredictionKey FGameplayPredictionLedger::BeginPrediction(
    const FGameplayAbilityPredictionSnapshot& Snapshot)
{
    if (NextPredictionKey == 0) NextPredictionKey = 1;
    const FGameplayPredictionKey Key {NextPredictionKey++};
    Records[Key.Value] = {Key, Snapshot, EGameplayPredictionResult::Pending};
    return Key;
}

EGameplayPredictionResult FGameplayPredictionLedger::ResolvePrediction(
    FGameplayPredictionKey Key,
    bool bAccepted,
    FGameplayAbilityPredictionSnapshot* OutRollbackSnapshot)
{
    const auto Existing = Records.find(Key.Value);
    if (!Key.IsValid() || Existing == Records.end()
        || Existing->second.Result != EGameplayPredictionResult::Pending)
    {
        return EGameplayPredictionResult::Unknown;
    }
    Existing->second.Result = bAccepted
        ? EGameplayPredictionResult::Confirmed
        : EGameplayPredictionResult::Rejected;
    if (!bAccepted && OutRollbackSnapshot != nullptr)
    {
        *OutRollbackSnapshot = Existing->second.Snapshot;
    }
    return Existing->second.Result;
}

const FGameplayPredictionRecord* FGameplayPredictionLedger::FindPrediction(
    FGameplayPredictionKey Key) const
{
    const auto Existing = Records.find(Key.Value);
    return Existing != Records.end() ? &Existing->second : nullptr;
}

std::size_t FGameplayPredictionLedger::NumPendingPredictions() const
{
    return static_cast<std::size_t>(std::count_if(
        Records.begin(), Records.end(),
        [](const auto& Pair)
        {
            return Pair.second.Result == EGameplayPredictionResult::Pending;
        }));
}

void FGameplayPredictionLedger::Reset()
{
    Records.clear();
    NextPredictionKey = 1;
}
}
