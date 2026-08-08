#include "Pico/Core/Delegate.h"

#include <atomic>

namespace Pico
{
FDelegateHandle::FDelegateHandle(uint64 InId)
    : Id(InId)
{
}

FDelegateHandle FDelegateHandle::Generate()
{
    static std::atomic<uint64> NextId {1};
    uint64 NewId = NextId.fetch_add(1, std::memory_order_relaxed);
    while (NewId == 0)
    {
        NewId = NextId.fetch_add(1, std::memory_order_relaxed);
    }
    return FDelegateHandle(NewId);
}

bool FDelegateHandle::IsValid() const
{
    return Id != 0;
}

void FDelegateHandle::Reset()
{
    Id = 0;
}
}
