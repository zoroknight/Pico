#pragma once

#include "Pico/Core/Types.h"

namespace Pico
{
enum class EMatchState : uint8
{
    EnteringMap,
    WaitingToStart,
    InProgress,
    WaitingPostMatch,
    LeavingMap,
    Aborted
};

constexpr const char* ToString(EMatchState State)
{
    switch (State)
    {
    case EMatchState::EnteringMap: return "EnteringMap";
    case EMatchState::WaitingToStart: return "WaitingToStart";
    case EMatchState::InProgress: return "InProgress";
    case EMatchState::WaitingPostMatch: return "WaitingPostMatch";
    case EMatchState::LeavingMap: return "LeavingMap";
    case EMatchState::Aborted: return "Aborted";
    }
    return "Unknown";
}
}
