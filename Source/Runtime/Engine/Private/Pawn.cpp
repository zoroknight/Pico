#include "Pico/Engine/Pawn.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Engine/Controller.h"
#include "Pico/Engine/PawnMovementComponent.h"

#include <cmath>

namespace Pico
{
PICO_DEFINE_CLASS(PPawn)

bool PPawn::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::Replicated;
    Metadata.ReplicationCondition = EReplicationCondition::OwnerOnly;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Controller, Metadata);
    FPropertyMetadata AutoPossessMetadata;
    AutoPossessMetadata.DisplayName = "Auto Possess Player";
    AutoPossessMetadata.EnumOptions = {
        {-1, "Disabled"},
        {0, "Player 0"}
    };
    PICO_ADD_PROPERTY_METADATA(
        Properties, AutoPossessPlayerIndex, AutoPossessMetadata);
    PICO_ADD_PROPERTY(Properties, bUseControllerRotationYaw);
    return Class.AddProperties(std::move(Properties));
}

int32 PPawn::GetAutoPossessPlayerIndex() const { return AutoPossessPlayerIndex; }
void PPawn::SetAutoPossessPlayerIndex(int32 PlayerIndex)
{ AutoPossessPlayerIndex = PlayerIndex == 0 ? 0 : -1; }
bool PPawn::UsesControllerRotationYaw() const { return bUseControllerRotationYaw; }
void PPawn::SetUseControllerRotationYaw(bool bValue) { bUseControllerRotationYaw = bValue; }

PPawn::PPawn(const FObjectConstructionParams& Params)
    : PActor(Params)
{
}

PController* PPawn::GetController() const
{
    return Controller.Get();
}

PPawnMovementComponent* PPawn::GetMovementComponent() const
{
    for (PActorComponent* Component : GetComponents())
    {
        if (Component != nullptr
            && Component->IsA(PPawnMovementComponent::StaticClass()))
        {
            return static_cast<PPawnMovementComponent*>(Component);
        }
    }
    return nullptr;
}

void PPawn::AddMovementInput(
    const FVector3& WorldDirection,
    float ScaleValue,
    bool bForce)
{
    (void)bForce;
    if (!CheckGameThread("PPawn::AddMovementInput")
        || !std::isfinite(ScaleValue)
        || !std::isfinite(WorldDirection.X)
        || !std::isfinite(WorldDirection.Y)
        || !std::isfinite(WorldDirection.Z)
        || WorldDirection.IsNearlyZero()
        || ScaleValue == 0.0f)
    {
        return;
    }
    PendingMovementInputVector += WorldDirection.GetSafeNormal() * ScaleValue;
    const float Size = PendingMovementInputVector.Size();
    if (Size > 1.0f)
    {
        PendingMovementInputVector /= Size;
    }
}

const FVector3& PPawn::GetPendingMovementInputVector() const
{
    return PendingMovementInputVector;
}

const FVector3& PPawn::GetLastMovementInputVector() const
{
    return LastMovementInputVector;
}

FVector3 PPawn::ConsumeMovementInputVector()
{
    if (!CheckGameThread("PPawn::ConsumeMovementInputVector"))
    {
        return FVector3::ZeroVector;
    }
    LastMovementInputVector = PendingMovementInputVector;
    PendingMovementInputVector = FVector3::ZeroVector;
    return LastMovementInputVector;
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
    PendingMovementInputVector = FVector3::ZeroVector;
    LastMovementInputVector = FVector3::ZeroVector;
    PActor::BeginDestroy();
}

void PPawn::SetController(PController* InController)
{
    if (Controller.Get() == InController) return;
    Controller = InController;
    MarkReplicatedPropertyDirty(FName("Controller"));
}

void PPawn::RefreshMovementTickPrerequisites()
{
    if (PPawnMovementComponent* Movement = GetMovementComponent())
    {
        Movement->RefreshControllerTickPrerequisite();
    }
}
}
