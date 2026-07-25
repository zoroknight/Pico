#include "Pico/Samples/DemoCharacter.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Property.h"

#include <cstddef>

namespace Pico
{
const PClass* PDemoCharacter::StaticClass()
{
    static PClass Class(
        FName("PDemoCharacter"),
        PObject::StaticClass(),
        sizeof(PDemoCharacter),
        &PDemoCharacter::ConstructInstance);
    static const bool bPropertiesAdded = AddProperties(Class);
    (void)bPropertiesAdded;
    return &Class;
}

bool PDemoCharacter::RegisterClass()
{
    return FClassRegistry::RegisterClass(StaticClass());
}

int32 PDemoCharacter::GetHealth() const
{
    return Health;
}

float PDemoCharacter::GetMoveSpeed() const
{
    return MoveSpeed;
}

bool PDemoCharacter::IsAlive() const
{
    return bAlive;
}

int32 PDemoCharacter::GetHealthSeenInPostLoad() const
{
    return HealthSeenInPostLoad;
}

PDemoCharacter::PDemoCharacter(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

void PDemoCharacter::PostLoad()
{
    HealthSeenInPostLoad = Health;
}

FObjectPtr PDemoCharacter::ConstructInstance(const FObjectConstructionParams& Params)
{
    return FObjectPtr(new PDemoCharacter(Params));
}

bool PDemoCharacter::AddProperties(PClass& Class)
{
    return Class.AddProperty(PProperty(
               FName("Health"),
               EPropertyType::Int32,
               offsetof(PDemoCharacter, Health),
               sizeof(Health)))
        && Class.AddProperty(PProperty(
            FName("MoveSpeed"),
            EPropertyType::Float,
            offsetof(PDemoCharacter, MoveSpeed),
            sizeof(MoveSpeed)))
        && Class.AddProperty(PProperty(
            FName("bAlive"),
            EPropertyType::Bool,
            offsetof(PDemoCharacter, bAlive),
            sizeof(bAlive)));
}
}
