#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/Types.h"
#include "Pico/Object/Property.h"

#include <string>
#include <vector>

namespace Pico
{
class FArchive;
class PObject;

struct FSerializedPropertyRecord
{
    std::string Name;
    EPropertyType Type = EPropertyType::Int32;
    int32 Int32Value = 0;
    float FloatValue = 0.0f;
    bool BoolValue = false;
    FVector3 Vector3Value;
    FRotator RotatorValue;
    FTransform TransformValue;
    FAssetPath AssetPathValue;
};

enum class ESerializedPropertyApplyResult
{
    None,
    InvalidArgument,
    TypeMismatch,
    AccessFailed
};

bool IsValidSerializedPropertyType(EPropertyType Type);
bool CaptureSerializedProperties(
    const PObject* Object,
    std::vector<FSerializedPropertyRecord>& OutProperties);
bool SerializePropertyRecord(FArchive& Archive, FSerializedPropertyRecord& Property);
ESerializedPropertyApplyResult ApplySerializedProperty(
    PObject* Object,
    const FSerializedPropertyRecord& Property,
    EPropertyChangeType ChangeType = EPropertyChangeType::Load);
}
