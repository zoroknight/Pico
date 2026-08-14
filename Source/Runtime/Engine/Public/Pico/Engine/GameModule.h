#pragma once

namespace Pico
{
class PClass;

class IGameModule
{
public:
    virtual ~IGameModule() = default;

    virtual bool StartupModule() = 0;
    virtual bool PostActorBlueprintCompile() { return true; }
    virtual const PClass* GetGameInstanceClass() const = 0;
    virtual const PClass* GetGameModeClass() const { return nullptr; }
    virtual void ShutdownModule() = 0;
};
}
