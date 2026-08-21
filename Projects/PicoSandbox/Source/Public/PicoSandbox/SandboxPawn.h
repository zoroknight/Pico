#pragma once

#include "Pico/Asset/ThirdPersonControlProfile.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/Character.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPawn.generated.h"

namespace PicoSandbox
{
using EMovementReference = Pico::EThirdPersonMovementReference;

const char* ToString(EMovementReference Reference);

PCLASS()
class PSandboxPawn final : public Pico::PCharacter
{
    GENERATED_BODY()

public:
    EMovementReference GetMovementReference() const;
    void SetMovementReference(EMovementReference Reference);
    const Pico::FAssetPath& GetThirdPersonControlProfileAsset() const;
    void SetThirdPersonControlProfileAsset(const Pico::FAssetPath& AssetPath);
    bool LoadAndApplyThirdPersonControlProfile();
    const Pico::FThirdPersonControlProfileData& GetActiveControlProfile() const;
    Pico::uint64 GetActiveControlProfileHash() const;
    bool HasLoadedControlProfile() const;
protected:
    explicit PSandboxPawn(const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;
    void PostLoad() override;

private:
    PPROPERTY()
    Pico::int32 MovementReferenceValue =
        static_cast<Pico::int32>(EMovementReference::ControlRotation);

    PPROPERTY(Asset=ThirdPersonControlProfile)
    Pico::FAssetPath ThirdPersonControlProfileAsset;

    Pico::FThirdPersonControlProfileData ActiveControlProfile;
    Pico::uint64 ActiveControlProfileHash = 0;
    bool bLoadedControlProfile = false;
};
}
