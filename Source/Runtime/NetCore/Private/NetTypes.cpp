#include "Pico/Net/NetTypes.h"

namespace Pico
{
std::string FNetAddress::ToString() const
{
    return Host + ":" + std::to_string(Port);
}

const char* ToString(ENetConnectionState State)
{
    switch (State)
    {
    case ENetConnectionState::Closed: return "Closed";
    case ENetConnectionState::Handshaking: return "Handshaking";
    case ENetConnectionState::Open: return "Open";
    case ENetConnectionState::Closing: return "Closing";
    }
    return "Unknown";
}
}
