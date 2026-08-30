#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Object/ObjectTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Pico
{
class PObject;
struct FGarbageCollectionResult;

struct FObjectHierarchyIndexStats
{
    std::size_t ParentEntryCount = 0;
    std::size_t ChildRelationCount = 0;
    std::size_t EstimatedStorageBytes = 0;
};

class FObjectRegistry
{
public:
    static PObject* AddObject(FObjectPtr Object, bool bDeferPostInitProperties = false);
    static void PostInitObject(PObject* Object);
    static bool DestroyObject(PObject* Object);
    static void DestroyObjectTree(PObject* Root);
    static void DestroyAllObjects();

    static PObject* ResolveObject(FObjectHandle Handle);
    static PObject* FindObject(PObject* Outer, FName Name);
    static bool RenameObject(PObject* Object, FName NewName);
    static std::vector<PObject*> GetObjects();
    static std::size_t GetObjectCount();
    static bool ValidateNameIndex(std::string* OutError = nullptr);
    static bool ValidateHierarchyIndex(std::string* OutError = nullptr);
    static FObjectHierarchyIndexStats GetHierarchyIndexStats();
    static void PublishMemoryStatistics();
    static bool CompactStorage(std::string* OutError = nullptr);
    static bool AddToRoot(PObject* Object);
    static bool RemoveFromRoot(PObject* Object);
    static bool IsRooted(const PObject* Object);
    static bool IsGarbageCollecting();
    static FGarbageCollectionResult CollectGarbage();

private:
    static void CallBeginDestroy(PObject* Object);
};
}
