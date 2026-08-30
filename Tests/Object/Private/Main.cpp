#include "TestRunner.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/Archive.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ObjectSerialization.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/Property.h"
#include "Pico/Object/ReflectionMacros.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
class PMacroObject : public Pico::PObject
{
    PICO_DECLARE_CLASS(PMacroObject, Pico::PObject)

public:
    Pico::int32 GetMacroValue() const
    {
        return MacroValue;
    }

    Pico::int32 AddToMacroValue(Pico::int32 Delta) const
    {
        return MacroValue + Delta;
    }

protected:
    explicit PMacroObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

private:
    Pico::int32 MacroValue = 42;
};

PICO_DEFINE_CLASS(PMacroObject)

bool PMacroObject::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, MacroValue);
    if (!Class.AddProperties(std::move(Properties)))
    {
        return false;
    }
    std::vector<Pico::PFunction> Functions;
    PICO_ADD_FUNCTION(
        Functions,
        AddToMacroValue,
        Pico::EFunctionFlags::Callable | Pico::EFunctionFlags::Pure,
        Pico::FName("Delta"));
    return Class.AddFunctions(std::move(Functions));
}

class PMacroDerivedObject final : public PMacroObject
{
    PICO_DECLARE_CLASS(PMacroDerivedObject, PMacroObject)

protected:
    explicit PMacroDerivedObject(const Pico::FObjectConstructionParams& Params)
        : PMacroObject(Params)
    {
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PMacroDerivedObject)

class PDelegateListener final : public Pico::PObject
{
    PICO_DECLARE_CLASS(PDelegateListener, Pico::PObject)

public:
    void OnValue(Pico::int32 Value)
    {
        Total += Value;
    }

    Pico::int32 AddTotal(Pico::int32 Value) const
    {
        return Total + Value;
    }

    Pico::int32 GetTotal() const
    {
        return Total;
    }

protected:
    explicit PDelegateListener(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

private:
    Pico::int32 Total = 0;
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PDelegateListener)

class PAssetReferenceObject final : public Pico::PObject
{
    PICO_DECLARE_CLASS(PAssetReferenceObject, Pico::PObject)

public:
    Pico::int32 GetTransientValue() const
    {
        return TransientValue;
    }

protected:
    explicit PAssetReferenceObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

private:
    Pico::FAssetPath AssetPath;
    Pico::int32 TransientValue = 7;
};

PICO_DEFINE_CLASS(PAssetReferenceObject)

bool PAssetReferenceObject::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_ASSET_PROPERTY(Properties, AssetPath, StaticMesh);
    Pico::FPropertyMetadata TransientMetadata;
    TransientMetadata.Flags =
        Pico::EPropertyFlags::Editable | Pico::EPropertyFlags::Transient;
    Properties.push_back(
        Pico::PProperty::Create<&ThisClass::TransientValue>(
            Pico::FName("TransientValue"),
            TransientMetadata));
    return Class.AddProperties(std::move(Properties));
}

class PInvalidMacroObject : public Pico::PObject
{
    PICO_DECLARE_CLASS(PInvalidMacroObject, Pico::PObject)

protected:
    explicit PInvalidMacroObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

private:
    Pico::int32 DuplicateValue = 0;
};

PICO_DEFINE_CLASS(PInvalidMacroObject)

bool PInvalidMacroObject::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, DuplicateValue);
    PICO_ADD_PROPERTY(Properties, DuplicateValue);
    return Class.AddProperties(std::move(Properties));
}

class PInvalidMacroDerivedObject final : public PInvalidMacroObject
{
    PICO_DECLARE_CLASS(PInvalidMacroDerivedObject, PInvalidMacroObject)

protected:
    explicit PInvalidMacroDerivedObject(const Pico::FObjectConstructionParams& Params)
        : PInvalidMacroObject(Params)
    {
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PInvalidMacroDerivedObject)

class PTestObject : public Pico::PObject
{
public:
    static const Pico::PClass* StaticClass()
    {
        static Pico::PClass Class = Pico::PClass::Create<PTestObject>(
            Pico::FName("PTestObject"),
            Pico::PObject::StaticClass(),
            sizeof(PTestObject),
            &PTestObject::ConstructInstance);
        static const bool bMetadataAdded = AddMetadata(Class);
        if (!bMetadataAdded)
        {
            return &Class;
        }
        return &Class;
    }

    static bool TryAddPropertyAfterRegistration()
    {
        auto* Class = const_cast<Pico::PClass*>(StaticClass());
        return Class->AddProperty(
            Pico::PProperty::Create<&PTestObject::Health>(Pico::FName("LateHealth")));
    }

    static Pico::PProperty CreateHealthPropertyForTest(Pico::FName Name)
    {
        return Pico::PProperty::Create<&PTestObject::Health>(Name);
    }

    Pico::int32 GetHealth() const
    {
        return Health;
    }

    float GetSpeed() const
    {
        return Speed;
    }

    bool IsAlive() const
    {
        return bAlive;
    }

    bool SawIdentityDuringConstruction() const
    {
        return bSawIdentityDuringConstruction;
    }

    bool DidPostInitProperties() const
    {
        return bDidPostInitProperties;
    }

    bool DidPostLoad() const
    {
        return bDidPostLoad;
    }

    Pico::int32 GetHealthSeenInPostLoad() const
    {
        return HealthSeenInPostLoad;
    }

    Pico::int32 AddHealth(Pico::int32 Delta)
    {
        Health += Delta;
        return Health;
    }

    bool IsHealthAtLeast(Pico::int32 Minimum) const
    {
        return Health >= Minimum;
    }

    void SetAlive(bool bInAlive)
    {
        bAlive = bInAlive;
    }

    PMacroObject* EchoMacroObject(PMacroObject* Object) const
    {
        return Object;
    }

    void ThrowFromFunction()
    {
        throw std::runtime_error("Expected reflected function failure");
    }

    void ServerNotify(Pico::int32)
    {
    }

protected:
    explicit PTestObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
        , bSawIdentityDuringConstruction(
            GetClass() == Params.Class
            && GetOuter() == Params.Outer
            && GetName() == Params.Name)
    {
    }

    void PostInitProperties() override
    {
        bDidPostInitProperties = true;
    }

    void PostLoad() override
    {
        bDidPostLoad = true;
        HealthSeenInPostLoad = Health;
    }

private:
    static bool AddMetadata(Pico::PClass& Class)
    {
        std::vector<Pico::PProperty> Properties;
        Properties.push_back(
            Pico::PProperty::Create<&PTestObject::Health>(Pico::FName("Health")));
        Properties.push_back(
            Pico::PProperty::Create<&PTestObject::Speed>(Pico::FName("Speed")));
        Properties.push_back(
            Pico::PProperty::Create<&PTestObject::bAlive>(Pico::FName("bAlive")));
        if (!Class.AddProperties(std::move(Properties)))
        {
            return false;
        }

        std::vector<Pico::PFunction> Functions;
        Functions.push_back(Pico::PFunction::Create<&PTestObject::AddHealth>(
            Pico::FName("AddHealth"),
            Pico::EFunctionFlags::Callable,
            { Pico::FName("Delta") }));
        Functions.push_back(Pico::PFunction::Create<&PTestObject::IsHealthAtLeast>(
            Pico::FName("IsHealthAtLeast"),
            Pico::EFunctionFlags::Callable | Pico::EFunctionFlags::Pure,
            { Pico::FName("Minimum") }));
        Functions.push_back(Pico::PFunction::Create<&PTestObject::SetAlive>(
            Pico::FName("SetAlive"),
            Pico::EFunctionFlags::Callable,
            { Pico::FName("bAlive") }));
        Functions.push_back(Pico::PFunction::Create<&PTestObject::EchoMacroObject>(
            Pico::FName("EchoMacroObject"),
            Pico::EFunctionFlags::Callable,
            { Pico::FName("Object") }));
        Functions.push_back(Pico::PFunction::Create<&PTestObject::ThrowFromFunction>(
            Pico::FName("ThrowFromFunction")));
        Functions.push_back(Pico::PFunction::Create<&PTestObject::ServerNotify>(
            Pico::FName("ServerNotify"),
            Pico::EFunctionFlags::Server | Pico::EFunctionFlags::Reliable,
            { Pico::FName("Value") }));
        return Class.AddFunctions(std::move(Functions));
    }

    static Pico::FObjectPtr ConstructInstance(const Pico::FObjectConstructionParams& Params)
    {
        return Pico::FObjectPtr(new PTestObject(Params));
    }

    Pico::int32 Health = 100;
    float Speed = 600.0f;
    bool bAlive = true;
    bool bSawIdentityDuringConstruction = false;
    bool bDidPostInitProperties = false;
    bool bDidPostLoad = false;
    Pico::int32 HealthSeenInPostLoad = 0;
};

class PTestDerivedObject final : public PTestObject
{
public:
    static const Pico::PClass* StaticClass()
    {
        static Pico::PClass Class = Pico::PClass::Create<PTestDerivedObject>(
            Pico::FName("PTestDerivedObject"),
            PTestObject::StaticClass(),
            sizeof(PTestDerivedObject),
            &PTestDerivedObject::ConstructInstance);
        static const bool bMetadataAdded = AddMetadata(Class);
        if (!bMetadataAdded)
        {
            return &Class;
        }
        return &Class;
    }

    Pico::int32 GetScore() const
    {
        return Score;
    }

    Pico::int32 MultiplyScore(Pico::int32 Factor)
    {
        Score *= Factor;
        return Score;
    }

private:
    explicit PTestDerivedObject(const Pico::FObjectConstructionParams& Params)
        : PTestObject(Params)
    {
    }

    static Pico::FObjectPtr ConstructInstance(const Pico::FObjectConstructionParams& Params)
    {
        return Pico::FObjectPtr(new PTestDerivedObject(Params));
    }

    static bool AddMetadata(Pico::PClass& Class)
    {
        if (!Class.AddProperty(
                Pico::PProperty::Create<&PTestDerivedObject::Score>(Pico::FName("Score"))))
        {
            return false;
        }
        return Class.AddFunction(Pico::PFunction::Create<&PTestDerivedObject::MultiplyScore>(
            Pico::FName("MultiplyScore"),
            Pico::EFunctionFlags::Callable,
            { Pico::FName("Factor") }));
    }

    Pico::int32 Score = 10;
};

class PThrowingPostInitObject final : public PTestObject
{
public:
    static const Pico::PClass* StaticClass()
    {
        static const Pico::PClass Class = Pico::PClass::Create<PThrowingPostInitObject>(
            Pico::FName("PThrowingPostInitObject"),
            PTestObject::StaticClass(),
            sizeof(PThrowingPostInitObject),
            &PThrowingPostInitObject::ConstructInstance);
        return &Class;
    }

protected:
    explicit PThrowingPostInitObject(const Pico::FObjectConstructionParams& Params)
        : PTestObject(Params)
    {
    }

    void PostInitProperties() override
    {
        Pico::NewObject<PTestObject>(this, "CreatedBeforeFailure");
        throw std::runtime_error("Expected PostInitProperties failure");
    }

private:
    static Pico::FObjectPtr ConstructInstance(const Pico::FObjectConstructionParams& Params)
    {
        return Pico::FObjectPtr(new PThrowingPostInitObject(Params));
    }
};

class PThrowingPostLoadObject final : public PTestObject
{
public:
    static const Pico::PClass* StaticClass()
    {
        static const Pico::PClass Class = Pico::PClass::Create<PThrowingPostLoadObject>(
            Pico::FName("PThrowingPostLoadObject"),
            PTestObject::StaticClass(),
            sizeof(PThrowingPostLoadObject),
            &PThrowingPostLoadObject::ConstructInstance);
        return &Class;
    }

protected:
    explicit PThrowingPostLoadObject(const Pico::FObjectConstructionParams& Params)
        : PTestObject(Params)
    {
    }

    void PostLoad() override
    {
        Pico::NewObject<PTestObject>(this, "CreatedBeforePostLoadFailure");
        throw std::runtime_error("Expected PostLoad failure");
    }

private:
    static Pico::FObjectPtr ConstructInstance(const Pico::FObjectConstructionParams& Params)
    {
        return Pico::FObjectPtr(new PThrowingPostLoadObject(Params));
    }
};

class PInvalidMetadataObject final : public Pico::PObject
{
public:
    static const Pico::PClass* StaticClass()
    {
        static Pico::PClass Class = Pico::PClass::Create<PInvalidMetadataObject>(
            Pico::FName("PInvalidMetadataObject"),
            Pico::PObject::StaticClass(),
            sizeof(PInvalidMetadataObject),
            &PInvalidMetadataObject::ConstructInstance);
        static const bool bPropertiesAdded = AddInvalidProperties(Class);
        (void)bPropertiesAdded;
        return &Class;
    }

private:
    explicit PInvalidMetadataObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

    static bool AddInvalidProperties(Pico::PClass& Class)
    {
        std::vector<Pico::PProperty> Properties;
        Properties.push_back(
            Pico::PProperty::Create<&PInvalidMetadataObject::First>(Pico::FName("Duplicate")));
        Properties.push_back(
            Pico::PProperty::Create<&PInvalidMetadataObject::Second>(Pico::FName("Duplicate")));
        return Class.AddProperties(std::move(Properties));
    }

    static Pico::FObjectPtr ConstructInstance(const Pico::FObjectConstructionParams& Params)
    {
        return Pico::FObjectPtr(new PInvalidMetadataObject(Params));
    }

    Pico::int32 First = 1;
    Pico::int32 Second = 2;
};

class PWrongOwnerMetadataObject final : public Pico::PObject
{
public:
    static const Pico::PClass* StaticClass()
    {
        static Pico::PClass Class = Pico::PClass::Create<PWrongOwnerMetadataObject>(
            Pico::FName("PWrongOwnerMetadataObject"),
            Pico::PObject::StaticClass(),
            sizeof(PWrongOwnerMetadataObject),
            &PWrongOwnerMetadataObject::ConstructInstance);
        static const bool bPropertyAdded =
            Class.AddProperty(PTestObject::CreateHealthPropertyForTest(Pico::FName("ForeignHealth")));
        (void)bPropertyAdded;
        return &Class;
    }

private:
    explicit PWrongOwnerMetadataObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

    static Pico::FObjectPtr ConstructInstance(const Pico::FObjectConstructionParams& Params)
    {
        return Pico::FObjectPtr(new PWrongOwnerMetadataObject(Params));
    }
};

std::vector<std::string> GBeginDestroyOrder;
bool GHandlesResolvedDuringBeginDestroy = true;
bool GChildCreationRejectedDuringBeginDestroy = false;
bool GSelfDestroyRejected = false;
bool GSiblingDestroySucceeded = false;
bool GRootCreationSucceeded = false;
bool GShutdownCreationRejected = false;
Pico::FObjectHandle GSiblingHandle;
Pico::FObjectHandle GCreatedRootHandle;

class PBeginDestroyObject final : public Pico::PObject
{
public:
    static const Pico::PClass* StaticClass()
    {
        static const Pico::PClass Class = Pico::PClass::Create<PBeginDestroyObject>(
            Pico::FName("PBeginDestroyObject"),
            Pico::PObject::StaticClass(),
            sizeof(PBeginDestroyObject),
            &PBeginDestroyObject::ConstructInstance);
        return &Class;
    }

protected:
    explicit PBeginDestroyObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

    void BeginDestroy() override
    {
        GBeginDestroyOrder.push_back(GetName().ToString());
        GHandlesResolvedDuringBeginDestroy =
            GHandlesResolvedDuringBeginDestroy && Pico::ResolveObject(GetHandle()) == this;

        if (GetName() == Pico::FName("DestroyRoot"))
        {
            GChildCreationRejectedDuringBeginDestroy =
                Pico::NewObject<PTestObject>(this, "CreatedDuringBeginDestroy") == nullptr;
        }
        else if (GetName() == Pico::FName("SelfDestroy"))
        {
            GSelfDestroyRejected = !Pico::DestroyObject(this);
        }
        else if (GetName() == Pico::FName("SiblingDestroyer"))
        {
            GSiblingDestroySucceeded =
                Pico::DestroyObject(Pico::ResolveObject(GSiblingHandle));
        }
        else if (GetName() == Pico::FName("RootCreator"))
        {
            PTestObject* Created =
                Pico::NewObject<PTestObject>(nullptr, "CreatedDuringDestroy");
            GRootCreationSucceeded = Created != nullptr;
            GCreatedRootHandle =
                Created != nullptr ? Created->GetHandle() : Pico::FObjectHandle {};
        }
        else if (GetName() == Pico::FName("ShutdownCreator"))
        {
            GShutdownCreationRejected =
                Pico::NewObject<PTestObject>(nullptr, "CreatedDuringShutdown") == nullptr;
        }
    }

private:
    static Pico::FObjectPtr ConstructInstance(const Pico::FObjectConstructionParams& Params)
    {
        return Pico::FObjectPtr(new PBeginDestroyObject(Params));
    }
};

void TestClassRegistry(FTestRunner& Runner)
{
    Runner.Expect(Pico::FClassRegistry::FindClass(Pico::FName("PObject")) == Pico::PObject::StaticClass(),
        "Object system registers the intrinsic PObject class");
    Runner.Expect(Pico::FClassRegistry::RegisterClass(PTestObject::StaticClass()), "Test class registers");
    Runner.Expect(Pico::FClassRegistry::RegisterClass(PTestDerivedObject::StaticClass()), "Derived test class registers");
    Runner.Expect(Pico::FClassRegistry::RegisterClass(PThrowingPostInitObject::StaticClass()),
        "Post-init failure test class registers");
    Runner.Expect(Pico::FClassRegistry::RegisterClass(PThrowingPostLoadObject::StaticClass()),
        "Post-load failure test class registers");
    Runner.Expect(Pico::FClassRegistry::RegisterClass(PBeginDestroyObject::StaticClass()),
        "BeginDestroy test class registers");
    Runner.Expect(PMacroObject::RegisterClass(), "Macro-declared class registers");
    Runner.Expect(PMacroDerivedObject::RegisterClass(), "Macro-declared no-property class registers");
    Runner.Expect(PDelegateListener::RegisterClass(), "Delegate listener class registers");
    Runner.Expect(Pico::FClassRegistry::FindClass(Pico::FName("PTestObject")) == PTestObject::StaticClass(),
        "Class registry finds a class by name");
    Runner.Expect(PTestDerivedObject::StaticClass()->IsChildOf(PTestObject::StaticClass()),
        "Class metadata follows the direct superclass");
    Runner.Expect(PTestDerivedObject::StaticClass()->IsChildOf(Pico::PObject::StaticClass()),
        "Class metadata follows the full inheritance chain");
    Runner.Expect(!PTestObject::StaticClass()->IsChildOf(PTestDerivedObject::StaticClass()),
        "A base class is not a child of its derived class");
    Runner.Expect(PTestDerivedObject::StaticClass()->GetSize() == sizeof(PTestDerivedObject),
        "Class metadata records the native object size");

    const Pico::PClass DuplicateName(
        Pico::FName("PTestObject"), Pico::PObject::StaticClass(), sizeof(Pico::PObject), nullptr);
    Runner.Expect(!Pico::FClassRegistry::RegisterClass(&DuplicateName),
        "Class registry rejects a different class with a duplicate name");

    const Pico::PClass UnregisteredSuperClass(
        Pico::FName("UnregisteredSuperClass"), Pico::PObject::StaticClass(), sizeof(Pico::PObject), nullptr);
    const Pico::PClass OrphanClass(
        Pico::FName("OrphanClass"), &UnregisteredSuperClass, sizeof(Pico::PObject), nullptr);
    Runner.Expect(!Pico::FClassRegistry::RegisterClass(&OrphanClass),
        "Class registry requires the superclass to be registered first");

    const Pico::PClass* InvalidMetadataClass = PInvalidMetadataObject::StaticClass();
    Runner.Expect(!InvalidMetadataClass->IsMetadataValid(),
        "A failed property batch marks class metadata invalid");
    Runner.Expect(InvalidMetadataClass->GetProperties().empty(),
        "A failed property batch leaves no partially registered properties");
    Runner.Expect(!Pico::FClassRegistry::RegisterClass(InvalidMetadataClass),
        "Class registry rejects invalid property metadata");

    const Pico::PClass* WrongOwnerClass = PWrongOwnerMetadataObject::StaticClass();
    Runner.Expect(!WrongOwnerClass->IsMetadataValid() && WrongOwnerClass->GetProperties().empty(),
        "A member pointer from another native class is rejected atomically");
    Runner.Expect(!Pico::FClassRegistry::RegisterClass(WrongOwnerClass),
        "Class registry rejects mismatched native owner metadata");

    Runner.Expect(PTestObject::StaticClass()->IsMetadataFinalized(),
        "Class registration finalizes property metadata");
    Runner.Expect(!PTestObject::TryAddPropertyAfterRegistration(),
        "Finalized class metadata rejects later mutation");
    Runner.Expect(PTestObject::StaticClass()->IsMetadataValid(),
        "Rejected late mutation does not corrupt finalized metadata");
}

void TestObjectDelegates(FTestRunner& Runner)
{
    Pico::TObjectMulticastDelegate<void(Pico::int32)> MulticastDelegate;
    Pico::TObjectDelegate<Pico::int32(Pico::int32)> SingleDelegate;
    PDelegateListener* Listener =
        Pico::NewObject<PDelegateListener>(nullptr, "DelegateListener");
    const Pico::FDelegateHandle InvalidHandle =
        MulticastDelegate.AddObject(
            static_cast<PDelegateListener*>(nullptr),
            &PDelegateListener::OnValue);
    const Pico::FDelegateHandle ListenerHandle = Listener != nullptr
        ? MulticastDelegate.AddObject(Listener, &PDelegateListener::OnValue)
        : Pico::FDelegateHandle {};
    const bool bSingleBound = Listener != nullptr
        && SingleDelegate.BindObject(Listener, &PDelegateListener::AddTotal);
    MulticastDelegate.Broadcast(4);
    Runner.Expect(
        !InvalidHandle.IsValid()
            && ListenerHandle.IsValid()
            && bSingleBound
            && Listener != nullptr
            && Listener->GetTotal() == 4
            && MulticastDelegate.IsBoundTo(Listener)
            && SingleDelegate.IsBoundTo(Listener)
            && SingleDelegate.Execute(6) == 10,
        "Object delegates invoke type-safe mutable and const member functions");

    const Pico::FObjectHandle OldObjectHandle = Listener != nullptr
        ? Listener->GetHandle()
        : Pico::FObjectHandle {};
    Runner.Expect(
        Listener != nullptr && Pico::DestroyObject(Listener),
        "A delegate listener can be destroyed while bindings still exist");
    Runner.Expect(
        Pico::ResolveObject(OldObjectHandle) == nullptr
            && !MulticastDelegate.IsBound()
            && !SingleDelegate.IsBound()
            && !SingleDelegate.ExecuteIfBound(2).has_value(),
        "Object delegate guards expire as soon as the target object is destroyed");

    PDelegateListener* Replacement =
        Pico::NewObject<PDelegateListener>(nullptr, "DelegateListener");
    MulticastDelegate.Broadcast(7);
    Runner.Expect(
        Replacement != nullptr
            && Replacement->GetHandle() != OldObjectHandle
            && Replacement->GetTotal() == 0
            && MulticastDelegate.Num() == 0,
        "A reused object slot cannot revive a stale weak delegate binding");

    if (Replacement != nullptr)
    {
        MulticastDelegate.AddObject(Replacement, &PDelegateListener::OnValue);
        MulticastDelegate.AddObject(Replacement, &PDelegateListener::OnValue);
    }
    Runner.Expect(
        Replacement != nullptr
            && MulticastDelegate.RemoveAll(Replacement) == 2
            && !MulticastDelegate.IsBoundTo(Replacement),
        "RemoveAll removes every binding owned by one object");

    if (Replacement != nullptr)
    {
        Pico::DestroyObject(Replacement);
    }
}

void TestClassDefaultObjects(FTestRunner& Runner)
{
    const Pico::PClass* BaseClass = PTestObject::StaticClass();
    const Pico::PClass* DerivedClass = PTestDerivedObject::StaticClass();
    const PTestObject* BaseDefault = Pico::GetDefault<PTestObject>();
    PTestDerivedObject* DerivedDefault = Pico::GetMutableDefault<PTestDerivedObject>();
    const std::size_t ObjectCountBeforeDefaults = Pico::FObjectRegistry::GetObjectCount();

    Runner.Expect(
        BaseDefault != nullptr
            && BaseDefault == BaseClass->GetDefaultObject()
            && BaseDefault == Pico::GetDefault<PTestObject>(),
        "Each registered class exposes one stable class default object");
    Runner.Expect(
        BaseDefault != nullptr
            && Pico::HasAnyFlags(BaseDefault->GetFlags(), Pico::EObjectFlags::ClassDefaultObject)
            && Pico::HasAnyFlags(BaseDefault->GetFlags(), Pico::EObjectFlags::RootSet)
            && Pico::HasAnyFlags(BaseDefault->GetFlags(), Pico::EObjectFlags::Transient)
            && !BaseDefault->GetHandle().IsValid()
            && !BaseDefault->DidPostInitProperties(),
        "A CDO is a rooted transient template outside runtime registration and PostInit");
    Runner.Expect(
        DerivedDefault != nullptr
            && DerivedDefault->GetClass() == DerivedClass
            && DerivedDefault->GetHealth() == 100
            && DerivedDefault->GetScore() == 10,
        "A derived CDO contains inherited and directly declared native defaults");
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == ObjectCountBeforeDefaults,
        "Querying CDOs does not add runtime objects to the object registry");

    const Pico::PProperty* HealthProperty = DerivedClass->FindProperty(Pico::FName("Health"));
    const Pico::PProperty* ScoreProperty = DerivedClass->FindProperty(Pico::FName("Score"));
    const bool bDefaultsChanged =
        HealthProperty != nullptr && HealthProperty->SetValue(DerivedDefault, Pico::int32 { 175 })
        && ScoreProperty != nullptr && ScoreProperty->SetValue(DerivedDefault, Pico::int32 { 25 });
    Runner.Expect(bDefaultsChanged, "Reflected class defaults can be changed through metadata");

    PTestDerivedObject* First = Pico::NewObject<PTestDerivedObject>(nullptr, "CdoFirst");
    PTestDerivedObject* Second = Pico::NewObject<PTestDerivedObject>(nullptr, "CdoSecond");
    Runner.Expect(
        First != nullptr && Second != nullptr
            && First->GetHealth() == 175 && First->GetScore() == 25
            && Second->GetHealth() == 175 && Second->GetScore() == 25,
        "NewObject initializes inherited and local reflected properties from the exact class CDO");
    if (First != nullptr && HealthProperty != nullptr)
    {
        HealthProperty->SetValue(First, Pico::int32 { 5 });
    }
    Runner.Expect(
        First != nullptr && Second != nullptr
            && First->GetHealth() == 5
            && Second->GetHealth() == 175
            && DerivedDefault->GetHealth() == 175,
        "Changing one instance does not mutate its CDO or sibling instances");

    PTestDerivedObject* CustomTemplate = Pico::NewObject<PTestDerivedObject>(nullptr, "CustomTemplate");
    if (CustomTemplate != nullptr && ScoreProperty != nullptr)
    {
        ScoreProperty->SetValue(CustomTemplate, Pico::int32 { 80 });
    }
    const Pico::FObjectConstructionParams CustomParams {
        DerivedClass,
        nullptr,
        Pico::FName("CustomTemplateInstance"),
        Pico::EObjectFlags::None,
        CustomTemplate
    };
    auto* CustomInstance = static_cast<PTestDerivedObject*>(Pico::NewObject(CustomParams));
    Runner.Expect(
        CustomInstance != nullptr && CustomInstance->GetScore() == 80,
        "The unified construction path accepts a compatible explicit template");

    Pico::FMemoryWriter MissingPropertyWriter;
    Pico::uint32 Magic = 0x4a424f50;
    Pico::uint32 Version = 3;
    std::string ClassName = "PTestDerivedObject";
    std::string ObjectName = "CdoMissingProperties";
    Pico::uint32 Flags = 0;
    Pico::uint32 PropertyCount = 0;
    MissingPropertyWriter.SerializeUInt32(Magic);
    MissingPropertyWriter.SerializeUInt32(Version);
    MissingPropertyWriter.SerializeString(ClassName);
    MissingPropertyWriter.SerializeString(ObjectName);
    MissingPropertyWriter.SerializeUInt32(Flags);
    MissingPropertyWriter.SerializeUInt32(PropertyCount);

    Pico::FMemoryReader MissingPropertyReader(MissingPropertyWriter.GetData());
    Pico::EObjectSerializationError LoadError = Pico::EObjectSerializationError::InvalidArchive;
    PTestDerivedObject* Loaded = static_cast<PTestDerivedObject*>(
        Pico::LoadObject(MissingPropertyReader, nullptr, &LoadError));
    Runner.Expect(
        Loaded != nullptr
            && LoadError == Pico::EObjectSerializationError::None
            && Loaded->GetHealth() == 175
            && Loaded->GetScore() == 25
            && Loaded->DidPostLoad()
            && Loaded->GetHealthSeenInPostLoad() == 175,
        "Missing serialized properties retain CDO defaults before PostLoad runs");

    Pico::FMemoryWriter DefaultObjectWriter;
    Runner.Expect(
        !Pico::SaveObject(DefaultObjectWriter, DerivedDefault),
        "Class default objects cannot be serialized as runtime object instances");

    for (Pico::PObject* Object : std::vector<Pico::PObject*> {
             First, Second, CustomTemplate, CustomInstance, Loaded })
    {
        if (Object != nullptr)
        {
            Pico::DestroyObject(Object);
        }
    }

    if (HealthProperty != nullptr)
    {
        HealthProperty->SetValue(DerivedDefault, Pico::int32 { 100 });
    }
    if (ScoreProperty != nullptr)
    {
        ScoreProperty->SetValue(DerivedDefault, Pico::int32 { 10 });
    }
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == ObjectCountBeforeDefaults,
        "CDO construction tests release every runtime instance");
}

void TestReflectionMacros(FTestRunner& Runner)
{
    const Pico::PClass* MacroClass = PMacroObject::StaticClass();
    const Pico::PClass* DerivedClass = PMacroDerivedObject::StaticClass();
    Runner.Expect(
        MacroClass != nullptr
            && MacroClass->GetName() == Pico::FName("PMacroObject")
            && MacroClass->GetSuperClass() == Pico::PObject::StaticClass(),
        "Class macros derive metadata name and superclass from C++ types");
    Runner.Expect(
        DerivedClass != nullptr
            && DerivedClass->GetSuperClass() == MacroClass
            && DerivedClass->GetProperties().empty(),
        "No-property macro defines derived class metadata without local properties");

    const Pico::PClass* InvalidMacroClass = PInvalidMacroObject::StaticClass();
    const Pico::PClass* InvalidDerivedClass = PInvalidMacroDerivedObject::StaticClass();
    Runner.Expect(
        InvalidMacroClass != nullptr
            && !InvalidMacroClass->IsMetadataValid()
            && InvalidMacroClass->GetMetadataError() == Pico::EClassMetadataError::InvalidProperty,
        "Class macros preserve invalid property metadata for diagnostics");
    Runner.Expect(
        InvalidDerivedClass != nullptr
            && InvalidDerivedClass->GetSuperClass() == InvalidMacroClass
            && !InvalidDerivedClass->IsMetadataValid()
            && InvalidDerivedClass->GetMetadataError() == Pico::EClassMetadataError::InvalidSuperClass,
        "Derived macro metadata propagates an invalid superclass");
    Runner.Expect(
        !PInvalidMacroObject::RegisterClass()
            && !PInvalidMacroDerivedObject::RegisterClass()
            && Pico::FClassRegistry::FindClass(Pico::FName("PInvalidMacroObject")) == nullptr
            && Pico::FClassRegistry::FindClass(Pico::FName("PInvalidMacroDerivedObject")) == nullptr,
        "Class registry rejects invalid macro metadata without losing its hierarchy");

    Pico::PObject* DynamicObject =
        Pico::NewObject(DerivedClass, nullptr, "MacroDynamicObject");
    Runner.Expect(
        DynamicObject != nullptr && DynamicObject->GetClass() == DerivedClass,
        "Class macros generate metadata-driven construction");

    const Pico::PProperty* MacroValueProperty =
        DerivedClass != nullptr
            ? DerivedClass->FindProperty(Pico::FName("MacroValue"))
            : nullptr;
    Pico::int32 MacroValue = 0;
    Runner.Expect(
        MacroValueProperty != nullptr
            && MacroValueProperty->GetValue(DynamicObject, MacroValue)
            && MacroValue == 42,
        "Property macro exposes a private inherited member");
    Runner.Expect(
        MacroValueProperty != nullptr
            && MacroValueProperty->SetValue(DynamicObject, Pico::int32 { 99 })
            && static_cast<PMacroDerivedObject*>(DynamicObject)->GetMacroValue() == 99,
        "Property macro preserves type-checked writes");
    const Pico::PFunction* MacroFunction =
        DerivedClass->FindFunction(Pico::FName("AddToMacroValue"));
    const std::array<Pico::FFunctionValue, 1> MacroArguments { Pico::int32 { 1 } };
    Pico::FFunctionValue MacroReturn;
    Runner.Expect(
        MacroFunction != nullptr
            && DynamicObject->ProcessEvent(MacroFunction, MacroArguments, &MacroReturn)
                == Pico::EFunctionInvokeResult::Success
            && std::get<Pico::int32>(MacroReturn) == 100,
        "Function macro registers a private-owner-compatible invocation thunk");

    if (DynamicObject != nullptr)
    {
        Pico::DestroyObject(DynamicObject);
    }
}

void TestObjectCreationAndIdentity(FTestRunner& Runner)
{
    PTestObject* Root = Pico::NewObject<PTestObject>(nullptr, "Root", Pico::EObjectFlags::Transient);
    Runner.Expect(Root != nullptr, "Template NewObject creates a registered object");
    if (Root == nullptr)
    {
        return;
    }

    Runner.Expect(Root->SawIdentityDuringConstruction(), "Object identity is available during construction");
    Runner.Expect(Root->DidPostInitProperties(), "Object registry calls PostInitProperties after registration");
    Runner.Expect(Root->IsA(PTestObject::StaticClass()) && Root->IsA(Pico::PObject::StaticClass()),
        "Object IsA follows its class hierarchy");
    Runner.Expect(Pico::HasAnyFlags(Root->GetFlags(), Pico::EObjectFlags::Transient),
        "Object stores its creation flags");
    Runner.Expect(Root->GetPathName() == "Root", "Root object path uses its local name");
    Runner.Expect(Pico::ResolveObject(Root->GetHandle()) == Root, "Object handle resolves to the live object");
    Runner.Expect(Pico::FindObject(nullptr, Pico::FName("Root")) == Root,
        "Object registry finds an object by outer and name");

    PTestDerivedObject* Child = Pico::NewObject<PTestDerivedObject>(Root, "Child");
    Runner.Expect(Child != nullptr, "NewObject creates a derived child object");
    if (Child == nullptr)
    {
        Pico::DestroyObject(Root);
        return;
    }

    Runner.Expect(Child->GetClass() == PTestDerivedObject::StaticClass(), "Derived object keeps its exact runtime class");
    Runner.Expect(Child->GetOuter() == Root, "Child object keeps its outer");
    Runner.Expect(Child->GetPathName() == "Root.Child", "Child path includes the outer path");

    Pico::PObject* DynamicObject = Pico::NewObject(PTestDerivedObject::StaticClass(), nullptr, "Dynamic");
    Runner.Expect(DynamicObject != nullptr && DynamicObject->GetClass() == PTestDerivedObject::StaticClass(),
        "Dynamic NewObject constructs from PClass metadata");

    Runner.Expect(Pico::NewObject<PTestObject>(nullptr, "Root") == nullptr,
        "NewObject rejects a duplicate name in the same outer");
    Runner.Expect(!Pico::DestroyObject(Root), "Object registry refuses to destroy an outer with live children");

    const Pico::FObjectHandle OldChildHandle = Child->GetHandle();
    Runner.Expect(Pico::DestroyObject(Child), "DestroyObject removes a leaf object");
    Runner.Expect(Pico::ResolveObject(OldChildHandle) == nullptr, "Destroyed object handle no longer resolves");

    PTestDerivedObject* Replacement = Pico::NewObject<PTestDerivedObject>(Root, "Child");
    Runner.Expect(Replacement != nullptr, "A destroyed object name can be reused");
    if (Replacement != nullptr)
    {
        Runner.Expect(Replacement->GetHandle() != OldChildHandle, "A reused object slot receives a new serial");
        Runner.Expect(Pico::ResolveObject(OldChildHandle) == nullptr, "Old handle stays invalid after slot reuse");
        Runner.Expect(Pico::DestroyObject(Replacement), "Replacement child can be destroyed");
    }

    Runner.Expect(Pico::DestroyObject(Root), "Outer can be destroyed after its children");
    if (DynamicObject != nullptr)
    {
        Runner.Expect(Pico::DestroyObject(DynamicObject), "Dynamically constructed object can be destroyed");
    }
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Explicit destruction leaves no live objects");

    const std::size_t ObjectCountBeforeFailure = Pico::FObjectRegistry::GetObjectCount();
    bool bCaughtPostInitFailure = false;
    try
    {
        Pico::NewObject<PThrowingPostInitObject>(nullptr, "ThrowingObject");
    }
    catch (const std::runtime_error&)
    {
        bCaughtPostInitFailure = true;
    }

    Runner.Expect(bCaughtPostInitFailure, "PostInitProperties failures propagate to the caller");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == ObjectCountBeforeFailure,
        "PostInitProperties failure rolls back the object and children it created");
    Runner.Expect(Pico::FindObject(nullptr, Pico::FName("ThrowingObject")) == nullptr,
        "PostInitProperties failure leaves no half-registered parent object");
}

void TestObjectNameIndexLifecycle(FTestRunner& Runner)
{
    PTestObject* OuterA = Pico::NewObject<PTestObject>(nullptr, "IndexOuterA");
    PTestObject* OuterB = Pico::NewObject<PTestObject>(nullptr, "IndexOuterB");
    PTestObject* ChildA = OuterA != nullptr
        ? Pico::NewObject<PTestObject>(OuterA, "SharedName") : nullptr;
    PTestObject* ChildB = OuterB != nullptr
        ? Pico::NewObject<PTestObject>(OuterB, "SharedName") : nullptr;
    Runner.Expect(ChildA != nullptr && ChildB != nullptr
            && Pico::FindObject(OuterA, Pico::FName("SharedName")) == ChildA
            && Pico::FindObject(OuterB, Pico::FName("SharedName")) == ChildB,
        "Object name index scopes identical names by stable Outer handle");

    Runner.Expect(ChildA != nullptr
            && Pico::RenameObject(ChildA, Pico::FName("RenamedChild"))
            && Pico::FindObject(OuterA, Pico::FName("SharedName")) == nullptr
            && Pico::FindObject(OuterA, Pico::FName("RenamedChild")) == ChildA,
        "Rename atomically moves the object name index entry");

    const Pico::FObjectHandle OldHandle = ChildA != nullptr
        ? ChildA->GetHandle() : Pico::FObjectHandle {};
    Runner.Expect(ChildA != nullptr && Pico::DestroyObject(ChildA)
            && Pico::FindObject(OuterA, Pico::FName("RenamedChild")) == nullptr,
        "Destroy removes the object name index entry");
    PTestObject* Replacement = OuterA != nullptr
        ? Pico::NewObject<PTestObject>(OuterA, "RenamedChild") : nullptr;
    Runner.Expect(Replacement != nullptr && Replacement->GetHandle() != OldHandle
            && Pico::FindObject(OuterA, Pico::FName("RenamedChild")) == Replacement,
        "A reused slot publishes only its new serial in the name index");
    std::string HierarchyError;
    Runner.Expect(Pico::FObjectRegistry::ValidateHierarchyIndex(&HierarchyError),
        "Hierarchy index replaces stale child serials after slot reuse");

    std::vector<PTestObject*> ChurnObjects;
    ChurnObjects.reserve(2000);
    for (std::size_t Index = 0; Index < 2000; ++Index)
        ChurnObjects.push_back(Pico::NewObject<PTestObject>(nullptr,
            "IndexChurn_" + std::to_string(Index)));
    bool bAllFound = true;
    for (std::size_t Index = 0; Index < ChurnObjects.size(); ++Index)
        bAllFound &= Pico::FindObject(nullptr,
            Pico::FName("IndexChurn_" + std::to_string(Index))) == ChurnObjects[Index];
    for (PTestObject* Object : ChurnObjects)
        if (Object != nullptr) Pico::DestroyObject(Object);
    std::string IndexError;
    Runner.Expect(bAllFound && Pico::FObjectRegistry::ValidateNameIndex(&IndexError),
        "Name index remains complete and free of stale entries after slot churn");

    if (Replacement != nullptr) Pico::DestroyObject(Replacement);
    if (ChildB != nullptr) Pico::DestroyObject(ChildB);
    if (OuterA != nullptr) Pico::DestroyObject(OuterA);
    if (OuterB != nullptr) Pico::DestroyObject(OuterB);
    Runner.Expect(Pico::FObjectRegistry::ValidateNameIndex(&IndexError),
        "Name index matches the registry after nested object cleanup");
}

void TestObjectHierarchyIndexLifecycle(FTestRunner& Runner)
{
    PTestObject* Root = Pico::NewObject<PTestObject>(nullptr, "HierarchyRoot");
    PTestObject* ChildA = Root != nullptr
        ? Pico::NewObject<PTestObject>(Root, "HierarchyChildA") : nullptr;
    PTestObject* ChildB = Root != nullptr
        ? Pico::NewObject<PTestObject>(Root, "HierarchyChildB") : nullptr;
    PTestObject* Grandchild = ChildA != nullptr
        ? Pico::NewObject<PTestObject>(ChildA, "HierarchyGrandchild") : nullptr;
    const Pico::FObjectHandle Handles[] = {
        Root != nullptr ? Root->GetHandle() : Pico::FObjectHandle {},
        ChildA != nullptr ? ChildA->GetHandle() : Pico::FObjectHandle {},
        ChildB != nullptr ? ChildB->GetHandle() : Pico::FObjectHandle {},
        Grandchild != nullptr ? Grandchild->GetHandle() : Pico::FObjectHandle {}};

    std::string HierarchyError;
    const Pico::FObjectHierarchyIndexStats PopulatedStats =
        Pico::FObjectRegistry::GetHierarchyIndexStats();
    Runner.Expect(
        Root != nullptr && ChildA != nullptr && ChildB != nullptr
            && Grandchild != nullptr
            && PopulatedStats.ParentEntryCount == 2
            && PopulatedStats.ChildRelationCount == 3
            && PopulatedStats.EstimatedStorageBytes > 0
            && Pico::FObjectRegistry::ValidateHierarchyIndex(&HierarchyError),
        "Hierarchy index records each direct Outer-child relationship");
    Runner.Expect(
        !Pico::DestroyObject(Root),
        "Hierarchy index prevents destroying an Outer with live children");

    Pico::DestroyObjectTree(Root);
    bool bAllHandlesExpired = true;
    for (Pico::FObjectHandle Handle : Handles)
        bAllHandlesExpired &= Pico::ResolveObject(Handle) == nullptr;
    const Pico::FObjectHierarchyIndexStats EmptyStats =
        Pico::FObjectRegistry::GetHierarchyIndexStats();
    Runner.Expect(
        bAllHandlesExpired
            && EmptyStats.ParentEntryCount == 0
            && EmptyStats.ChildRelationCount == 0
            && Pico::FObjectRegistry::ValidateHierarchyIndex(&HierarchyError),
        "Recursive destruction removes hierarchy entries child before parent");
}

void TestPropertyReflection(FTestRunner& Runner)
{
    const Pico::PClass* TestClass = PTestObject::StaticClass();
    const Pico::PProperty* HealthProperty = TestClass->FindProperty(Pico::FName("Health"));
    const Pico::PProperty* SpeedProperty = TestClass->FindProperty(Pico::FName("Speed"));
    const Pico::PProperty* AliveProperty = TestClass->FindProperty(Pico::FName("bAlive"));

    Runner.Expect(TestClass->GetProperties().size() == 3, "Class exposes its directly declared properties");
    Runner.Expect(HealthProperty != nullptr && HealthProperty->GetOwnerClass() == TestClass,
        "Property records its declaring class");
    Runner.Expect(SpeedProperty != nullptr && SpeedProperty->GetType() == Pico::EPropertyType::Float,
        "Property records its value type");
    Runner.Expect(AliveProperty != nullptr && AliveProperty->GetSize() == sizeof(bool),
        "Property records its native size");
    Runner.Expect(TestClass->FindProperty(Pico::FName("Missing")) == nullptr,
        "Missing property lookup returns null");

    PTestObject* Object = Pico::NewObject<PTestObject>(nullptr, "PropertyObject");
    Runner.Expect(Object != nullptr, "Object for property reflection is created");
    if (Object == nullptr)
    {
        return;
    }

    Pico::int32 Health = 0;
    float Speed = 0.0f;
    bool bAlive = false;
    Runner.Expect(HealthProperty != nullptr && HealthProperty->GetValue(Object, Health) && Health == 100,
        "Int32 property reads its instance value");
    Runner.Expect(SpeedProperty != nullptr && SpeedProperty->GetValue(Object, Speed) && Speed == 600.0f,
        "Float property reads its instance value");
    Runner.Expect(AliveProperty != nullptr && AliveProperty->GetValue(Object, bAlive) && bAlive,
        "Bool property reads its instance value");
    Runner.Expect(HealthProperty != nullptr && HealthProperty->SetValue(Object, Pico::int32 { 75 })
            && Object->GetHealth() == 75,
        "Property writes its instance value");
    Runner.Expect(HealthProperty != nullptr && HealthProperty->GetValuePtr<float>(Object) == nullptr,
        "Property rejects access through the wrong value type");

    PTestDerivedObject* Derived = Pico::NewObject<PTestDerivedObject>(nullptr, "DerivedPropertyObject");
    Runner.Expect(Derived != nullptr, "Derived object for inherited property reflection is created");
    if (Derived != nullptr)
    {
        const Pico::PProperty* InheritedHealth = Derived->GetClass()->FindProperty(Pico::FName("Health"));
        const Pico::PProperty* ScoreProperty = Derived->GetClass()->FindProperty(Pico::FName("Score"));
        Pico::int32 DerivedHealth = 0;
        Pico::int32 Score = 0;
        Runner.Expect(InheritedHealth == HealthProperty,
            "Derived class lookup finds a property declared by its superclass");
        Runner.Expect(InheritedHealth != nullptr
                && InheritedHealth->GetValue(Derived, DerivedHealth)
                && DerivedHealth == 100,
            "Superclass property reads from a derived instance");
        Runner.Expect(ScoreProperty != nullptr
                && ScoreProperty->GetValue(Derived, Score)
                && Score == Derived->GetScore(),
            "Derived class exposes its directly declared property");
        Runner.Expect(ScoreProperty != nullptr && ScoreProperty->GetValuePtr<Pico::int32>(Object) == nullptr,
            "Derived-only property rejects a base instance");
        Pico::DestroyObject(Derived);
    }

    Pico::DestroyObject(Object);
}

void TestFunctionReflection(FTestRunner& Runner)
{
    const Pico::PClass* TestClass = PTestObject::StaticClass();
    const Pico::PFunction* AddHealth = TestClass->FindFunction(Pico::FName("AddHealth"));
    const Pico::PFunction* IsHealthAtLeast =
        TestClass->FindFunction(Pico::FName("IsHealthAtLeast"));
    const Pico::PFunction* SetAlive = TestClass->FindFunction(Pico::FName("SetAlive"));
    const Pico::PFunction* EchoMacroObject =
        TestClass->FindFunction(Pico::FName("EchoMacroObject"));
    const Pico::PFunction* ThrowFromFunction =
        TestClass->FindFunction(Pico::FName("ThrowFromFunction"));
    const Pico::PFunction* ServerNotify =
        TestClass->FindFunction(Pico::FName("ServerNotify"));

    Runner.Expect(
        TestClass->GetFunctions().size() == 6
            && AddHealth != nullptr
            && AddHealth->GetOwnerClass() == TestClass
            && AddHealth->GetParameters().size() == 1
            && AddHealth->GetParameters()[0].Name == Pico::FName("Delta")
            && AddHealth->GetParameters()[0].Value.Type == Pico::EFunctionValueType::Int32
            && AddHealth->GetReturnValue().Type == Pico::EFunctionValueType::Int32,
        "Function metadata exposes owner, parameter, and return descriptors");
    Runner.Expect(
        IsHealthAtLeast != nullptr
            && IsHealthAtLeast->HasAnyFlags(Pico::EFunctionFlags::Const)
            && IsHealthAtLeast->HasAnyFlags(Pico::EFunctionFlags::Pure)
            && ServerNotify != nullptr
            && ServerNotify->HasAnyFlags(Pico::EFunctionFlags::Server)
            && ServerNotify->HasAnyFlags(Pico::EFunctionFlags::Reliable),
        "Function metadata records const, pure, and future RPC policy flags");
    Runner.Expect(
        PTestDerivedObject::StaticClass()->FindFunction(Pico::FName("AddHealth")) == AddHealth
            && PTestDerivedObject::StaticClass()->FindFunction(Pico::FName("MultiplyScore")) != nullptr,
        "Derived classes find inherited and directly declared reflected functions");

    PTestDerivedObject* Object = Pico::NewObject<PTestDerivedObject>(nullptr, "FunctionObject");
    PMacroObject* MacroObject = Pico::NewObject<PMacroObject>(nullptr, "FunctionMacroObject");
    PDelegateListener* WrongObject = Pico::NewObject<PDelegateListener>(nullptr, "WrongFunctionObject");
    Runner.Expect(Object != nullptr && MacroObject != nullptr && WrongObject != nullptr,
        "Function reflection test objects are created");
    if (Object == nullptr || MacroObject == nullptr || WrongObject == nullptr)
    {
        Pico::DestroyObject(Object);
        Pico::DestroyObject(MacroObject);
        Pico::DestroyObject(WrongObject);
        return;
    }

    Pico::FFunctionValue ReturnValue;
    const std::array<Pico::FFunctionValue, 1> AddArguments { Pico::int32 { 25 } };
    Runner.Expect(
        Object->ProcessEvent(AddHealth, AddArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::Success
            && std::get<Pico::int32>(ReturnValue) == 125
            && Object->GetHealth() == 125,
        "ProcessEvent invokes an inherited mutable function and returns its value");

    const std::array<Pico::FFunctionValue, 1> QueryArguments { Pico::int32 { 120 } };
    Runner.Expect(
        Object->ProcessEvent(IsHealthAtLeast, QueryArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::Success
            && std::get<bool>(ReturnValue),
        "ProcessEvent invokes a const pure function");

    const std::array<Pico::FFunctionValue, 1> SetAliveArguments { false };
    ReturnValue = Pico::int32 { 9 };
    Runner.Expect(
        Object->ProcessEvent(SetAlive, SetAliveArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::Success
            && std::holds_alternative<std::monostate>(ReturnValue)
            && !Object->IsAlive(),
        "Void reflected functions write an explicit empty return value");

    const std::array<Pico::FFunctionValue, 1> ObjectArguments {
        static_cast<Pico::PObject*>(MacroObject)
    };
    Runner.Expect(
        Object->ProcessEvent(EchoMacroObject, ObjectArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::Success
            && std::get<Pico::PObject*>(ReturnValue) == MacroObject,
        "Object parameters preserve their declared reflected class constraint");
    const std::array<Pico::FFunctionValue, 1> NullObjectArguments {
        static_cast<Pico::PObject*>(nullptr)
    };
    Runner.Expect(
        Object->ProcessEvent(EchoMacroObject, NullObjectArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::Success
            && std::get<Pico::PObject*>(ReturnValue) == nullptr,
        "Null is a valid reflected object argument");

    Runner.Expect(
        Object->ProcessEvent(AddHealth, {}, &ReturnValue)
                == Pico::EFunctionInvokeResult::ArgumentCountMismatch,
        "ProcessEvent rejects the wrong parameter count");
    const std::array<Pico::FFunctionValue, 1> WrongTypeArguments { 25.0f };
    Runner.Expect(
        Object->ProcessEvent(AddHealth, WrongTypeArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::ArgumentTypeMismatch,
        "ProcessEvent rejects a mismatched value type without coercion");
    const std::array<Pico::FFunctionValue, 1> WrongObjectArguments {
        static_cast<Pico::PObject*>(WrongObject)
    };
    Runner.Expect(
        Object->ProcessEvent(EchoMacroObject, WrongObjectArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::InvalidObjectArgument,
        "ProcessEvent rejects an object outside the parameter class hierarchy");
    Runner.Expect(
        Object->ProcessEvent(AddHealth, AddArguments, nullptr)
                == Pico::EFunctionInvokeResult::MissingReturnStorage,
        "Non-void reflected functions require return storage");
    Runner.Expect(
        MacroObject->ProcessEvent(AddHealth, AddArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::InvalidTarget
            && Object->ProcessEvent(nullptr) == Pico::EFunctionInvokeResult::InvalidFunction
            && const_cast<PTestObject*>(Pico::GetDefault<PTestObject>())->ProcessEvent(
                    AddHealth, AddArguments, &ReturnValue)
                == Pico::EFunctionInvokeResult::InvalidTarget,
        "ProcessEvent rejects wrong-class, null-function, and template targets");
    Runner.Expect(
        Object->ProcessEvent(ThrowFromFunction) == Pico::EFunctionInvokeResult::InvocationFailed
            && Pico::ResolveObject(Object->GetHandle()) == Object,
        "Function exceptions stop at the ProcessEvent boundary and leave the target alive");

    Pico::PFunction InvalidReliable = Pico::PFunction::Create<&PTestObject::AddHealth>(
        Pico::FName("InvalidReliable"),
        Pico::EFunctionFlags::Reliable,
        { Pico::FName("Delta") });
    Pico::PClass InvalidFunctionClass = Pico::PClass::Create<PTestObject>(
        Pico::FName("InvalidFunctionClass"),
        Pico::PObject::StaticClass(),
        sizeof(PTestObject),
        nullptr);
    Runner.Expect(
        !InvalidReliable.IsMetadataValid()
            && !InvalidFunctionClass.AddFunction(std::move(InvalidReliable))
            && InvalidFunctionClass.GetMetadataError() == Pico::EClassMetadataError::InvalidFunction,
        "Invalid RPC flags poison class metadata before registration");

    Pico::PClass WrongOwnerClass = Pico::PClass::Create<PDelegateListener>(
        Pico::FName("WrongFunctionOwner"),
        Pico::PObject::StaticClass(),
        sizeof(PDelegateListener),
        nullptr);
    Runner.Expect(
        !WrongOwnerClass.AddFunction(Pico::PFunction::Create<&PTestObject::SetAlive>(
            Pico::FName("ForeignFunction"),
            Pico::EFunctionFlags::Callable,
            { Pico::FName("bAlive") }))
            && WrongOwnerClass.GetMetadataError() == Pico::EClassMetadataError::InvalidFunction,
        "A class rejects function metadata created for another native owner");

    Pico::PClass ShadowingClass = Pico::PClass::Create<PTestDerivedObject>(
        Pico::FName("ShadowingFunctionClass"),
        PTestObject::StaticClass(),
        sizeof(PTestDerivedObject),
        nullptr);
    Runner.Expect(
        !ShadowingClass.AddFunction(Pico::PFunction::Create<&PTestDerivedObject::MultiplyScore>(
            Pico::FName("AddHealth"),
            Pico::EFunctionFlags::Callable,
            { Pico::FName("Factor") })),
        "The first function-reflection version rejects ambiguous inherited name shadowing");

    Pico::DestroyObject(WrongObject);
    Pico::DestroyObject(MacroObject);
    Pico::DestroyObject(Object);
}

void TestBeginDestroy(FTestRunner& Runner)
{
    GBeginDestroyOrder.clear();
    GHandlesResolvedDuringBeginDestroy = true;
    GChildCreationRejectedDuringBeginDestroy = false;

    PBeginDestroyObject* Root =
        Pico::NewObject<PBeginDestroyObject>(nullptr, "DestroyRoot");
    PBeginDestroyObject* Child =
        Root != nullptr ? Pico::NewObject<PBeginDestroyObject>(Root, "DestroyChild") : nullptr;
    const Pico::FObjectHandle RootHandle = Root != nullptr ? Root->GetHandle() : Pico::FObjectHandle {};
    const Pico::FObjectHandle ChildHandle = Child != nullptr ? Child->GetHandle() : Pico::FObjectHandle {};

    Runner.Expect(Root != nullptr && Child != nullptr, "BeginDestroy test object tree is created");
    Pico::FObjectRegistry::DestroyObjectTree(Root);

    Runner.Expect(
        GBeginDestroyOrder == std::vector<std::string> { "DestroyChild", "DestroyRoot" },
        "BeginDestroy runs exactly once in child-before-parent order");
    Runner.Expect(GHandlesResolvedDuringBeginDestroy,
        "Object handles remain resolvable while BeginDestroy runs");
    Runner.Expect(GChildCreationRejectedDuringBeginDestroy,
        "An object being destroyed cannot receive new child objects");
    Runner.Expect(Pico::ResolveObject(RootHandle) == nullptr && Pico::ResolveObject(ChildHandle) == nullptr,
        "Object tree handles are invalid after destruction");

    GSelfDestroyRejected = false;
    PBeginDestroyObject* SelfDestroy =
        Pico::NewObject<PBeginDestroyObject>(nullptr, "SelfDestroy");
    Runner.Expect(
        SelfDestroy != nullptr
            && Pico::DestroyObject(SelfDestroy)
            && GSelfDestroyRejected,
        "DestroyObject rejects recursive self-destruction from BeginDestroy");

    GSiblingDestroySucceeded = false;
    PBeginDestroyObject* SiblingRoot =
        Pico::NewObject<PBeginDestroyObject>(nullptr, "SiblingRoot");
    PBeginDestroyObject* SiblingDestroyer = SiblingRoot != nullptr
        ? Pico::NewObject<PBeginDestroyObject>(SiblingRoot, "SiblingDestroyer")
        : nullptr;
    PBeginDestroyObject* SiblingVictim = SiblingRoot != nullptr
        ? Pico::NewObject<PBeginDestroyObject>(SiblingRoot, "SiblingVictim")
        : nullptr;
    GSiblingHandle = SiblingVictim != nullptr
        ? SiblingVictim->GetHandle()
        : Pico::FObjectHandle {};
    Runner.Expect(
        SiblingDestroyer != nullptr && SiblingVictim != nullptr,
        "Sibling-destruction test tree is created");
    Pico::DestroyObjectTree(SiblingRoot);
    Runner.Expect(
        GSiblingDestroySucceeded
            && Pico::ResolveObject(GSiblingHandle) == nullptr,
        "Handle-based tree traversal tolerates a sibling destroyed from BeginDestroy");

    GRootCreationSucceeded = false;
    PBeginDestroyObject* RootCreator =
        Pico::NewObject<PBeginDestroyObject>(nullptr, "RootCreator");
    Runner.Expect(
        RootCreator != nullptr
            && Pico::DestroyObject(RootCreator)
            && GRootCreationSucceeded
            && Pico::ResolveObject(GCreatedRootHandle) != nullptr,
        "DestroyObject reacquires its slot after BeginDestroy grows the registry");
    Pico::DestroyObject(Pico::ResolveObject(GCreatedRootHandle));

    GShutdownCreationRejected = false;
    Pico::NewObject<PBeginDestroyObject>(nullptr, "ShutdownCreator");
    Pico::FObjectRegistry::DestroyAllObjects();
    Runner.Expect(
        GShutdownCreationRejected && Pico::FObjectRegistry::GetObjectCount() == 0,
        "Registry shutdown rejects objects created from BeginDestroy callbacks");
}

void TestReflectionObservation(FTestRunner& Runner)
{
    const std::vector<const Pico::PClass*> Classes = Pico::FClassRegistry::GetClasses();
    Runner.Expect(
        Classes.size() == Pico::FClassRegistry::GetClassCount(),
        "Class registry exposes a complete read-only snapshot");
    Runner.Expect(
        std::is_sorted(
            Classes.begin(),
            Classes.end(),
            [](const Pico::PClass* Left, const Pico::PClass* Right)
            {
                return Left->GetName().ToString() < Right->GetName().ToString();
            }),
        "Class registry snapshot has deterministic name order");
    Runner.Expect(
        std::find(Classes.begin(), Classes.end(), PTestDerivedObject::StaticClass()) != Classes.end(),
        "Class registry snapshot includes registered derived classes");

    PTestObject* Root = Pico::NewObject<PTestObject>(nullptr, "ObservedRoot");
    PTestDerivedObject* Child =
        Root != nullptr ? Pico::NewObject<PTestDerivedObject>(Root, "ObservedChild") : nullptr;
    const std::vector<Pico::PObject*> Objects = Pico::FObjectRegistry::GetObjects();
    Runner.Expect(
        Root != nullptr && Child != nullptr
            && std::find(Objects.begin(), Objects.end(), Root) != Objects.end()
            && std::find(Objects.begin(), Objects.end(), Child) != Objects.end(),
        "Object registry snapshot includes live root and child objects");

    const std::string ClassDump = Pico::DumpClass(PTestDerivedObject::StaticClass());
    const std::vector<const Pico::PFunction*> ReflectedFunctions =
        Pico::GetAllFunctions(PTestDerivedObject::StaticClass());
    Runner.Expect(
        ReflectedFunctions.size() == 7
            && ReflectedFunctions.front()->GetName() == Pico::FName("AddHealth")
            && ReflectedFunctions.back()->GetName() == Pico::FName("MultiplyScore"),
        "Function observation returns inherited metadata in deterministic base-first order");
    Runner.Expect(
        ClassDump.find("Class: PTestDerivedObject") != std::string::npos
            && ClassDump.find("Health") != std::string::npos
            && ClassDump.find("Score") != std::string::npos
            && ClassDump.find("Int32 AddHealth(Int32 Delta)") != std::string::npos
            && ClassDump.find("Int32 MultiplyScore(Int32 Factor)") != std::string::npos,
        "Class dump includes inherited properties and reflected function signatures");

    const std::string ObjectDump = Pico::DumpObject(Child);
    Runner.Expect(
        ObjectDump.find("Object: ObservedRoot.ObservedChild") != std::string::npos
            && ObjectDump.find("Health = 100") != std::string::npos
            && ObjectDump.find("Score = 10") != std::string::npos,
        "Object dump exposes identity and reflected values");

    if (Child != nullptr)
    {
        Pico::DestroyObject(Child);
    }
    if (Root != nullptr)
    {
        Pico::DestroyObject(Root);
    }
}

void TestObjectSerialization(FTestRunner& Runner)
{
    PTestDerivedObject* Source = Pico::NewObject<PTestDerivedObject>(
        nullptr,
        "SerializedObject",
        Pico::EObjectFlags::Transient);
    Runner.Expect(Source != nullptr, "Object for serialization is created");
    if (Source == nullptr)
    {
        return;
    }

    const Pico::PClass* Class = Source->GetClass();
    const Pico::PProperty* HealthProperty = Class->FindProperty(Pico::FName("Health"));
    const Pico::PProperty* SpeedProperty = Class->FindProperty(Pico::FName("Speed"));
    const Pico::PProperty* AliveProperty = Class->FindProperty(Pico::FName("bAlive"));
    const Pico::PProperty* ScoreProperty = Class->FindProperty(Pico::FName("Score"));
    const bool bValuesChanged =
        HealthProperty != nullptr && HealthProperty->SetValue(Source, Pico::int32 { 75 })
        && SpeedProperty != nullptr && SpeedProperty->SetValue(Source, 321.5f)
        && AliveProperty != nullptr && AliveProperty->SetValue(Source, false)
        && ScoreProperty != nullptr && ScoreProperty->SetValue(Source, Pico::int32 { 9001 });
    Runner.Expect(bValuesChanged, "Reflected values are changed before saving");

    Pico::FMemoryWriter Writer;
    Runner.Expect(Pico::SaveObject(Writer, Source), "Object saves to a memory archive");
    const std::vector<Pico::uint8>& Data = Writer.GetData();
    const std::string ClassName = "PTestDerivedObject";
    const auto ClassNamePosition = std::search(
        Data.begin(),
        Data.end(),
        ClassName.begin(),
        ClassName.end());
    Runner.Expect(ClassNamePosition != Data.end(), "Archive stores class names as text instead of FName indices");

    Runner.Expect(Pico::DestroyObject(Source), "Source object is destroyed before loading");

    Pico::EObjectSerializationError SerializationError = Pico::EObjectSerializationError::InvalidArchive;
    Pico::FMemoryReader Reader(Data);
    Pico::PObject* LoadedBase = Pico::LoadObject(Reader, nullptr, &SerializationError);
    Runner.Expect(
        LoadedBase != nullptr && SerializationError == Pico::EObjectSerializationError::None,
        "Object loads from a memory archive without an error");
    Runner.Expect(LoadedBase != nullptr && LoadedBase->GetClass() == PTestDerivedObject::StaticClass(),
        "Loading resolves the saved class name through the class registry");
    Runner.Expect(LoadedBase != nullptr && LoadedBase->GetName() == Pico::FName("SerializedObject"),
        "Loading restores the object name");
    Runner.Expect(LoadedBase != nullptr
            && Pico::HasAnyFlags(LoadedBase->GetFlags(), Pico::EObjectFlags::Transient),
        "Loading restores object flags");

    if (LoadedBase != nullptr)
    {
        Pico::int32 Health = 0;
        float Speed = 0.0f;
        bool bAlive = true;
        Pico::int32 Score = 0;
        const bool bValuesRestored =
            HealthProperty->GetValue(LoadedBase, Health) && Health == 75
            && SpeedProperty->GetValue(LoadedBase, Speed) && Speed == 321.5f
            && AliveProperty->GetValue(LoadedBase, bAlive) && !bAlive
            && ScoreProperty->GetValue(LoadedBase, Score) && Score == 9001;
        Runner.Expect(bValuesRestored, "Loading restores inherited and directly declared reflected properties");
        const auto* LoadedObject = static_cast<const PTestDerivedObject*>(LoadedBase);
        Runner.Expect(
            LoadedObject->DidPostLoad() && LoadedObject->GetHealthSeenInPostLoad() == 75,
            "PostLoad runs after serialized property values are applied");
    }

    const std::filesystem::path FilePath =
        std::filesystem::temp_directory_path() / "PicoObjectSerializationTest.pobj";
    Runner.Expect(LoadedBase != nullptr && Pico::SaveObjectToFile(FilePath, LoadedBase, &SerializationError),
        "Object saves to a persistent file");
    if (LoadedBase != nullptr)
    {
        ScoreProperty->SetValue(LoadedBase, Pico::int32 { 1234 });
    }
    Runner.Expect(LoadedBase != nullptr && Pico::SaveObjectToFile(FilePath, LoadedBase, &SerializationError),
        "Saving again safely replaces an existing persistent file");
    if (LoadedBase != nullptr)
    {
        Pico::DestroyObject(LoadedBase);
    }

    std::vector<Pico::uint8> Version1Data = Data;
    Version1Data[4] = 1;
    Version1Data[5] = 0;
    Version1Data[6] = 0;
    Version1Data[7] = 0;
    Pico::FMemoryReader Version1Reader(Version1Data);
    Pico::PObject* Version1Object = Pico::LoadObject(Version1Reader, nullptr, &SerializationError);
    Runner.Expect(
        Version1Object != nullptr && SerializationError == Pico::EObjectSerializationError::None,
        "Version 3 reader remains compatible with version 1 scalar archives");
    if (Version1Object != nullptr)
    {
        Pico::int32 Version1Score = 0;
        Runner.Expect(
            ScoreProperty->GetValue(Version1Object, Version1Score) && Version1Score == 9001,
            "Version 1 compatibility restores scalar property values");
        Pico::DestroyObject(Version1Object);
    }

    Pico::PObject* FileLoadedObject = Pico::LoadObjectFromFile(FilePath, nullptr, &SerializationError);
    Runner.Expect(FileLoadedObject != nullptr, "Object loads from a persistent file");
    if (FileLoadedObject != nullptr)
    {
        Pico::int32 Score = 0;
        Runner.Expect(
            ScoreProperty->GetValue(FileLoadedObject, Score) && Score == 1234,
            "File replacement preserves the most recently saved values");
        Pico::DestroyObject(FileLoadedObject);
    }
    Runner.Expect(
        !std::filesystem::exists(std::filesystem::path(FilePath).concat(".tmp"))
            && !std::filesystem::exists(std::filesystem::path(FilePath).concat(".bak")),
        "Successful file replacement leaves no temporary files");

    {
        std::ofstream TrailingFile(FilePath, std::ios::binary | std::ios::app);
        TrailingFile.put(static_cast<char>(0x7f));
    }
    Runner.Expect(
        Pico::LoadObjectFromFile(FilePath, nullptr, &SerializationError) == nullptr
            && SerializationError == Pico::EObjectSerializationError::TrailingData,
        "File loading reports unexpected trailing data");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0,
        "Trailing file data rolls back the loaded object");

    std::error_code ErrorCode;
    std::filesystem::remove(FilePath, ErrorCode);

    std::vector<Pico::uint8> TruncatedData = Data;
    if (!TruncatedData.empty())
    {
        TruncatedData.pop_back();
    }
    const std::size_t ObjectCountBeforeFailure = Pico::FObjectRegistry::GetObjectCount();
    Pico::FMemoryReader TruncatedReader(TruncatedData);
    Runner.Expect(
        Pico::LoadObject(TruncatedReader, nullptr, &SerializationError) == nullptr
            && SerializationError == Pico::EObjectSerializationError::InvalidArchive,
        "Loading identifies a truncated archive");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == ObjectCountBeforeFailure,
        "A malformed archive leaves no half-loaded object");

    std::vector<Pico::uint8> UnsupportedVersionData = Data;
    UnsupportedVersionData[4] = 99;
    UnsupportedVersionData[5] = 0;
    UnsupportedVersionData[6] = 0;
    UnsupportedVersionData[7] = 0;
    Pico::FMemoryReader UnsupportedVersionReader(UnsupportedVersionData);
    Runner.Expect(
        Pico::LoadObject(UnsupportedVersionReader, nullptr, &SerializationError) == nullptr
            && SerializationError == Pico::EObjectSerializationError::UnsupportedVersion,
        "Loading reports an unsupported format version");

    std::vector<Pico::uint8> MissingClassData = Data;
    const auto MissingClassPosition = std::search(
        MissingClassData.begin(),
        MissingClassData.end(),
        ClassName.begin(),
        ClassName.end());
    if (MissingClassPosition != MissingClassData.end())
    {
        *MissingClassPosition = static_cast<Pico::uint8>('Q');
    }
    Pico::FMemoryReader MissingClassReader(MissingClassData);
    Runner.Expect(
        Pico::LoadObject(MissingClassReader, nullptr, &SerializationError) == nullptr
            && SerializationError == Pico::EObjectSerializationError::ClassNotFound,
        "Loading reports a class name that is no longer registered");

    std::vector<Pico::uint8> RemovedPropertyData = Data;
    const std::string ScoreName = "Score";
    const std::string RemovedPropertyName = "GoneX";
    const auto RemovedPropertyPosition = std::search(
        RemovedPropertyData.begin(),
        RemovedPropertyData.end(),
        ScoreName.begin(),
        ScoreName.end());
    if (RemovedPropertyPosition != RemovedPropertyData.end())
    {
        std::copy(RemovedPropertyName.begin(), RemovedPropertyName.end(), RemovedPropertyPosition);
    }
    Pico::FMemoryReader RemovedPropertyReader(RemovedPropertyData);
    Pico::PObject* RemovedPropertyObject =
        Pico::LoadObject(RemovedPropertyReader, nullptr, &SerializationError);
    Pico::int32 RemovedPropertyScore = 0;
    Runner.Expect(
        RemovedPropertyObject != nullptr
            && ScoreProperty->GetValue(RemovedPropertyObject, RemovedPropertyScore)
            && RemovedPropertyScore == 10,
        "Loading ignores a serialized property that no longer exists");
    if (RemovedPropertyObject != nullptr)
    {
        Pico::DestroyObject(RemovedPropertyObject);
    }

    std::vector<Pico::uint8> ChangedTypeData = Data;
    const auto ChangedTypePosition = std::search(
        ChangedTypeData.begin(),
        ChangedTypeData.end(),
        ScoreName.begin(),
        ScoreName.end());
    if (ChangedTypePosition != ChangedTypeData.end())
    {
        *(ChangedTypePosition + ScoreName.size()) = static_cast<Pico::uint8>(Pico::EPropertyType::Float);
    }
    Pico::FMemoryReader ChangedTypeReader(ChangedTypeData);
    Runner.Expect(
        Pico::LoadObject(ChangedTypeReader, nullptr, &SerializationError) == nullptr
            && SerializationError == Pico::EObjectSerializationError::PropertyTypeMismatch,
        "Loading rejects a known property whose reflected type changed");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == ObjectCountBeforeFailure,
        "A property type mismatch rolls back the newly created object");

    Pico::FMemoryReader FirstDuplicateReader(Data);
    Pico::PObject* FirstDuplicateObject =
        Pico::LoadObject(FirstDuplicateReader, nullptr, &SerializationError);
    Pico::FMemoryReader SecondDuplicateReader(Data);
    Runner.Expect(
        FirstDuplicateObject != nullptr
            && Pico::LoadObject(SecondDuplicateReader, nullptr, &SerializationError) == nullptr
            && SerializationError == Pico::EObjectSerializationError::ObjectCreationFailed,
        "Loading reports an object name collision");
    if (FirstDuplicateObject != nullptr)
    {
        Pico::DestroyObject(FirstDuplicateObject);
    }

    PThrowingPostLoadObject* ThrowingSource =
        Pico::NewObject<PThrowingPostLoadObject>(nullptr, "ThrowingPostLoadObject");
    Pico::FMemoryWriter ThrowingWriter;
    Runner.Expect(
        ThrowingSource != nullptr && Pico::SaveObject(ThrowingWriter, ThrowingSource, &SerializationError),
        "PostLoad rollback fixture saves successfully");
    if (ThrowingSource != nullptr)
    {
        Pico::DestroyObject(ThrowingSource);
    }
    const std::size_t CountBeforePostLoadFailure = Pico::FObjectRegistry::GetObjectCount();
    Pico::FMemoryReader ThrowingReader(ThrowingWriter.GetData());
    Runner.Expect(
        Pico::LoadObject(ThrowingReader, nullptr, &SerializationError) == nullptr
            && SerializationError == Pico::EObjectSerializationError::PostLoadFailed,
        "Loading reports a PostLoad failure");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == CountBeforePostLoadFailure,
        "PostLoad failure rolls back the object and children it created");
}

void TestAssetPathSerialization(FTestRunner& Runner)
{
    Runner.Expect(
        PAssetReferenceObject::RegisterClass(),
        "An object class can register a reflected AssetPath property");
    const Pico::PClass* AssetReferenceClass = PAssetReferenceObject::StaticClass();
    const Pico::PProperty* TransientDefaultProperty =
        AssetReferenceClass->FindProperty(Pico::FName("TransientValue"));
    PAssetReferenceObject* AssetReferenceDefault =
        Pico::GetMutableDefault<PAssetReferenceObject>();
    if (TransientDefaultProperty != nullptr && AssetReferenceDefault != nullptr)
    {
        TransientDefaultProperty->SetValue(AssetReferenceDefault, Pico::int32 { 99 });
    }
    PAssetReferenceObject* Source =
        Pico::NewObject<PAssetReferenceObject>(nullptr, "AssetReference");
    const Pico::PProperty* Property = Source != nullptr
        ? Source->GetClass()->FindProperty(Pico::FName("AssetPath"))
        : nullptr;
    const Pico::PProperty* TransientProperty = Source != nullptr
        ? Source->GetClass()->FindProperty(Pico::FName("TransientValue"))
        : nullptr;
    Pico::FAssetPath AssetPath;
    const bool bPathCreated = Pico::FAssetPath::TryParse(
        "/Game/Meshes/Robot.pmesh",
        AssetPath);
    Runner.Expect(
        Source != nullptr
            && Property != nullptr
            && Property->GetType() == Pico::EPropertyType::AssetPath
            && Property->GetAssetReferenceType()
                == Pico::EAssetReferenceType::StaticMesh
            && Property->HasAnyFlags(Pico::EPropertyFlags::Editable)
            && Property->HasAnyFlags(Pico::EPropertyFlags::Serializable)
            && TransientProperty != nullptr
            && TransientProperty->HasAnyFlags(Pico::EPropertyFlags::Transient)
            && !TransientProperty->HasAnyFlags(Pico::EPropertyFlags::Serializable)
            && Source->GetTransientValue() == 7
            && bPathCreated
            && Property->SetValue(Source, AssetPath)
            && TransientProperty->SetValue(Source, Pico::int32 {99}),
        "Reflection exposes typed flags and asset-reference metadata");
    if (TransientDefaultProperty != nullptr && AssetReferenceDefault != nullptr)
    {
        TransientDefaultProperty->SetValue(AssetReferenceDefault, Pico::int32 { 7 });
    }
    if (Source == nullptr || Property == nullptr || !bPathCreated)
    {
        if (Source != nullptr)
        {
            Pico::DestroyObject(Source);
        }
        return;
    }

    Pico::FMemoryWriter Writer;
    Runner.Expect(
        Pico::SaveObject(Writer, Source),
        "An AssetPath property serializes through the generic object archive");
    const std::vector<Pico::uint8> Data = Writer.GetData();
    Pico::DestroyObject(Source);

    Pico::EObjectSerializationError Error = Pico::EObjectSerializationError::InvalidArchive;
    Pico::FMemoryReader Reader(Data);
    Pico::PObject* Loaded = Pico::LoadObject(Reader, nullptr, &Error);
    Pico::FAssetPath LoadedPath;
    Runner.Expect(
        Loaded != nullptr
            && Error == Pico::EObjectSerializationError::None
            && Property->GetValue(Loaded, LoadedPath)
            && LoadedPath == AssetPath
            && static_cast<PAssetReferenceObject*>(Loaded)->GetTransientValue() == 7,
        "Object format version 3 restores serializable values and skips transient state");
    Runner.Expect(
        Loaded != nullptr
            && Pico::DumpObject(Loaded).find("/Game/Meshes/Robot.pmesh")
                != std::string::npos,
        "Reflection diagnostics display AssetPath values");
    if (Loaded != nullptr)
    {
        Pico::DestroyObject(Loaded);
    }

    std::vector<Pico::uint8> PretendVersion2 = Data;
    PretendVersion2[4] = 2;
    PretendVersion2[5] = 0;
    PretendVersion2[6] = 0;
    PretendVersion2[7] = 0;
    Pico::FMemoryReader Version2Reader(PretendVersion2);
    Runner.Expect(
        Pico::LoadObject(Version2Reader, nullptr, &Error) == nullptr
            && Error == Pico::EObjectSerializationError::InvalidArchive,
        "Older object format versions reject AssetPath payloads");
}

void TestShutdownAndReinitialize(FTestRunner& Runner)
{
    PTestObject* Root = Pico::NewObject<PTestObject>(nullptr, "ShutdownRoot");
    PTestDerivedObject* Child = Root != nullptr
        ? Pico::NewObject<PTestDerivedObject>(Root, "ShutdownChild")
        : nullptr;
    const Pico::FObjectHandle OldHandle = Child != nullptr ? Child->GetHandle() : Pico::FObjectHandle {};

    Pico::PObjectSystem::Shutdown();
    Runner.Expect(!Pico::PObjectSystem::IsInitialized(), "Object system reports shutdown state");
    Runner.Expect(Pico::FObjectRegistry::GetObjectCount() == 0, "Object system shutdown destroys all objects");
    Runner.Expect(Pico::FClassRegistry::GetClassCount() == 0, "Object system shutdown clears class registration");
    Runner.Expect(Pico::ResolveObject(OldHandle) == nullptr, "Handles remain invalid after system shutdown");
    std::string HierarchyError;
    Runner.Expect(
        Pico::FObjectRegistry::GetHierarchyIndexStats().ChildRelationCount == 0
            && Pico::FObjectRegistry::ValidateHierarchyIndex(&HierarchyError),
        "Object system shutdown clears the hierarchy index");

    Runner.Expect(Pico::PObjectSystem::Init(), "Object system can initialize again after shutdown");
    Runner.Expect(Pico::FClassRegistry::GetClassCount() == 1, "Reinitialization restores only intrinsic classes");
    Runner.Expect(Pico::FObjectRegistry::ValidateHierarchyIndex(&HierarchyError),
        "Object system restart begins with a consistent hierarchy index");
    Runner.Expect(PMacroObject::RegisterClass() && PMacroDerivedObject::RegisterClass(),
        "Macro-declared classes can register again after object-system restart");

    PMacroDerivedObject* MacroObject =
        Pico::NewObject<PMacroDerivedObject>(nullptr, "MacroAfterRestart");
    Runner.Expect(MacroObject != nullptr && MacroObject->GetMacroValue() == 42,
        "Macro-generated construction remains valid after object-system restart");
    if (MacroObject != nullptr)
    {
        Pico::DestroyObject(MacroObject);
    }
}

void TestObjectStorageCompaction(FTestRunner& Runner)
{
    PTestObject* Root = Pico::NewObject<PTestObject>(nullptr, "CompactRoot");
    PTestDerivedObject* Child = Root != nullptr
        ? Pico::NewObject<PTestDerivedObject>(Root, "CompactChild") : nullptr;
    const Pico::FObjectHandle RootHandle = Root != nullptr
        ? Root->GetHandle() : Pico::FObjectHandle {};
    const Pico::FObjectHandle ChildHandle = Child != nullptr
        ? Child->GetHandle() : Pico::FObjectHandle {};
    std::string Error;
    const bool bCompacted = Pico::FObjectRegistry::CompactStorage(&Error);
    Runner.Expect(bCompacted && Error.empty()
            && Pico::ResolveObject(RootHandle) == Root
            && Pico::ResolveObject(ChildHandle) == Child
            && Pico::FObjectRegistry::ValidateNameIndex(&Error)
            && Pico::FObjectRegistry::ValidateHierarchyIndex(&Error),
        "Safe-point compaction preserves handles and registry indexes");
    if (Root != nullptr) Pico::DestroyObjectTree(Root);
}
}

int main()
{
    FTestRunner Runner;
    Runner.Expect(Pico::PObjectSystem::Init(), "Object system initializes");

    if (Pico::PObjectSystem::IsInitialized())
    {
        TestClassRegistry(Runner);
        TestClassDefaultObjects(Runner);
        TestObjectDelegates(Runner);
        TestReflectionMacros(Runner);
        TestObjectCreationAndIdentity(Runner);
        TestObjectNameIndexLifecycle(Runner);
        TestObjectHierarchyIndexLifecycle(Runner);
        TestPropertyReflection(Runner);
        TestFunctionReflection(Runner);
        TestBeginDestroy(Runner);
        TestReflectionObservation(Runner);
        TestAssetPathSerialization(Runner);
        TestObjectSerialization(Runner);
        TestObjectStorageCompaction(Runner);
        TestShutdownAndReinitialize(Runner);
        Pico::PObjectSystem::Shutdown();
    }

    return Runner.Finish();
}
