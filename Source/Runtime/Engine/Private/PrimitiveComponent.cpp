#include "Pico/Engine/PrimitiveComponent.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Property.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PPrimitiveComponent)

bool PPrimitiveComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, bVisible);
    FPropertyMetadata ColorMetadata;
    ColorMetadata.Description = "Linear RGB tint with each channel in the 0 to 1 range";
    ColorMetadata.Semantic = "Color";
    ColorMetadata.Units = "LinearRGB";
    ColorMetadata.Minimum = 0.0;
    ColorMetadata.Maximum = 1.0;
    PICO_ADD_PROPERTY_METADATA(Properties, Color, ColorMetadata);
    FPropertyMetadata ProfileMetadata;
    ProfileMetadata.DisplayName = "Collision Profile";
    ProfileMetadata.Description =
        "Preset object channel and Ignore, Overlap, or Block responses; use Custom for low-level collision settings";
    ProfileMetadata.Semantic = "CollisionProfile";
    ProfileMetadata.EnumOptions = {
        {static_cast<int32>(ECollisionProfile::Custom), "Custom"},
        {static_cast<int32>(ECollisionProfile::NoCollision), "No Collision"},
        {static_cast<int32>(ECollisionProfile::BlockAll), "Block All"},
        {static_cast<int32>(ECollisionProfile::Pawn), "Pawn"},
        {static_cast<int32>(ECollisionProfile::PawnNoPawnCollision),
            "Pawn (Ignore Pawns)"},
        {static_cast<int32>(ECollisionProfile::CharacterMesh), "Character Mesh"},
        {static_cast<int32>(ECollisionProfile::PhysicsActor), "Physics Actor"},
        {static_cast<int32>(ECollisionProfile::Trigger), "Trigger"},
        {static_cast<int32>(ECollisionProfile::Projectile), "Projectile"}
    };
    PICO_ADD_PROPERTY_METADATA(
        Properties, CollisionProfileValue, ProfileMetadata);
    FPropertyMetadata CollisionMetadata;
    CollisionMetadata.DisplayName = "Collision Enabled";
    CollisionMetadata.EnumOptions = {
        {static_cast<int32>(ECollisionEnabled::NoCollision), "No Collision"},
        {static_cast<int32>(ECollisionEnabled::QueryOnly), "Query Only"},
        {static_cast<int32>(ECollisionEnabled::PhysicsOnly), "Physics Only"},
        {static_cast<int32>(ECollisionEnabled::QueryAndPhysics), "Query And Physics"}
    };
    PICO_ADD_PROPERTY_METADATA(
        Properties, CollisionEnabledValue, CollisionMetadata);
    FPropertyMetadata BodyTypeMetadata;
    BodyTypeMetadata.DisplayName = "Physics Body Type";
    BodyTypeMetadata.EnumOptions = {
        {static_cast<int32>(EPhysicsBodyType::Static), "Static"},
        {static_cast<int32>(EPhysicsBodyType::Kinematic), "Kinematic"},
        {static_cast<int32>(EPhysicsBodyType::Dynamic), "Dynamic"}
    };
    PICO_ADD_PROPERTY_METADATA(
        Properties, PhysicsBodyTypeValue, BodyTypeMetadata);
    PICO_ADD_PROPERTY(Properties, bSimulatePhysics);
    PICO_ADD_PROPERTY(Properties, bSensor);
    PICO_ADD_PROPERTY(Properties, bUseGravity);
    PICO_ADD_PROPERTY(Properties, Mass);
    return Class.AddProperties(std::move(Properties));
}

PPrimitiveComponent::PPrimitiveComponent(const FObjectConstructionParams& Params)
    : PSceneComponent(Params)
{
}

bool PPrimitiveComponent::IsVisible() const
{
    return bVisible;
}

void PPrimitiveComponent::SetVisible(bool bInVisible)
{
    bVisible = bInVisible;
}

const FVector3& PPrimitiveComponent::GetColor() const
{
    return Color;
}

void PPrimitiveComponent::SetColor(const FVector3& InColor)
{
    Color = InColor;
}

ECollisionEnabled PPrimitiveComponent::GetCollisionEnabled() const
{
    return static_cast<ECollisionEnabled>(std::clamp(
        CollisionEnabledValue,
        static_cast<int32>(ECollisionEnabled::NoCollision),
        static_cast<int32>(ECollisionEnabled::QueryAndPhysics)));
}

void PPrimitiveComponent::SetCollisionEnabled(ECollisionEnabled Value)
{
    CollisionProfileValue = static_cast<int32>(ECollisionProfile::Custom);
    CollisionEnabledValue = static_cast<int32>(Value);
    RecreatePhysicsState();
}

ECollisionProfile PPrimitiveComponent::GetCollisionProfile() const
{
    return static_cast<ECollisionProfile>(std::clamp(
        CollisionProfileValue,
        static_cast<int32>(ECollisionProfile::Custom),
        static_cast<int32>(ECollisionProfile::Projectile)));
}

void PPrimitiveComponent::SetCollisionProfile(ECollisionProfile Profile)
{
    CollisionProfileValue = std::clamp(
        static_cast<int32>(Profile),
        static_cast<int32>(ECollisionProfile::Custom),
        static_cast<int32>(ECollisionProfile::Projectile));
    ApplyCollisionProfile();
    RecreatePhysicsState();
}

FCollisionFilterData PPrimitiveComponent::GetCollisionFilterData() const
{
    return MakeCollisionFilter(GetCollisionProfile(), GetPhysicsBodyType());
}

EPhysicsBodyType PPrimitiveComponent::GetPhysicsBodyType() const
{
    return bSimulatePhysics
        ? EPhysicsBodyType::Dynamic
        : static_cast<EPhysicsBodyType>(std::clamp(
            PhysicsBodyTypeValue,
            static_cast<int32>(EPhysicsBodyType::Static),
            static_cast<int32>(EPhysicsBodyType::Dynamic)));
}

void PPrimitiveComponent::SetPhysicsBodyType(EPhysicsBodyType Value)
{
    PhysicsBodyTypeValue = static_cast<int32>(Value);
    bSimulatePhysics = Value == EPhysicsBodyType::Dynamic;
    RecreatePhysicsState();
}

bool PPrimitiveComponent::IsSimulatingPhysics() const { return bSimulatePhysics; }

void PPrimitiveComponent::SetSimulatePhysics(bool bValue)
{
    bSimulatePhysics = bValue;
    if (bValue)
    {
        PhysicsBodyTypeValue = static_cast<int32>(EPhysicsBodyType::Dynamic);
    }
    else if (PhysicsBodyTypeValue == static_cast<int32>(EPhysicsBodyType::Dynamic))
    {
        PhysicsBodyTypeValue = static_cast<int32>(EPhysicsBodyType::Kinematic);
    }
    RecreatePhysicsState();
}

bool PPrimitiveComponent::IsSensor() const { return bSensor; }
void PPrimitiveComponent::SetSensor(bool bValue)
{
    CollisionProfileValue = static_cast<int32>(ECollisionProfile::Custom);
    bSensor = bValue;
    RecreatePhysicsState();
}
bool PPrimitiveComponent::IsGravityEnabled() const { return bUseGravity; }
void PPrimitiveComponent::SetGravityEnabled(bool bValue) { bUseGravity = bValue; RecreatePhysicsState(); }
float PPrimitiveComponent::GetMass() const { return Mass; }

void PPrimitiveComponent::SetMass(float InMass)
{
    Mass = std::isfinite(InMass) && InMass > 0.0f ? InMass : 1.0f;
    RecreatePhysicsState();
}

FPhysicsBodyHandle PPrimitiveComponent::GetPhysicsBodyHandle() const
{
    return PhysicsBodyHandle;
}

bool PPrimitiveComponent::GetPhysicsBodyState(
    FPhysicsBodyState& OutState) const
{
    PWorld* World = GetWorld();
    IPhysicsScene* Scene = World != nullptr ? World->GetPhysicsScene() : nullptr;
    return Scene != nullptr && PhysicsBodyHandle.IsValid()
        && Scene->GetBodyState(PhysicsBodyHandle, OutState);
}

bool PPrimitiveComponent::ApplyReplicatedPhysicsBodyState(
    const FPhysicsBodyState& State)
{
    SetNetworkPhysicsProxy(true);
    PWorld* World = GetWorld();
    IPhysicsScene* Scene = World != nullptr ? World->GetPhysicsScene() : nullptr;
    if (Scene == nullptr || !PhysicsBodyHandle.IsValid()
        || !Scene->SetBodyState(PhysicsBodyHandle, State))
    {
        return false;
    }
    FTransform Transform = State.Transform;
    Transform.Scale = GetWorldTransform().Scale;
    SetWorldTransformFromPhysics(Transform);
    return true;
}

bool PPrimitiveComponent::IsNetworkPhysicsProxy() const
{
    return bNetworkPhysicsProxy;
}

void PPrimitiveComponent::SetNetworkPhysicsProxy(bool bInNetworkPhysicsProxy)
{
    if (bNetworkPhysicsProxy == bInNetworkPhysicsProxy) return;
    bNetworkPhysicsProxy = bInNetworkPhysicsProxy;
    RecreatePhysicsState();
}

bool PPrimitiveComponent::IsPhysicsContactEnabled() const
{
    return bPhysicsContactEnabled;
}

void PPrimitiveComponent::SetPhysicsContactEnabled(bool bEnabled)
{
    if (bPhysicsContactEnabled == bEnabled) return;
    bPhysicsContactEnabled = bEnabled;
    RecreatePhysicsState();
}

bool PPrimitiveComponent::AddImpulse(const FVector3& Impulse)
{
    PWorld* World = GetWorld();
    IPhysicsScene* Scene = World != nullptr ? World->GetPhysicsScene() : nullptr;
    return CheckGameThread("PPrimitiveComponent::AddImpulse")
        && Scene != nullptr
        && PhysicsBodyHandle.IsValid()
        && Scene->AddImpulse(PhysicsBodyHandle, Impulse);
}

FComponentPhysicsEvent& PPrimitiveComponent::OnComponentHit() { return ComponentHitEvent; }
FComponentPhysicsEvent& PPrimitiveComponent::OnComponentBeginOverlap() { return ComponentBeginOverlapEvent; }
FComponentPhysicsEvent& PPrimitiveComponent::OnComponentEndOverlap() { return ComponentEndOverlapEvent; }

void PPrimitiveComponent::OnRegister()
{
    PSceneComponent::OnRegister();
    CreatePhysicsState();
}

void PPrimitiveComponent::OnUnregister()
{
    DestroyPhysicsState();
    PSceneComponent::OnUnregister();
}

void PPrimitiveComponent::OnWorldTransformChanged(ETeleportType Teleport)
{
    PWorld* World = GetWorld();
    IPhysicsScene* Scene = World != nullptr ? World->GetPhysicsScene() : nullptr;
    if (Scene != nullptr && PhysicsBodyHandle.IsValid())
    {
        const FVector3 CurrentScale = GetWorldTransform().Scale;
        if (!CurrentScale.Equals(PhysicsShapeScale))
        {
            RecreatePhysicsState();
            return;
        }
        Scene->SetBodyTransform(PhysicsBodyHandle, GetWorldTransform(), Teleport);
    }
}

void PPrimitiveComponent::RecreatePhysicsState()
{
    if (!IsRegistered()) return;
    DestroyPhysicsState();
    CreatePhysicsState();
}

void PPrimitiveComponent::CreatePhysicsState()
{
    ECollisionEnabled EffectiveCollision = GetCollisionEnabled();
    if (!IsPhysicsContactEnabled())
    {
        EffectiveCollision = HasQueryCollision(EffectiveCollision)
            ? ECollisionEnabled::QueryOnly
            : ECollisionEnabled::NoCollision;
    }
    if (PhysicsBodyHandle.IsValid()
        || EffectiveCollision == ECollisionEnabled::NoCollision)
    {
        return;
    }
    PWorld* World = GetWorld();
    IPhysicsScene* Scene = World != nullptr ? World->GetPhysicsScene() : nullptr;
    if (Scene == nullptr) return;
    FPhysicsBodyDesc Desc;
    Desc.OwnerObject = GetHandle();
    Desc.Shape = GetCollisionShape();
    Desc.Transform = GetWorldTransform();
    Desc.CollisionEnabled = EffectiveCollision;
    Desc.BodyType = GetPhysicsBodyType();
    Desc.Mass = Mass;
    Desc.bUseGravity = bUseGravity;
    Desc.bSensor = bSensor;
    Desc.CollisionFilter = GetCollisionFilterData();
    PhysicsBodyHandle = Scene->CreateBody(Desc);
    if (PhysicsBodyHandle.IsValid())
    {
        PhysicsShapeScale = Desc.Transform.Scale;
    }
}

void PPrimitiveComponent::DestroyPhysicsState()
{
    if (!PhysicsBodyHandle.IsValid()) return;
    PWorld* World = GetWorld();
    if (IPhysicsScene* Scene = World != nullptr ? World->GetPhysicsScene() : nullptr)
    {
        Scene->DestroyBody(PhysicsBodyHandle);
    }
    PhysicsBodyHandle = {};
    PhysicsShapeScale = FVector3::OneVector;
}

void PPrimitiveComponent::SyncComponentFromPhysics()
{
    if (GetPhysicsBodyType() != EPhysicsBodyType::Dynamic || !PhysicsBodyHandle.IsValid()) return;
    PWorld* World = GetWorld();
    IPhysicsScene* Scene = World != nullptr ? World->GetPhysicsScene() : nullptr;
    FPhysicsBodyState State;
    if (Scene != nullptr && Scene->GetBodyState(PhysicsBodyHandle, State))
    {
        State.Transform.Scale = GetWorldTransform().Scale;
        SetWorldTransformFromPhysics(State.Transform);
    }
}

void PPrimitiveComponent::DispatchPhysicsEvent(
    PPrimitiveComponent* Other,
    const FPhysicsContactEvent& Event)
{
    if (Event.bSensor)
    {
        if (Event.Type == EPhysicsContactEventType::Begin)
            ComponentBeginOverlapEvent.Broadcast(Other, Event);
        else if (Event.Type == EPhysicsContactEventType::End)
            ComponentEndOverlapEvent.Broadcast(Other, Event);
        return;
    }
    if (Event.Type == EPhysicsContactEventType::Begin)
    {
        ComponentHitEvent.Broadcast(Other, Event);
    }
}

void PPrimitiveComponent::PostEditChangeProperty(
    const FPropertyChangedEvent& Event)
{
    PSceneComponent::PostEditChangeProperty(Event);
    if (Event.Property == nullptr)
    {
        return;
    }

    const FName PropertyName = Event.Property->GetName();
    if (PropertyName == FName("Color"))
    {
        Color.X = std::clamp(Color.X, 0.0f, 1.0f);
        Color.Y = std::clamp(Color.Y, 0.0f, 1.0f);
        Color.Z = std::clamp(Color.Z, 0.0f, 1.0f);
    }
    else if (PropertyName == FName("CollisionProfileValue"))
    {
        CollisionProfileValue = std::clamp(
            CollisionProfileValue,
            static_cast<int32>(ECollisionProfile::Custom),
            static_cast<int32>(ECollisionProfile::Projectile));
        ApplyCollisionProfile();
    }
    else if (PropertyName == FName("CollisionEnabledValue"))
    {
        CollisionProfileValue = static_cast<int32>(ECollisionProfile::Custom);
    }
    else if (PropertyName == FName("PhysicsBodyTypeValue"))
    {
        PhysicsBodyTypeValue = std::clamp(
            PhysicsBodyTypeValue,
            static_cast<int32>(EPhysicsBodyType::Static),
            static_cast<int32>(EPhysicsBodyType::Dynamic));
        bSimulatePhysics =
            PhysicsBodyTypeValue == static_cast<int32>(EPhysicsBodyType::Dynamic);
    }
    else if (PropertyName == FName("bSimulatePhysics"))
    {
        if (bSimulatePhysics)
        {
            PhysicsBodyTypeValue = static_cast<int32>(EPhysicsBodyType::Dynamic);
        }
        else if (PhysicsBodyTypeValue
            == static_cast<int32>(EPhysicsBodyType::Dynamic))
        {
            PhysicsBodyTypeValue = static_cast<int32>(EPhysicsBodyType::Kinematic);
        }
    }
    else if (PropertyName == FName("bSensor"))
    {
        CollisionProfileValue = static_cast<int32>(ECollisionProfile::Custom);
    }

    CollisionEnabledValue = std::clamp(
        CollisionEnabledValue,
        static_cast<int32>(ECollisionEnabled::NoCollision),
        static_cast<int32>(ECollisionEnabled::QueryAndPhysics));
    Mass = std::isfinite(Mass) && Mass > 0.0f ? Mass : 1.0f;
    if (PropertyName == FName("CollisionProfileValue")
        || PropertyName == FName("CollisionEnabledValue")
        || PropertyName == FName("PhysicsBodyTypeValue")
        || PropertyName == FName("bSimulatePhysics")
        || PropertyName == FName("bSensor")
        || PropertyName == FName("bUseGravity")
        || PropertyName == FName("Mass"))
    {
        RecreatePhysicsState();
    }
}

void PPrimitiveComponent::PostLoad()
{
    PSceneComponent::PostLoad();
    CollisionProfileValue = std::clamp(
        CollisionProfileValue,
        static_cast<int32>(ECollisionProfile::Custom),
        static_cast<int32>(ECollisionProfile::Projectile));
    ApplyCollisionProfile();
    CollisionEnabledValue = std::clamp(
        CollisionEnabledValue,
        static_cast<int32>(ECollisionEnabled::NoCollision),
        static_cast<int32>(ECollisionEnabled::QueryAndPhysics));
    PhysicsBodyTypeValue = std::clamp(
        PhysicsBodyTypeValue,
        static_cast<int32>(EPhysicsBodyType::Static),
        static_cast<int32>(EPhysicsBodyType::Dynamic));
    bSimulatePhysics =
        PhysicsBodyTypeValue == static_cast<int32>(EPhysicsBodyType::Dynamic);
    Mass = std::isfinite(Mass) && Mass > 0.0f ? Mass : 1.0f;
}

void PPrimitiveComponent::ApplyCollisionProfile()
{
    const ECollisionProfile Profile = GetCollisionProfile();
    if (Profile == ECollisionProfile::Custom) return;
    CollisionEnabledValue = static_cast<int32>(
        GetCollisionProfileEnabled(Profile));
    bSensor = IsCollisionProfileSensor(Profile);
}
}
