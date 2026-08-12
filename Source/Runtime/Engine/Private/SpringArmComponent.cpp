#include "Pico/Engine/SpringArmComponent.h"

#include "Pico/Object/Class.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/Controller.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PSpringArmComponent)

bool PSpringArmComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, TargetArmLength);
    PICO_ADD_PROPERTY(Properties, SocketOffset);
    PICO_ADD_PROPERTY(Properties, TargetOffset);
    PICO_ADD_PROPERTY(Properties, bUsePawnControlRotation);
    return Class.AddProperties(std::move(Properties));
}

PSpringArmComponent::PSpringArmComponent(const FObjectConstructionParams& Params)
    : PSceneComponent(Params)
{
    PrimaryComponentTick.SetCanEverTick(true);
    PrimaryComponentTick.SetTickGroup(ETickGroup::PostUpdateWork);
}

float PSpringArmComponent::GetTargetArmLength() const
{
    return TargetArmLength;
}

void PSpringArmComponent::SetTargetArmLength(float InLength)
{
    TargetArmLength = InLength;
    SanitizeParameters();
}

const FVector3& PSpringArmComponent::GetSocketOffset() const
{
    return SocketOffset;
}

void PSpringArmComponent::SetSocketOffset(const FVector3& InOffset)
{
    SocketOffset = InOffset;
    SanitizeParameters();
}

const FVector3& PSpringArmComponent::GetTargetOffset() const
{
    return TargetOffset;
}

void PSpringArmComponent::SetTargetOffset(const FVector3& InOffset)
{
    TargetOffset = InOffset;
    SanitizeParameters();
}
bool PSpringArmComponent::UsesPawnControlRotation() const { return bUsePawnControlRotation; }
void PSpringArmComponent::SetUsePawnControlRotation(bool bValue) { bUsePawnControlRotation = bValue; }

void PSpringArmComponent::TickComponent(float)
{
    if (!bUsePawnControlRotation) return;
    PActor* Owner = GetOwner();
    PPawn* Pawn = Owner != nullptr && Owner->IsA(PPawn::StaticClass())
        ? static_cast<PPawn*>(Owner) : nullptr;
    PController* Controller = Pawn != nullptr ? Pawn->GetController() : nullptr;
    if (Controller != nullptr) SetRelativeRotation(Controller->GetControlRotation());
}

FName PSpringArmComponent::GetEndpointSocketName()
{
    static const FName Name("SpringEndpoint");
    return Name;
}

bool PSpringArmComponent::DoesSocketExist(FName SocketName) const
{
    return SocketName.IsNone() || SocketName == GetEndpointSocketName();
}

FTransform PSpringArmComponent::GetSocketTransform(FName SocketName) const
{
    FTransform SocketTransform = PSceneComponent::GetWorldTransform();
    if (SocketName != GetEndpointSocketName())
    {
        return SocketTransform;
    }
    const FVector3 ArmOffset = SocketTransform.Rotation.RotateVector(
        FVector3::ForwardVector * -TargetArmLength + SocketOffset);
    SocketTransform.Translation += TargetOffset + ArmOffset;
    return SocketTransform;
}

void PSpringArmComponent::PostEditChangeProperty(
    const FPropertyChangedEvent& Event)
{
    PSceneComponent::PostEditChangeProperty(Event);
    SanitizeParameters();
}

void PSpringArmComponent::PostLoad()
{
    PSceneComponent::PostLoad();
    SanitizeParameters();
}

void PSpringArmComponent::SanitizeParameters()
{
    if (!std::isfinite(TargetArmLength)) TargetArmLength = 300.0f;
    TargetArmLength = std::max(TargetArmLength, 0.0f);
    if (!std::isfinite(SocketOffset.X)) SocketOffset.X = 0.0f;
    if (!std::isfinite(SocketOffset.Y)) SocketOffset.Y = 0.0f;
    if (!std::isfinite(SocketOffset.Z)) SocketOffset.Z = 0.0f;
    if (!std::isfinite(TargetOffset.X)) TargetOffset.X = 0.0f;
    if (!std::isfinite(TargetOffset.Y)) TargetOffset.Y = 0.0f;
    if (!std::isfinite(TargetOffset.Z)) TargetOffset.Z = 0.0f;
}
}
