#pragma once

namespace Pico
{
class PObjectSystem
{
public:
    static bool Init();
    static void Shutdown();
    static bool IsInitialized();
};
}
