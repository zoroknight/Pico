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
    PICO_ADD_PROPERTY(Properties, bInheritPitch);
    PICO_ADD_PROPERTY(Properties, bInheritYaw);
    PICO_ADD_PROPERTY(Properties, bInheritRoll);
    return Class.AddProperties(std::move(Properties));
}

PSpringArmComponent::PSpringArmComponent(const FObjectConstructionParams& Params)
    : PSceneComponent(Params)
{
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
bool PSpringArmComponent::InheritsPitch() const { return bInheritPitch; }
void PSpringArmComponent::SetInheritPitch(bool bValue) { bInheritPitch = bValue; }
bool PSpringArmComponent::InheritsYaw() const { return bInheritYaw; }
void PSpringArmComponent::SetInheritYaw(bool bValue) { bInheritYaw = bValue; }
bool PSpringArmComponent::InheritsRoll() const { return bInheritRoll; }
void PSpringArmComponent::SetInheritRoll(bool bValue) { bInheritRoll = bValue; }

FRotator PSpringArmComponent::GetTargetRotation() const
{
    FRotator DesiredRotation = GetWorldTransform().Rotation.Rotator();
    const PActor* Owner = GetOwner();
    const PPawn* Pawn = Owner != nullptr && Owner->IsA(PPawn::StaticClass())
        ? static_cast<const PPawn*>(Owner) : nullptr;
    const PController* Controller = Pawn != nullptr ? Pawn->GetController() : nullptr;
    if (bUsePawnControlRotation && Controller != nullptr)
    {
        DesiredRotation = Controller->GetControlRotation();
    }

    const FRotator RelativeRotation = GetRelativeRotation();
    if (!bInheritPitch) DesiredRotation.Pitch = RelativeRotation.Pitch;
    if (!bInheritYaw) DesiredRotation.Yaw = RelativeRotation.Yaw;
    if (!bInheritRoll) DesiredRotation.Roll = RelativeRotation.Roll;
    return DesiredRotation.GetNormalized();
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
    SocketTransform.Rotation = GetTargetRotation().Quaternion();
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
