#pragma once

#include "Pico/Core/Types.h"

namespace Pico
{
class PObject;
class PProperty;

enum class EPropertyChangeType : uint8
{
    ValueSet,
    Interactive,
    Load,
    UndoRedo
};

struct FPropertyChangedEvent
{
    PObject* Object = nullptr;
    const PProperty* Property = nullptr;
    EPropertyChangeType ChangeType = EPropertyChangeType::ValueSet;
};
}
