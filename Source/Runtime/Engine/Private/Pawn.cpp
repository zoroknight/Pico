#include "Pico/Engine/Pawn.h"

#include "Pico/Engine/Controller.h"

namespace Pico
{
PICO_DEFINE_CLASS(PPawn)

bool PPawn::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::Replicated;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Controller, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PPawn::PPawn(const FObjectConstructionParams& Params)
    : PActor(Params)
{
}

PController* PPawn::GetController() const
{
    return Controller.Get();
}

void PPawn::PossessedBy(PController*)
{
}

void PPawn::UnPossessed()
{
}

void PPawn::BeginDestroy()
{
    if (PController* CurrentController = Controller.Get())
    {
        CurrentController->UnPossess();
    }
    Controller.Reset();
    PActor::BeginDestroy();
}

void PPawn::SetController(PController* InController)
{
    Controller = InController;
}
}
