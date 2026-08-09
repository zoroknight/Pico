#include "TestRunner.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/ReferenceCollector.h"
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Tests/GCNode.h"

namespace
{
using PicoTest::PGCNode;

void TestStrongWeakAndNativeReferences(FTestRunner& Runner)
{
    PGCNode* Root = Pico::NewObject<PGCNode>(nullptr, "Root");
    PGCNode* Strong = Pico::NewObject<PGCNode>(nullptr, "Strong");
    PGCNode* Weak = Pico::NewObject<PGCNode>(nullptr, "Weak");
    PGCNode* Native = Pico::NewObject<PGCNode>(nullptr, "Native");
    const Pico::FObjectHandle RootHandle = Root->GetHandle();
    const Pico::FObjectHandle StrongHandle = Strong->GetHandle();
    const Pico::FObjectHandle WeakHandle = Weak->GetHandle();
    const Pico::FObjectHandle NativeHandle = Native->GetHandle();
    Root->SetStrong(Strong);
    Root->SetWeak(Weak);
    Root->AddNativeStrong(Native);
    Runner.Expect(Pico::AddToRoot(Root), "GC root can be added");

    const Pico::FGarbageCollectionResult First = Pico::CollectGarbage();
    Runner.Expect(First.bSucceeded, "GC collection succeeds");
    Runner.Expect(First.CollectedObjectCount == 1, "weak-only target is collected");
    Runner.Expect(Pico::ResolveObject(RootHandle) == Root, "root survives collection");
    Runner.Expect(Pico::ResolveObject(StrongHandle) == Strong, "reflected strong target survives");
    Runner.Expect(Pico::ResolveObject(NativeHandle) == Native, "native reported target survives");
    Runner.Expect(Pico::ResolveObject(WeakHandle) == nullptr && Root->GetWeak() == nullptr, "weak reference expires safely");

    Runner.Expect(Pico::RemoveFromRoot(Root), "GC root can be removed");
    const Pico::FGarbageCollectionResult Second = Pico::CollectGarbage();
    Runner.Expect(Second.CollectedObjectCount == 3, "unrooted strong graph is collected");
    Runner.Expect(Pico::ResolveObject(RootHandle) == nullptr, "former root handle expires");
}

void TestCyclesAndOuterDirection(FTestRunner& Runner)
{
    PGCNode* A = Pico::NewObject<PGCNode>(nullptr, "CycleA");
    PGCNode* B = Pico::NewObject<PGCNode>(nullptr, "CycleB");
    const Pico::FObjectHandle StaleCycleHandle = A->GetHandle();
    A->SetStrong(B);
    B->SetStrong(A);
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 2, "unrooted strong cycle is collected");

    PGCNode* Parent = Pico::NewObject<PGCNode>(nullptr, "OuterParent");
    PGCNode* Child = Pico::NewObject<PGCNode>(Parent, "RootedChild");
    const Pico::FObjectHandle ParentHandle = Parent->GetHandle();
    Runner.Expect(Pico::AddToRoot(Child), "inner object can be rooted");
    Runner.Expect(Pico::ResolveObject(StaleCycleHandle) == nullptr, "stale handle cannot resolve after slot reuse");
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 0, "rooted child keeps its Outer alive");
    Runner.Expect(Pico::ResolveObject(ParentHandle) == Parent, "Outer is a child-to-parent strong reference");
    Pico::RemoveFromRoot(Child);
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 2, "unrooted Outer graph is collected");

    Parent = Pico::NewObject<PGCNode>(nullptr, "RootedParent");
    Child = Pico::NewObject<PGCNode>(Parent, "UnreferencedChild");
    const Pico::FObjectHandle ChildHandle = Child->GetHandle();
    Pico::AddToRoot(Parent);
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 1, "Outer does not automatically keep every inner alive");
    Runner.Expect(Pico::ResolveObject(ChildHandle) == nullptr, "unreported inner reference is collected");
    Pico::RemoveFromRoot(Parent);
    Pico::CollectGarbage();
}

void TestCollectionGuard(FTestRunner& Runner)
{
    PicoTest::PGCNode::EnableReentrancyProbe();
    Pico::NewObject<PicoTest::PGCNode>(nullptr, "GCReentrancyProbe");
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 1, "reentrancy probe is collected");
    Runner.Expect(PicoTest::PGCNode::WasGCActiveInBeginDestroy(), "BeginDestroy observes active GC guard");
    Runner.Expect(PicoTest::PGCNode::WasNestedCollectionRejected(), "nested garbage collection is rejected");
}

void TestWorldGraph(FTestRunner& Runner)
{
    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "GCWorld", Pico::EObjectFlags::RootSet);
    Runner.Expect(World != nullptr && World->Initialize(), "rooted World initializes");
    Pico::PActor* Actor = World != nullptr ? World->SpawnActor<Pico::PActor>("GCActor") : nullptr;
    Pico::PActorComponent* Component = Actor != nullptr
        ? Actor->CreateComponent<Pico::PActorComponent>("GCComponent")
        : nullptr;
    const Pico::FObjectHandle ActorHandle = Actor != nullptr ? Actor->GetHandle() : Pico::FObjectHandle {};
    const Pico::FObjectHandle ComponentHandle = Component != nullptr ? Component->GetHandle() : Pico::FObjectHandle {};
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 0, "rooted World keeps Level Actor and Component alive");
    Runner.Expect(Pico::ResolveObject(ActorHandle) == Actor, "World-Level-Actor strong chain survives");
    Runner.Expect(Pico::ResolveObject(ComponentHandle) == Component, "Actor-Component strong chain survives");
    Pico::RemoveFromRoot(World);
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount >= 4, "unrooted World graph is collected");
}

void TestCollectionRequests(FTestRunner& Runner)
{
    Runner.Expect(!Pico::IsGarbageCollectionRequested(), "GC starts without a pending request");
    Pico::RequestGarbageCollection(Pico::EGarbageCollectionReason::TimeLimit);
    Pico::RequestGarbageCollection(Pico::EGarbageCollectionReason::WorldTransition);

    const Pico::EGarbageCollectionReason Reasons =
        Pico::GetPendingGarbageCollectionReasons();
    Runner.Expect(
        Pico::HasAnyGarbageCollectionReason(
            Reasons, Pico::EGarbageCollectionReason::TimeLimit)
            && Pico::HasAnyGarbageCollectionReason(
                Reasons, Pico::EGarbageCollectionReason::WorldTransition),
        "GC scheduler merges requests raised before the next safe point");

    PGCNode* Pending = Pico::NewObject<PGCNode>(nullptr, "PendingScheduledGC");
    const Pico::FObjectHandle PendingHandle = Pending->GetHandle();
    Runner.Expect(
        Pico::ResolveObject(PendingHandle) == Pending,
        "requesting GC does not collect objects immediately");

    Pico::FGarbageCollectionResult Result;
    Runner.Expect(
        Pico::CollectGarbageIfRequested(&Result)
            && Result.CollectedObjectCount == 1
            && Pico::ResolveObject(PendingHandle) == nullptr,
        "a GC safe point executes the deferred request");
    Runner.Expect(
        !Pico::IsGarbageCollectionRequested()
            && !Pico::CollectGarbageIfRequested(),
        "a successful collection consumes pending requests exactly once");
}
}

int main()
{
    FTestRunner Runner;
    Runner.Expect(Pico::PObjectSystem::Init(), "Object system initializes");
    if (Pico::PObjectSystem::IsInitialized())
    {
        Runner.Expect(PGCNode::RegisterClass(), "GC test class registers");
        Runner.Expect(Pico::PActorComponent::RegisterClass(), "ActorComponent class registers");
        Runner.Expect(Pico::PSceneComponent::RegisterClass(), "SceneComponent class registers");
        Runner.Expect(Pico::PActor::RegisterClass(), "Actor class registers");
        Runner.Expect(Pico::PLevel::RegisterClass(), "Level class registers");
        Runner.Expect(Pico::PWorld::RegisterClass(), "World class registers");
        TestStrongWeakAndNativeReferences(Runner);
        TestCyclesAndOuterDirection(Runner);
        TestCollectionGuard(Runner);
        TestWorldGraph(Runner);
        TestCollectionRequests(Runner);
        Runner.Expect(PGCNode::GetDestroyedInstanceCount() >= 10, "BeginDestroy runs for collected instances");
        Runner.Expect(PGCNode::StaticClass()->GetDefaultObject() != nullptr, "CDO remains owned outside runtime GC");
        Pico::PObjectSystem::Shutdown();
    }
    return Runner.Finish();
}
