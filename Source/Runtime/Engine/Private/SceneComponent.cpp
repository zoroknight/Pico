#include "Pico/Engine/SceneComponent.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/PhysicsCore/WorldCollisionQuery.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PSceneComponent)

bool PSceneComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, RelativeTransform);
    return Class.AddProperties(std::move(Properties));
}

PSceneComponent::PSceneComponent(const FObjectConstructionParams& Params)
    : PActorComponent(Params)
{
}

PSceneComponent* PSceneComponent::GetAttachParent() const
{
    return ResolveSceneComponent(AttachParentHandle);
}

FName PSceneComponent::GetAttachSocketName() const
{
    return AttachSocketName;
}

std::vector<PSceneComponent*> PSceneComponent::GetAttachChildren() const
{
    std::vector<PSceneComponent*> Children;
    Children.reserve(AttachChildrenHandles.size());
    for (const FObjectHandle Handle : AttachChildrenHandles)
    {
        if (PSceneComponent* Child = ResolveSceneComponent(Handle))
        {
            Children.push_back(Child);
        }
    }
    return Children;
}

bool PSceneComponent::IsAttachedTo(const PSceneComponent* Component) const
{
    if (Component == nullptr)
    {
        return false;
    }

    const PSceneComponent* Parent = GetAttachParent();
    while (Parent != nullptr)
    {
        if (Parent == Component)
        {
            return true;
        }
        Parent = Parent->GetAttachParent();
    }
    return false;
}

bool PSceneComponent::AttachToComponent(
    PSceneComponent* Parent,
    EAttachmentTransformRule Rule,
    FName SocketName)
{
    PActor* Owner = GetOwner();
    if (Parent == nullptr
        || Parent == this
        || Owner == nullptr
        || Parent->GetOwner() != Owner
        || IsBeginningDestroy()
        || Parent->IsBeginningDestroy()
        || Owner->GetRootComponent() == this
        || Parent->IsAttachedTo(this)
        || !Parent->DoesSocketExist(SocketName))
    {
        return false;
    }

    if (GetAttachParent() == Parent && AttachSocketName == SocketName)
    {
        return true;
    }

    const FTransform WorldTransform = GetWorldTransform();
    if (PSceneComponent* PreviousParent = GetAttachParent())
    {
        PreviousParent->RemoveAttachChild(GetHandle());
    }

    AttachParentHandle = Parent->GetHandle();
    AttachSocketName = SocketName;
    Parent->AddAttachChild(GetHandle());
    if (Rule == EAttachmentTransformRule::KeepWorld)
    {
        RelativeTransform = WorldTransform.GetRelativeTransform(
            Parent->GetSocketTransform(SocketName));
    }
    return true;
}

bool PSceneComponent::DetachFromComponent(EAttachmentTransformRule Rule)
{
    PSceneComponent* Parent = GetAttachParent();
    if (Parent == nullptr)
    {
        AttachParentHandle = {};
        AttachSocketName = {};
        return false;
    }

    const FTransform WorldTransform = GetWorldTransform();
    Parent->RemoveAttachChild(GetHandle());
    AttachParentHandle = {};
    AttachSocketName = {};
    if (Rule == EAttachmentTransformRule::KeepWorld)
    {
        RelativeTransform = WorldTransform;
    }
    return true;
}

const FTransform& PSceneComponent::GetRelativeTransform() const
{
    return RelativeTransform;
}

void PSceneComponent::SetRelativeTransform(const FTransform& Transform)
{
    RelativeTransform = Transform;
    NotifyTransformChangedRecursive(ETeleportType::TeleportPhysics);
}

const FVector3& PSceneComponent::GetRelativeLocation() const
{
    return RelativeTransform.Translation;
}

void PSceneComponent::SetRelativeLocation(const FVector3& Location)
{
    RelativeTransform.Translation = Location;
    NotifyTransformChangedRecursive(ETeleportType::TeleportPhysics);
}

FRotator PSceneComponent::GetRelativeRotation() const
{
    return RelativeTransform.Rotation.Rotator();
}

void PSceneComponent::SetRelativeRotation(const FRotator& Rotation)
{
    RelativeTransform.Rotation = Rotation.Quaternion();
    NotifyTransformChangedRecursive(ETeleportType::TeleportPhysics);
}

const FVector3& PSceneComponent::GetRelativeScale() const
{
    return RelativeTransform.Scale;
}

void PSceneComponent::SetRelativeScale(const FVector3& Scale)
{
    RelativeTransform.Scale = Scale;
    NotifyTransformChangedRecursive(ETeleportType::TeleportPhysics);
}

FTransform PSceneComponent::GetWorldTransform() const
{
    const PSceneComponent* Parent = GetAttachParent();
    return Parent != nullptr
        ? RelativeTransform * Parent->GetSocketTransform(AttachSocketName)
        : RelativeTransform;
}

bool PSceneComponent::DoesSocketExist(FName SocketName) const
{
    return SocketName.IsNone();
}

FTransform PSceneComponent::GetSocketTransform(FName SocketName) const
{
    (void)SocketName;
    return GetWorldTransform();
}

void PSceneComponent::SetWorldTransform(const FTransform& Transform)
{
    SetWorldTransformInternal(Transform, ETeleportType::TeleportPhysics);
}

void PSceneComponent::SetWorldTransformInternal(
    const FTransform& Transform,
    ETeleportType Teleport)
{
    const PSceneComponent* Parent = GetAttachParent();
    RelativeTransform = Parent != nullptr
        ? Transform.GetRelativeTransform(Parent->GetSocketTransform(AttachSocketName))
        : Transform;
    NotifyTransformChangedRecursive(Teleport);
}

void PSceneComponent::SetWorldTransformFromPhysics(const FTransform& Transform)
{
    bApplyingPhysicsTransform = true;
    SetWorldTransformInternal(Transform, ETeleportType::None);
    bApplyingPhysicsTransform = false;
}

void PSceneComponent::OnWorldTransformChanged(ETeleportType)
{
}

void PSceneComponent::NotifyTransformChangedRecursive(ETeleportType Teleport)
{
    if (!bApplyingPhysicsTransform)
    {
        OnWorldTransformChanged(Teleport);
    }
    for (PSceneComponent* Child : GetAttachChildren())
    {
        if (Child != nullptr)
        {
            Child->NotifyTransformChangedRecursive(Teleport);
        }
    }
}

FCollisionShape PSceneComponent::GetCollisionShape() const
{
    return FCollisionShape::MakePoint();
}

bool PSceneComponent::MoveComponent(
    const FVector3& Delta,
    const FQuat& NewRotation,
    bool bSweep,
    FHitResult* OutHit,
    EMoveComponentFlags,
    ETeleportType Teleport)
{
    if (!CheckGameThread("PSceneComponent::MoveComponent"))
    {
        return false;
    }
    const FTransform StartTransform = GetWorldTransform();
    const FVector3 Start = StartTransform.Translation;
    const FVector3 End = Start + Delta;
    FHitResult Hit;
    Hit.Reset(Start, End);

    const bool bShouldSweep = bSweep && Teleport == ETeleportType::None
        && !Delta.IsNearlyZero();
    if (bShouldSweep)
    {
        PWorld* World = GetWorld();
        IWorldCollisionQuery* CollisionQuery =
            World != nullptr ? World->GetCollisionQuery() : nullptr;
        if (CollisionQuery != nullptr)
        {
            FCollisionQueryParams QueryParams;
            QueryParams.MovingObject = GetHandle();
            CollisionQuery->Sweep(
                GetCollisionShape(),
                Start,
                End,
                NewRotation,
                QueryParams,
                Hit);
        }
    }

    Hit.Time = std::clamp(Hit.Time, 0.0f, 1.0f);
    if (!Hit.bBlockingHit)
    {
        Hit.Time = 1.0f;
    }
    Hit.Location = Start + Delta * Hit.Time;
    Hit.Distance = (Hit.Location - Start).Size();
    FTransform TargetTransform = StartTransform;
    TargetTransform.Translation = Hit.Location;
    TargetTransform.Rotation = NewRotation.GetNormalized();
    SetWorldTransformInternal(TargetTransform, Teleport);

    if (OutHit != nullptr)
    {
        *OutHit = Hit;
    }
    return !Hit.bStartPenetrating || Hit.Time > 0.0f;
}

void PSceneComponent::BeginDestroy()
{
    const FTransform WorldTransform = GetWorldTransform();
    if (PSceneComponent* Parent = GetAttachParent())
    {
        Parent->RemoveAttachChild(GetHandle());
    }
    AttachParentHandle = {};
    AttachSocketName = {};
    RelativeTransform = WorldTransform;

    const std::vector<PSceneComponent*> Children = GetAttachChildren();
    for (PSceneComponent* Child : Children)
    {
        if (Child == nullptr || Child->IsBeginningDestroy())
        {
            continue;
        }

        const FTransform ChildWorldTransform = Child->GetWorldTransform();
        Child->AttachParentHandle = {};
        Child->AttachSocketName = {};
        Child->RelativeTransform = ChildWorldTransform;
    }
    AttachChildrenHandles.clear();
    PActorComponent::BeginDestroy();
}

PSceneComponent* PSceneComponent::ResolveSceneComponent(FObjectHandle Handle) const
{
    PObject* Object = ResolveObject(Handle);
    return Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object)
        : nullptr;
}

void PSceneComponent::AddAttachChild(FObjectHandle Handle)
{
    if (std::find(AttachChildrenHandles.begin(), AttachChildrenHandles.end(), Handle)
        == AttachChildrenHandles.end())
    {
        AttachChildrenHandles.push_back(Handle);
    }
}

void PSceneComponent::RemoveAttachChild(FObjectHandle Handle)
{
    std::erase(AttachChildrenHandles, Handle);
}
}
