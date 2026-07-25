#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Object/ObjectTypes.h"

#include <cstddef>
#include <vector>

namespace Pico
{
class PObject;

class FObjectRegistry
{
public:
    static PObject* AddObject(FObjectPtr Object);
    static bool DestroyObject(PObject* Object);
    static void DestroyObjectTree(PObject* Root);
    static void DestroyAllObjects();

    static PObject* ResolveObject(FObjectHandle Handle);
    static PObject* FindObject(PObject* Outer, FName Name);
    static std::vector<PObject*> GetObjects();
    static std::size_t GetObjectCount();
};
}
