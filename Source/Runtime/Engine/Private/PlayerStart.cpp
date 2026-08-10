#include "Pico/Engine/PlayerStart.h"

#include "Pico/Engine/SceneComponent.h"
#include "Pico/Object/ObjectInitializer.h"

namespace Pico
{
PICO_DEFINE_CLASS(PPlayerStart)

bool PPlayerStart::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, PlayerStartId);
    return Class.AddProperties(std::move(Properties));
}

PPlayerStart::PPlayerStart(const FObjectConstructionParams& Params)
    : PActor(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
}

bool PPlayerStart::DefineDefaultSubobjects(FObjectInitializer& Initializer)
{
    PSceneComponent* Root =
        Initializer.CreateDefaultSubobject<PSceneComponent>("DefaultSceneRoot");
    return Root != nullptr && Initializer.SetRootSubobject(Root);
}

int32 PPlayerStart::GetPlayerStartId() const { return PlayerStartId; }
void PPlayerStart::SetPlayerStartId(int32 InId) { PlayerStartId = InId; }
}
