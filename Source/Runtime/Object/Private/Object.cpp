#include "Pico/Object/Object.h"

#include "Pico/Object/Class.h"

namespace Pico
{
void* PObject::operator new(std::size_t Size)
{
    return ::operator new(Size);
}

void PObject::operator delete(void* Memory) noexcept
{
    ::operator delete(Memory);
}

PObject::PObject(const FObjectConstructionParams& Params)
    : ClassPrivate(Params.Class)
    , OuterPrivate(Params.Outer)
    , NamePrivate(Params.Name)
    , FlagsPrivate(Params.Flags)
{
}

const PClass* PObject::StaticClass()
{
    static const PClass Class = PClass::Create<PObject>(
        FName("PObject"),
        nullptr,
        sizeof(PObject),
        &PObject::ConstructInstance);
    return &Class;
}

FObjectPtr PObject::ConstructInstance(const FObjectConstructionParams& Params)
{
    return FObjectPtr(new PObject(Params));
}

void FObjectDeleter::operator()(PObject* Object) const
{
    delete Object;
}

const PClass* PObject::GetClass() const
{
    return ClassPrivate;
}

PObject* PObject::GetOuter() const
{
    return OuterPrivate;
}

FName PObject::GetName() const
{
    return NamePrivate;
}

EObjectFlags PObject::GetFlags() const
{
    return FlagsPrivate;
}

FObjectHandle PObject::GetHandle() const
{
    return HandlePrivate;
}

std::string PObject::GetPathName() const
{
    const std::string LocalName = NamePrivate.ToString();
    if (OuterPrivate == nullptr)
    {
        return LocalName;
    }

    const std::string OuterPath = OuterPrivate->GetPathName();
    return OuterPath.empty() ? LocalName : OuterPath + "." + LocalName;
}

bool PObject::IsA(const PClass* Class) const
{
    return ClassPrivate != nullptr && ClassPrivate->IsChildOf(Class);
}

bool PObject::IsBeginningDestroy() const
{
    return LifecycleState != ELifecycleState::Alive;
}

void PObject::PostEditChangeProperty(const FPropertyChangedEvent&)
{
}

void PObject::PostInitProperties()
{
}

void PObject::PostLoad()
{
}

void PObject::BeginDestroy()
{
}
}
