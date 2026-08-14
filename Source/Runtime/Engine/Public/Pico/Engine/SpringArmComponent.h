#pragma once

#include "Pico/Engine/SceneComponent.h"

namespace Pico
{
class PSpringArmComponent final : public PSceneComponent
{
    PICO_DECLARE_CLASS(PSpringArmComponent, PSceneComponent)

public:
    float GetTargetArmLength() const;
    void SetTargetArmLength(float InLength);
    const FVector3& GetSocketOffset() const;
    void SetSocketOffset(const FVector3& InOffset);
    const FVector3& GetTargetOffset() const;
    void SetTargetOffset(const FVector3& InOffset);
    bool UsesPawnControlRotation() const;
    void SetUsePawnControlRotation(bool bValue);
    bool InheritsPitch() const;
    void SetInheritPitch(bool bValue);
    bool InheritsYaw() const;
    void SetInheritYaw(bool bValue);
    bool InheritsRoll() const;
    void SetInheritRoll(bool bValue);
    FRotator GetTargetRotation() const;
    static FName GetEndpointSocketName();
    bool DoesSocketExist(FName SocketName) const override;
    FTransform GetSocketTransform(FName SocketName) const override;
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;

protected:
    explicit PSpringArmComponent(const FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    void SanitizeParameters();

    float TargetArmLength = 300.0f;
    FVector3 SocketOffset = FVector3::ZeroVector;
    FVector3 TargetOffset = FVector3::ZeroVector;
    bool bUsePawnControlRotation = false;
    bool bInheritPitch = true;
    bool bInheritYaw = true;
    bool bInheritRoll = true;
};
}
