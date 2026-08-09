#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ReflectionMacros.h"

#include <string_view>
#include <type_traits>
#include <vector>

namespace Pico
{
class PActor;
class PLevel;
class PSceneComponent;
class PWorld;
class FWorldAssetLoader;
class FReferenceCollector;

using FOnActorDestroyed = TObjectMulticastDelegate<void(PActor*)>;

class PActor : public PObject
{
    PICO_DECLARE_CLASS(PActor, PObject)

public:
    PWorld* GetWorld() const;
    PLevel* GetLevel() const;
    PActor* GetOwner() const;
    bool HasBegunPlay() const;
    bool IsPendingDestroy() const;
    bool Destroy();
    FOnActorDestroyed& OnDestroyed();

    PActorComponent* CreateComponent(const PClass* ComponentClass, FName Name);
    PActorComponent* CreateComponent(const PClass* ComponentClass, std::string_view Name);
    bool DestroyComponent(PActorComponent* Component);

    template <typename TComponent>
    TComponent* CreateComponent(FName Name)
    {
        static_assert(
            std::is_base_of_v<PActorComponent, TComponent>,
            "CreateComponent only constructs PActorComponent-derived types");
        return static_cast<TComponent*>(CreateComponent(TComponent::StaticClass(), Name));
    }

    template <typename TComponent>
    TComponent* CreateComponent(std::string_view Name)
    {
        return CreateComponent<TComponent>(FName(Name));
    }

    std::vector<PActorComponent*> GetComponents() const;
    PSceneComponent* GetRootComponent() const;
    bool SetRootComponent(PSceneComponent* Component);

    FTransform GetActorTransform() const;
    bool SetActorTransform(const FTransform& Transform);
    FVector3 GetActorLocation() const;
    bool SetActorLocation(const FVector3& Location);
    FRotator GetActorRotation() const;
    bool SetActorRotation(const FRotator& Rotation);
    FVector3 GetActorScale() const;
    bool SetActorScale(const FVector3& Scale);

    virtual void BeginPlay();
    virtual void Tick(float DeltaSeconds);
    virtual void EndPlay();

protected:
    explicit PActor(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    void AddReferencedObjects(FReferenceCollector& Collector) const override;
    bool OnDefaultSubobjectCreated(PObject* Subobject) override;
    bool OnDefaultSubobjectRelation(
        PObject* Subobject,
        PObject* AttachParent,
        FName AttachSocketName,
        bool bIsRoot) override;

private:
    void DispatchBeginPlay();
    void DispatchTick(float DeltaSeconds);
    void DispatchEndPlay();
    void DispatchDestroyed();
    void MarkPendingDestroy();
    void SetOwner(PActor* InOwner);
    PActorComponent* ResolveComponent(FObjectHandle Handle) const;
    bool OwnsComponent(const PActorComponent* Component) const;
    void RegisterAllComponents();
    void UnregisterAllComponents();

    friend class PWorld;
    friend class FWorldAssetLoader;

    std::vector<FObjectHandle> ComponentHandles;
    FObjectHandle RootComponentHandle;
    FObjectHandle OwnerHandle;
    FOnActorDestroyed ActorDestroyedEvent;
    bool bHasBegunPlay = false;
    bool bHasEndedPlay = false;
    bool bPendingDestroy = false;
    bool bDestroyedEventBroadcast = false;
};
}
