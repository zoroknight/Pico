#pragma once

#include "Pico/Object/ObjectPtr.h"

#include <cstddef>
#include <vector>

namespace Pico
{
class FReferenceCollector
{
public:
    void AddReferencedObject(PObject* Object);

    void AddReferencedHandle(FObjectHandle Handle)
    {
        if (Handle.IsValid())
        {
            References.push_back(Handle);
        }
    }

    template <typename TObject>
    void AddReferencedObject(const TObjectPtr<TObject>& Object)
    {
        AddReferencedHandle(Object.GetHandle());
    }

    template <typename TRange>
    void AddReferencedHandles(const TRange& Handles)
    {
        for (FObjectHandle Handle : Handles)
        {
            AddReferencedHandle(Handle);
        }
    }

    const std::vector<FObjectHandle>& GetReferences() const { return References; }
    void Reset() { References.clear(); }
    std::size_t GetCurrentBytes() const
    {
        return References.size() * sizeof(FObjectHandle);
    }
    std::size_t GetReservedBytes() const
    {
        return References.capacity() * sizeof(FObjectHandle);
    }

private:
    std::vector<FObjectHandle> References;
};
}
