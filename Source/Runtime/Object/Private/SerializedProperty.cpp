#include "Pico/Object/SerializedProperty.h"

#include "Pico/Object/Archive.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"

#include <utility>
#include <vector>

namespace Pico
{
namespace
{
void GatherProperties(const PClass* Class, std::vector<const PProperty*>& OutProperties)
{
    if (Class == nullptr)
    {
        return;
    }

    GatherProperties(Class->GetSuperClass(), OutProperties);
    for (const PProperty& Property : Class->GetProperties())
    {
        OutProperties.push_back(&Property);
    }
}

void SerializeVector3(FArchive& Archive, FVector3& Value)
{
    Archive.SerializeFloat(Value.X);
    Archive.SerializeFloat(Value.Y);
    Archive.SerializeFloat(Value.Z);
}

void SerializeRotator(FArchive& Archive, FRotator& Value)
{
    Archive.SerializeFloat(Value.Pitch);
    Archive.SerializeFloat(Value.Yaw);
    Archive.SerializeFloat(Value.Roll);
}

void SerializeTransform(FArchive& Archive, FTransform& Value)
{
    SerializeVector3(Archive, Value.Translation);
    Archive.SerializeFloat(Value.Rotation.X);
    Archive.SerializeFloat(Value.Rotation.Y);
    Archive.SerializeFloat(Value.Rotation.Z);
    Archive.SerializeFloat(Value.Rotation.W);
    SerializeVector3(Archive, Value.Scale);
    if (Archive.IsLoading() && !Archive.HasError())
    {
        Value.Rotation.Normalize();
    }
}

bool CaptureProperty(
    const PProperty& Property,
    const PObject* Object,
    FSerializedPropertyRecord& OutProperty)
{
    OutProperty.Name = Property.GetName().ToString();
    OutProperty.Type = Property.GetType();
    switch (OutProperty.Type)
    {
    case EPropertyType::Int32:
        return Property.GetValue(Object, OutProperty.Int32Value);
    case EPropertyType::Float:
        return Property.GetValue(Object, OutProperty.FloatValue);
    case EPropertyType::Bool:
        return Property.GetValue(Object, OutProperty.BoolValue);
    case EPropertyType::Vector3:
        return Property.GetValue(Object, OutProperty.Vector3Value);
    case EPropertyType::Rotator:
        return Property.GetValue(Object, OutProperty.RotatorValue);
    case EPropertyType::Transform:
        return Property.GetValue(Object, OutProperty.TransformValue);
    case EPropertyType::AssetPath:
        return Property.GetValue(Object, OutProperty.AssetPathValue);
    }
    return false;
}

bool SerializePropertyValue(FArchive& Archive, FSerializedPropertyRecord& Property)
{
    switch (Property.Type)
    {
    case EPropertyType::Int32:
        Archive.SerializeInt32(Property.Int32Value);
        break;
    case EPropertyType::Float:
        Archive.SerializeFloat(Property.FloatValue);
        break;
    case EPropertyType::Bool:
        Archive.SerializeBool(Property.BoolValue);
        break;
    case EPropertyType::Vector3:
        SerializeVector3(Archive, Property.Vector3Value);
        break;
    case EPropertyType::Rotator:
        SerializeRotator(Archive, Property.RotatorValue);
        break;
    case EPropertyType::Transform:
        SerializeTransform(Archive, Property.TransformValue);
        break;
    case EPropertyType::AssetPath:
    {
        std::string Value = Archive.IsSaving()
            ? std::string(Property.AssetPathValue.ToString())
            : std::string {};
        Archive.SerializeString(Value);
        if (Archive.IsLoading() && !Archive.HasError())
        {
            FAssetPath ParsedPath;
            if (!FAssetPath::TryParse(Value, ParsedPath))
            {
                return false;
            }
            Property.AssetPathValue = std::move(ParsedPath);
        }
        break;
    }
    default:
        return false;
    }
    return !Archive.HasError();
}
}

bool IsValidSerializedPropertyType(EPropertyType Type)
{
    return Type >= EPropertyType::Int32 && Type <= EPropertyType::AssetPath;
}

bool CaptureSerializedProperties(
    const PObject* Object,
    std::vector<FSerializedPropertyRecord>& OutProperties)
{
    OutProperties.clear();
    if (Object == nullptr || Object->GetClass() == nullptr)
    {
        return false;
    }

    std::vector<const PProperty*> Properties;
    GatherProperties(Object->GetClass(), Properties);
    OutProperties.reserve(Properties.size());
    for (const PProperty* Property : Properties)
    {
        if (Property != nullptr
            && (!Property->HasAnyFlags(EPropertyFlags::Serializable)
                || Property->HasAnyFlags(EPropertyFlags::Transient)
                || Property->GetType() == EPropertyType::DynamicMulticastDelegate))
        {
            continue;
        }
        FSerializedPropertyRecord Record;
        if (Property == nullptr || !CaptureProperty(*Property, Object, Record))
        {
            OutProperties.clear();
            return false;
        }
        OutProperties.push_back(std::move(Record));
    }
    return true;
}

bool SerializePropertyRecord(FArchive& Archive, FSerializedPropertyRecord& Property)
{
    if (Archive.IsSaving()
        && (Property.Name.empty() || !IsValidSerializedPropertyType(Property.Type)))
    {
        return false;
    }

    Archive.SerializeString(Property.Name);
    uint8 PropertyType = Archive.IsSaving() ? static_cast<uint8>(Property.Type) : 0;
    Archive.SerializeUInt8(PropertyType);
    if (Archive.IsLoading())
    {
        Property.Type = static_cast<EPropertyType>(PropertyType);
    }

    return !Archive.HasError()
        && !Property.Name.empty()
        && IsValidSerializedPropertyType(Property.Type)
        && SerializePropertyValue(Archive, Property);
}

ESerializedPropertyApplyResult ApplySerializedProperty(
    PObject* Object,
    const FSerializedPropertyRecord& SerializedProperty,
    EPropertyChangeType ChangeType)
{
    if (Object == nullptr || Object->GetClass() == nullptr)
    {
        return ESerializedPropertyApplyResult::InvalidArgument;
    }

    const PProperty* Property =
        Object->GetClass()->FindProperty(FName(SerializedProperty.Name));
    if (Property == nullptr)
    {
        return ESerializedPropertyApplyResult::None;
    }
    if (!Property->HasAnyFlags(EPropertyFlags::Serializable)
        || Property->HasAnyFlags(EPropertyFlags::Transient))
    {
        return ESerializedPropertyApplyResult::None;
    }
    if (Property->GetType() != SerializedProperty.Type)
    {
        return ESerializedPropertyApplyResult::TypeMismatch;
    }

    bool bApplied = false;
    switch (SerializedProperty.Type)
    {
    case EPropertyType::Int32:
        bApplied = Property->SetValue(Object, SerializedProperty.Int32Value, ChangeType);
        break;
    case EPropertyType::Float:
        bApplied = Property->SetValue(Object, SerializedProperty.FloatValue, ChangeType);
        break;
    case EPropertyType::Bool:
        bApplied = Property->SetValue(Object, SerializedProperty.BoolValue, ChangeType);
        break;
    case EPropertyType::Vector3:
        bApplied = Property->SetValue(Object, SerializedProperty.Vector3Value, ChangeType);
        break;
    case EPropertyType::Rotator:
        bApplied = Property->SetValue(Object, SerializedProperty.RotatorValue, ChangeType);
        break;
    case EPropertyType::Transform:
        bApplied = Property->SetValue(Object, SerializedProperty.TransformValue, ChangeType);
        break;
    case EPropertyType::AssetPath:
        bApplied = Property->SetValue(Object, SerializedProperty.AssetPathValue, ChangeType);
        break;
    default:
        return ESerializedPropertyApplyResult::TypeMismatch;
    }

    return bApplied
        ? ESerializedPropertyApplyResult::None
        : ESerializedPropertyApplyResult::AccessFailed;
}
}
