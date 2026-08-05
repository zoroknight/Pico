#pragma once

#include "Pico/Object/ObjectTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class PObject;
class PWorld;

PObject* FindEditorWorldObjectByPath(PWorld* World, std::string_view Path);

enum class EEditorSelectionOperation
{
    Replace,
    Add,
    Toggle,
    RangeReplace,
    RangeAdd
};

class FEditorSelection
{
public:
    PObject* Resolve() const;
    std::vector<PObject*> ResolveAll() const;
    FObjectHandle GetHandle() const;
    const std::vector<FObjectHandle>& GetHandles() const;
    std::string GetObjectPath() const;
    std::vector<std::string> GetObjectPaths() const;
    std::size_t Num() const;
    bool IsValid() const;
    bool Contains(const PObject* Object) const;
    bool Contains(FObjectHandle ObjectHandle) const;
    bool Set(PObject* Object);
    bool Add(PObject* Object);
    bool Remove(PObject* Object);
    bool Toggle(PObject* Object);
    bool SetRange(const std::vector<PObject*>& OrderedObjects, PObject* Object, bool bAppend);
    void Clear();
    bool Validate();
    PObject* Restore(PWorld* World, std::string_view ObjectPath);
    PObject* Restore(
        PWorld* World,
        const std::vector<std::string>& ObjectPaths,
        std::string_view PrimaryObjectPath);

private:
    std::vector<FObjectHandle> Handles;
    FObjectHandle PrimaryHandle;
    FObjectHandle RangeAnchorHandle;
};
}
