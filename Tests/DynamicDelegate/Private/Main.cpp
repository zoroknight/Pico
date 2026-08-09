#include "TestRunner.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Tests/DynamicDelegateListener.h"

#include <array>
#include <vector>

namespace
{
using PicoTest::PDynamicDelegateListener;

class PNativeOnlyListener final : public Pico::PObject
{
    PICO_DECLARE_CLASS(PNativeOnlyListener, Pico::PObject)

public:
    void OnValue(Pico::int32) {}

protected:
    explicit PNativeOnlyListener(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }
};

PICO_DEFINE_CLASS(PNativeOnlyListener)

bool PNativeOnlyListener::RegisterProperties(Pico::PClass& Class)
{
    return Class.AddFunction(
        Pico::PFunction::Create<&ThisClass::OnValue>(
            Pico::FName("OnValue"),
            Pico::EFunctionFlags::None,
            {Pico::FName("Value")}));
}

void TestBindingAndBroadcast(FTestRunner& Runner)
{
    Pico::TDynamicMulticastDelegate<void(Pico::int32)> Delegate;
    PDynamicDelegateListener* Listener =
        Pico::NewObject<PDynamicDelegateListener>(nullptr, "BindingListener");
    PNativeOnlyListener* NativeOnly =
        Pico::NewObject<PNativeOnlyListener>(nullptr, "NativeOnlyListener");
    Runner.Expect(
        Listener != nullptr && NativeOnly != nullptr,
        "Dynamic delegate binding fixtures are created");

    Runner.Expect(
        Delegate.AddDynamic(nullptr, Pico::FName("OnValue")).Result
            == Pico::EDynamicDelegateBindResult::InvalidTarget,
        "Dynamic delegate rejects a null target");
    Runner.Expect(
        Delegate.AddDynamic(Listener, Pico::FName("Missing")).Result
            == Pico::EDynamicDelegateBindResult::FunctionNotFound,
        "Dynamic delegate requires a reflected function name");
    Runner.Expect(
        Delegate.AddDynamic(NativeOnly, Pico::FName("OnValue")).Result
            == Pico::EDynamicDelegateBindResult::FunctionNotCallable,
        "Dynamic delegate requires the reflected Callable flag");
    const auto PairResult = Delegate.AddDynamic(Listener, Pico::FName("OnPair"));
    const auto ReturnResult = Delegate.AddDynamic(Listener, Pico::FName("ReturnValue"));
    Runner.Expect(
        PairResult.Result
            == Pico::EDynamicDelegateBindResult::SignatureMismatch
            && ReturnResult.Result
                == Pico::EDynamicDelegateBindResult::SignatureMismatch,
        "Dynamic delegate validates parameters and requires a void return");

    const Pico::FDynamicDelegateBindingResult First =
        Delegate.AddDynamic(Listener, Pico::FName("OnValue"));
    Runner.Expect(
        First.IsSuccess()
            && Delegate.AddUniqueDynamic(Listener, Pico::FName("OnValue")).Result
                == Pico::EDynamicDelegateBindResult::AlreadyBound,
        "AddUniqueDynamic rejects an existing target-function pair");
    const Pico::FDynamicDelegateBindingResult Duplicate =
        Delegate.AddDynamic(Listener, Pico::FName("OnValue"));
    const Pico::FDynamicDelegateBroadcastReport FirstReport = Delegate.Broadcast(7);
    Runner.Expect(
        Duplicate.IsSuccess()
            && FirstReport.InvokedBindingCount == 2
            && Listener->GetValueCallCount() == 2
            && Listener->GetLastValue() == 7,
        "AddDynamic permits duplicate bindings and Broadcast uses ProcessEvent");

    Runner.Expect(
        Delegate.Remove(First.Handle)
            && Delegate.Broadcast(9).InvokedBindingCount == 1
            && Listener->GetValueCallCount() == 3,
        "FDelegateHandle removes exactly one dynamic binding");
    Runner.Expect(
        Delegate.RemoveAll(Listener) == 1 && !Delegate.IsBound(),
        "RemoveAll removes every dynamic binding owned by an object");

    Pico::FDynamicMulticastDelegate RuntimeDelegate(
        {Pico::FFunctionValueDescriptor {Pico::EFunctionValueType::Int32, nullptr}});
    const std::array<Pico::FFunctionValue, 1> WrongType {1.0f};
    Runner.Expect(
        RuntimeDelegate.Broadcast().Result
                == Pico::EDynamicDelegateBroadcastResult::ArgumentCountMismatch
            && RuntimeDelegate.Broadcast(WrongType).Result
                == Pico::EDynamicDelegateBroadcastResult::ArgumentTypeMismatch,
        "Runtime dynamic delegate rejects invalid broadcast argument frames");

    Pico::TDynamicMulticastDelegate<void(PDynamicDelegateListener*)> ObjectDelegate;
    Runner.Expect(
        ObjectDelegate.AddDynamic(Listener, Pico::FName("OnTypedObject")).IsSuccess()
            && ObjectDelegate.AddDynamic(Listener, Pico::FName("OnBaseObject")).Result
                == Pico::EDynamicDelegateBindResult::SignatureMismatch
            && ObjectDelegate.Broadcast(Listener).InvokedBindingCount == 1
            && Listener->GetObjectCallCount() == 1,
        "Dynamic delegate validates the exact reflected class of object parameters");

    Pico::DestroyObject(Listener);
    Pico::DestroyObject(NativeOnly);
}

void TestBroadcastMutationAndFailureIsolation(FTestRunner& Runner)
{
    Pico::TDynamicMulticastDelegate<void(Pico::int32)> Delegate;
    PDynamicDelegateListener* RemoveSelf =
        Pico::NewObject<PDynamicDelegateListener>(nullptr, "RemoveSelf");
    PDynamicDelegateListener* AddLate =
        Pico::NewObject<PDynamicDelegateListener>(nullptr, "AddLate");
    PDynamicDelegateListener* Late =
        Pico::NewObject<PDynamicDelegateListener>(nullptr, "Late");

    const Pico::FDynamicDelegateBindingResult RemoveBinding =
        Delegate.AddDynamic(RemoveSelf, Pico::FName("OnValue"));
    Delegate.AddDynamic(AddLate, Pico::FName("OnValue"));
    RemoveSelf->ConfigureRemoveSelf(&Delegate, RemoveBinding.Handle);
    AddLate->ConfigureAddLate(&Delegate, Late);

    const Pico::FDynamicDelegateBroadcastReport First = Delegate.Broadcast(10);
    Runner.Expect(
        First.SnapshotBindingCount == 2
            && First.InvokedBindingCount == 2
            && RemoveSelf->GetValueCallCount() == 1
            && AddLate->GetValueCallCount() == 1
            && Late->GetLateCallCount() == 0
            && Delegate.Num() == 2,
        "Broadcast snapshot defers new bindings and honors removal during invocation");
    const Pico::FDynamicDelegateBroadcastReport Second = Delegate.Broadcast(20);
    Runner.Expect(
        Second.SnapshotBindingCount == 2
            && Second.InvokedBindingCount == 2
            && RemoveSelf->GetValueCallCount() == 1
            && AddLate->GetValueCallCount() == 2
            && Late->GetLateCallCount() == 1,
        "A binding added during Broadcast participates on the next Broadcast only");

    Delegate.Clear();
    Delegate.AddDynamic(RemoveSelf, Pico::FName("Throwing"));
    Delegate.AddDynamic(Late, Pico::FName("OnLateValue"));
    const Pico::FDynamicDelegateBroadcastReport Failure = Delegate.Broadcast(30);
    Runner.Expect(
        Failure.FailedBindingCount == 1
            && Failure.InvokedBindingCount == 1
            && Failure.LastInvokeFailure == Pico::EFunctionInvokeResult::InvocationFailed
            && Late->GetLateCallCount() == 2,
        "A failing ProcessEvent listener does not stop later dynamic listeners");

    Delegate.Clear();
    Pico::DestroyObject(RemoveSelf);
    Pico::DestroyObject(AddLate);
    Pico::DestroyObject(Late);
}

void TestExpiredTargets(FTestRunner& Runner)
{
    Pico::TDynamicMulticastDelegate<void(Pico::int32)> ExplicitDelegate;
    PDynamicDelegateListener* Explicit =
        Pico::NewObject<PDynamicDelegateListener>(nullptr, "ExplicitExpiry");
    ExplicitDelegate.AddDynamic(Explicit, Pico::FName("OnValue"));
    const Pico::FObjectHandle ExplicitHandle = Explicit->GetHandle();
    Pico::DestroyObject(Explicit);
    const Pico::FDynamicDelegateBroadcastReport ExplicitReport =
        ExplicitDelegate.Broadcast(1);
    Runner.Expect(
        Pico::ResolveObject(ExplicitHandle) == nullptr
            && ExplicitReport.RemovedInvalidBindingCount == 1
            && ExplicitDelegate.GetBindings().empty(),
        "Broadcast removes a dynamic binding whose target was explicitly destroyed");

    PDynamicDelegateListener* Replacement =
        Pico::NewObject<PDynamicDelegateListener>(nullptr, "ExplicitExpiry");
    ExplicitDelegate.Broadcast(2);
    Runner.Expect(
        Replacement != nullptr && Replacement->GetValueCallCount() == 0,
        "Object slot reuse cannot revive a stale dynamic binding");
    Pico::DestroyObject(Replacement);

    Pico::TDynamicMulticastDelegate<void(Pico::int32)> GCDelegate;
    PDynamicDelegateListener* Garbage =
        Pico::NewObject<PDynamicDelegateListener>(nullptr, "GCExpiry");
    const Pico::FObjectHandle GarbageHandle = Garbage->GetHandle();
    GCDelegate.AddDynamic(Garbage, Pico::FName("OnValue"));
    Pico::CollectGarbage();
    const Pico::FDynamicDelegateBroadcastReport GCReport = GCDelegate.Broadcast(3);
    Runner.Expect(
        Pico::ResolveObject(GarbageHandle) == nullptr
            && GCReport.RemovedInvalidBindingCount == 1
            && !GCDelegate.IsBound(),
        "A dynamic binding is weak and is compacted after its target is collected by GC");
}
}

int main()
{
    FTestRunner Runner;
    Runner.Expect(Pico::PObjectSystem::Init(), "Object system initializes");
    if (Pico::PObjectSystem::IsInitialized())
    {
        Runner.Expect(
            PDynamicDelegateListener::RegisterClass(),
            "Dynamic delegate listener class registers");
        Runner.Expect(
            PNativeOnlyListener::RegisterClass(),
            "Native-only listener class registers");
        TestBindingAndBroadcast(Runner);
        TestBroadcastMutationAndFailureIsolation(Runner);
        TestExpiredTargets(Runner);
        Pico::PObjectSystem::Shutdown();
    }
    return Runner.Finish();
}
