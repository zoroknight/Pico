#pragma once

#include "Pico/Core/Types.h"

namespace Pico
{
enum class ENetMode : uint8
{
    Standalone,
    Server,
    Client
};

enum class ENetRole : uint8
{
    None,
    SimulatedProxy,
    AutonomousProxy,
    Authority
};

const char* ToString(ENetMode Mode);
const char* ToString(ENetRole Role);
}
