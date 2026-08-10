#include "Pico/Object/ObjectSystem.h"

#include "Pico/Core/Log.h"
#include "Pico/Core/GameThread.h"
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
        return CheckGameThread("PObjectSystem::Init");
    }
    if (!InitializeGameThread())
    {
        PICO_LOG(LogObject, Error, "Object system must initialize on the owning Game Thread");
        return false;
    }

    ResetGarbageCollectionRequests();
    FClassRegistry::Clear();
    if (!FClassRegistry::RegisterClass(PObject::StaticClass()))
    {
        FClassRegistry::Clear();
        ShutdownGameThread();
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
    if (!CheckGameThread("PObjectSystem::Shutdown"))
    {
        return;
    }

    FObjectRegistry::DestroyAllObjects();
    ResetGarbageCollectionRequests();
    FClassRegistry::Clear();
    GObjectSystemInitialized = false;
    PICO_LOG(LogObject, Info, "Object system shut down");
    ShutdownGameThread();
}

bool PObjectSystem::IsInitialized()
{
    return GObjectSystemInitialized;
}
}
