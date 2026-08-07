#pragma once

namespace Pico
{
class FGameEngine;

class FGameInstance
{
public:
    virtual ~FGameInstance() = default;

    virtual bool Init(FGameEngine& GameEngine);
    virtual void Tick(float DeltaSeconds);
    virtual void Shutdown();
};
}
