#include "Pico/Object/ReferenceCollector.h"

#include "Pico/Object/Object.h"

namespace Pico
{
void FReferenceCollector::AddReferencedObject(PObject* Object)
{
    if (Object != nullptr)
    {
        References.push_back(Object->GetHandle());
    }
}
}
