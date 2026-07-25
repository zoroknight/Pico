#pragma once

#include "Pico/Core/Name.h"

#include <cstddef>
#include <vector>

namespace Pico
{
class PClass;

class FClassRegistry
{
public:
    static bool RegisterClass(const PClass* Class);
    static const PClass* FindClass(FName Name);
    static std::vector<const PClass*> GetClasses();
    static std::size_t GetClassCount();
    static void Clear();
};
}
