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
    PICO_ADD_PROPERTY(Properties, Color);
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
    CollisionEnabledValue = static_cast<int32>(Value);
    RecreatePhysicsState();
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
void PPrimitiveComponent::SetSensor(bool bValue) { bSensor = bValue; RecreatePhysicsState(); }
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
    if (PhysicsBodyHandle.IsValid()
        || GetCollisionEnabled() == ECollisionEnabled::NoCollision)
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
    Desc.CollisionEnabled = GetCollisionEnabled();
    Desc.BodyType = GetPhysicsBodyType();
    Desc.Mass = Mass;
    Desc.bUseGravity = bUseGravity;
    Desc.bSensor = bSensor;
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
    if (PropertyName == FName("PhysicsBodyTypeValue"))
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

    CollisionEnabledValue = std::clamp(
        CollisionEnabledValue,
        static_cast<int32>(ECollisionEnabled::NoCollision),
        static_cast<int32>(ECollisionEnabled::QueryAndPhysics));
    Mass = std::isfinite(Mass) && Mass > 0.0f ? Mass : 1.0f;
    if (PropertyName == FName("CollisionEnabledValue")
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
}
