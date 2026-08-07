#pragma once

#include <memory>

namespace Pico
{
class FGameInstance;

class IGameModule
{
public:
    virtual ~IGameModule() = default;

    virtual bool StartupModule() = 0;
    virtual std::unique_ptr<FGameInstance> CreateGameInstance() = 0;
    virtual void ShutdownModule() = 0;
};
}
