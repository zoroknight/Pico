#include "Pico/Object/ObjectSystem.h"

#include "Pico/Core/Log.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectRegistry.h"

namespace Pico
{
namespace
{
bool GObjectSystemInitialized = false;
}

bool PObjectSystem::Init()
{
    if (GObjectSystemInitialized)
    {
        return true;
    }

    ResetGarbageCollectionRequests();
    FClassRegistry::Clear();
    if (!FClassRegistry::RegisterClass(PObject::StaticClass()))
    {
        FClassRegistry::Clear();
        return false;
    }

    GObjectSystemInitialized = true;
    PICO_LOG(LogObject, Info, "Object system initialized with {} intrinsic class", FClassRegistry::GetClassCount());
    return true;
}

void PObjectSystem::Shutdown()
{
    if (!GObjectSystemInitialized)
    {
        return;
    }

    FObjectRegistry::DestroyAllObjects();
    ResetGarbageCollectionRequests();
    FClassRegistry::Clear();
    GObjectSystemInitialized = false;
    PICO_LOG(LogObject, Info, "Object system shut down");
}

bool PObjectSystem::IsInitialized()
{
    return GObjectSystemInitialized;
}
}
