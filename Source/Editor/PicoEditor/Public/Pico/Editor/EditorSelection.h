#pragma once

#include "Pico/Object/ObjectTypes.h"

#include <string>
#include <string_view>

namespace Pico
{
class PObject;
class PWorld;

PObject* FindEditorWorldObjectByPath(PWorld* World, std::string_view Path);

class FEditorSelection
{
public:
    PObject* Resolve() const;
    FObjectHandle GetHandle() const;
    std::string GetObjectPath() const;
    bool IsValid() const;
    bool Set(PObject* Object);
    void Clear();
    bool Validate();
    PObject* Restore(PWorld* World, std::string_view ObjectPath);

private:
    FObjectHandle Handle;
};
}
