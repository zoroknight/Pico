#pragma once

#include "Pico/Object/ReflectionMacros.h"

#include <vector>

namespace Pico
{
class PActor;
class FWorldAssetLoader;
class PWorld;

class PLevel final : public PObject
{
    PICO_DECLARE_CLASS(PLevel, PObject)

public:
    PWorld* GetWorld() const;
    std::vector<PActor*> GetActors() const;

protected:
    explicit PLevel(const FObjectConstructionParams& Params);

private:
    void AddActor(PActor* Actor);
    bool RemoveActor(PActor* Actor);
    PActor* ResolveActor(FObjectHandle Handle) const;
    bool OwnsActor(const PActor* Actor) const;

    friend class PWorld;
    friend class FWorldAssetLoader;

    std::vector<FObjectHandle> ActorHandles;
};
}
