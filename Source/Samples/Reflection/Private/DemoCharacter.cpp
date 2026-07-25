#include "Pico/Samples/DemoCharacter.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Property.h"

#include <utility>
#include <vector>

namespace Pico
{
const PClass* PDemoCharacter::StaticClass()
{
    static PClass Class = PClass::Create<PDemoCharacter>(
        FName("PDemoCharacter"),
        PObject::StaticClass(),
        sizeof(PDemoCharacter),
        &PDemoCharacter::ConstructInstance);
    static const bool bPropertiesAdded = AddProperties(Class);
    if (!bPropertiesAdded)
    {
        return nullptr;
    }
    return &Class;
}

bool PDemoCharacter::RegisterClass()
{
    const PClass* Class = StaticClass();
    return Class != nullptr && FClassRegistry::RegisterClass(Class);
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
    std::vector<PProperty> Properties;
    Properties.push_back(PProperty::Create<&PDemoCharacter::Health>(FName("Health")));
    Properties.push_back(PProperty::Create<&PDemoCharacter::MoveSpeed>(FName("MoveSpeed")));
    Properties.push_back(PProperty::Create<&PDemoCharacter::bAlive>(FName("bAlive")));
    return Class.AddProperties(std::move(Properties));
}
}
