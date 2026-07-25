#include "Pico/Object/ObjectSerialization.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/Property.h"

#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace Pico
{
namespace
{
constexpr uint32 ObjectMagic = 0x4a424f50;
constexpr uint32 ObjectFormatVersion = 1;
constexpr uint32 MaxSerializedPropertyCount = 64 * 1024;
constexpr std::size_t MaxObjectFileSize = 16 * 1024 * 1024;

struct FSerializedProperty
{
    std::string Name;
    EPropertyType Type = EPropertyType::Int32;
    int32 Int32Value = 0;
    float FloatValue = 0.0f;
    bool BoolValue = false;
};

void ReportError(EObjectSerializationError* OutError, EObjectSerializationError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
}

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

bool ReadPropertyValue(FArchive& Archive, EPropertyType Type, FSerializedProperty& Property)
{
    switch (Type)
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
    default:
        return false;
    }

    return !Archive.HasError();
}

bool WritePropertyValue(FArchive& Archive, const PProperty& Property, const PObject* Object)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        if (!Property.GetValue(Object, Value))
        {
            return false;
        }
        Archive.SerializeInt32(Value);
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (!Property.GetValue(Object, Value))
        {
            return false;
        }
        Archive.SerializeFloat(Value);
        break;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        if (!Property.GetValue(Object, Value))
        {
            return false;
        }
        Archive.SerializeBool(Value);
        break;
    }
    default:
        return false;
    }

    return !Archive.HasError();
}

EObjectSerializationError ApplyPropertyValue(PObject* Object, const FSerializedProperty& SerializedProperty)
{
    const PProperty* Property = Object->GetClass()->FindProperty(FName(SerializedProperty.Name));
    if (Property == nullptr)
    {
        return EObjectSerializationError::None;
    }

    if (Property->GetType() != SerializedProperty.Type)
    {
        return EObjectSerializationError::PropertyTypeMismatch;
    }

    bool bApplied = false;
    switch (SerializedProperty.Type)
    {
    case EPropertyType::Int32:
        bApplied = Property->SetValue(Object, SerializedProperty.Int32Value);
        break;
    case EPropertyType::Float:
        bApplied = Property->SetValue(Object, SerializedProperty.FloatValue);
        break;
    case EPropertyType::Bool:
        bApplied = Property->SetValue(Object, SerializedProperty.BoolValue);
        break;
    default:
        return EObjectSerializationError::InvalidArchive;
    }

    return bApplied ? EObjectSerializationError::None : EObjectSerializationError::PropertyAccessFailed;
}

bool ReplaceFile(const std::filesystem::path& TemporaryPath, const std::filesystem::path& FilePath)
{
    std::error_code ErrorCode;
    std::filesystem::rename(TemporaryPath, FilePath, ErrorCode);
    if (!ErrorCode)
    {
        return true;
    }

    ErrorCode.clear();
    if (!std::filesystem::is_regular_file(FilePath, ErrorCode))
    {
        return false;
    }

    std::filesystem::path BackupPath = FilePath;
    BackupPath += ".bak";
    std::filesystem::remove(BackupPath, ErrorCode);

    ErrorCode.clear();
    std::filesystem::rename(FilePath, BackupPath, ErrorCode);
    if (ErrorCode)
    {
        return false;
    }

    ErrorCode.clear();
    std::filesystem::rename(TemporaryPath, FilePath, ErrorCode);
    if (ErrorCode)
    {
        std::error_code RestoreError;
        std::filesystem::rename(BackupPath, FilePath, RestoreError);
        return false;
    }

    std::filesystem::remove(BackupPath, ErrorCode);
    return true;
}
}

std::string_view ToString(EObjectSerializationError Error)
{
    switch (Error)
    {
    case EObjectSerializationError::None:
        return "None";
    case EObjectSerializationError::InvalidArgument:
        return "InvalidArgument";
    case EObjectSerializationError::InvalidArchive:
        return "InvalidArchive";
    case EObjectSerializationError::UnsupportedVersion:
        return "UnsupportedVersion";
    case EObjectSerializationError::ClassNotFound:
        return "ClassNotFound";
    case EObjectSerializationError::ObjectCreationFailed:
        return "ObjectCreationFailed";
    case EObjectSerializationError::PropertyTypeMismatch:
        return "PropertyTypeMismatch";
    case EObjectSerializationError::PropertyAccessFailed:
        return "PropertyAccessFailed";
    case EObjectSerializationError::PostLoadFailed:
        return "PostLoadFailed";
    case EObjectSerializationError::FileOpenFailed:
        return "FileOpenFailed";
    case EObjectSerializationError::FileReadFailed:
        return "FileReadFailed";
    case EObjectSerializationError::FileWriteFailed:
        return "FileWriteFailed";
    case EObjectSerializationError::FileTooLarge:
        return "FileTooLarge";
    case EObjectSerializationError::TrailingData:
        return "TrailingData";
    }

    return "Unknown";
}

bool SaveObject(FArchive& Archive, const PObject* Object, EObjectSerializationError* OutError)
{
    ReportError(OutError, EObjectSerializationError::None);
    if (!Archive.IsSaving() || Object == nullptr || Object->GetClass() == nullptr)
    {
        ReportError(OutError, EObjectSerializationError::InvalidArgument);
        return false;
    }

    uint32 Magic = ObjectMagic;
    uint32 Version = ObjectFormatVersion;
    std::string ClassName = Object->GetClass()->GetName().ToString();
    std::string ObjectName = Object->GetName().ToString();
    uint32 Flags = static_cast<uint32>(Object->GetFlags());
    std::vector<const PProperty*> Properties;
    GatherProperties(Object->GetClass(), Properties);
    if (Properties.size() > std::numeric_limits<uint32>::max())
    {
        ReportError(OutError, EObjectSerializationError::InvalidArchive);
        return false;
    }
    uint32 PropertyCount = static_cast<uint32>(Properties.size());

    Archive.SerializeUInt32(Magic);
    Archive.SerializeUInt32(Version);
    Archive.SerializeString(ClassName);
    Archive.SerializeString(ObjectName);
    Archive.SerializeUInt32(Flags);
    Archive.SerializeUInt32(PropertyCount);

    for (const PProperty* Property : Properties)
    {
        std::string PropertyName = Property->GetName().ToString();
        uint8 PropertyType = static_cast<uint8>(Property->GetType());
        Archive.SerializeString(PropertyName);
        Archive.SerializeUInt8(PropertyType);
        if (!WritePropertyValue(Archive, *Property, Object))
        {
            ReportError(
                OutError,
                Archive.HasError()
                    ? EObjectSerializationError::InvalidArchive
                    : EObjectSerializationError::PropertyAccessFailed);
            return false;
        }
    }

    if (Archive.HasError())
    {
        ReportError(OutError, EObjectSerializationError::InvalidArchive);
        return false;
    }

    return true;
}

PObject* LoadObject(FArchive& Archive, PObject* Outer, EObjectSerializationError* OutError)
{
    ReportError(OutError, EObjectSerializationError::None);
    if (!Archive.IsLoading())
    {
        ReportError(OutError, EObjectSerializationError::InvalidArgument);
        return nullptr;
    }

    uint32 Magic = 0;
    uint32 Version = 0;
    std::string ClassName;
    std::string ObjectName;
    uint32 Flags = 0;
    uint32 PropertyCount = 0;

    Archive.SerializeUInt32(Magic);
    Archive.SerializeUInt32(Version);
    Archive.SerializeString(ClassName);
    Archive.SerializeString(ObjectName);
    Archive.SerializeUInt32(Flags);
    Archive.SerializeUInt32(PropertyCount);
    if (Archive.HasError()
        || Magic != ObjectMagic
        || ClassName.empty()
        || ObjectName.empty()
        || PropertyCount > MaxSerializedPropertyCount)
    {
        ReportError(OutError, EObjectSerializationError::InvalidArchive);
        return nullptr;
    }
    if (Version != ObjectFormatVersion)
    {
        ReportError(OutError, EObjectSerializationError::UnsupportedVersion);
        return nullptr;
    }

    std::vector<FSerializedProperty> Properties;
    Properties.reserve(PropertyCount);
    for (uint32 Index = 0; Index < PropertyCount; ++Index)
    {
        FSerializedProperty Property;
        uint8 PropertyType = 0;
        Archive.SerializeString(Property.Name);
        Archive.SerializeUInt8(PropertyType);
        Property.Type = static_cast<EPropertyType>(PropertyType);
        if (Archive.HasError() || Property.Name.empty() || !ReadPropertyValue(Archive, Property.Type, Property))
        {
            ReportError(OutError, EObjectSerializationError::InvalidArchive);
            return nullptr;
        }
        Properties.push_back(std::move(Property));
    }

    const PClass* Class = FClassRegistry::FindClass(FName(ClassName));
    if (Class == nullptr)
    {
        ReportError(OutError, EObjectSerializationError::ClassNotFound);
        return nullptr;
    }

    PObject* Object = nullptr;
    try
    {
        Object = NewObject(Class, Outer, FName(ObjectName), static_cast<EObjectFlags>(Flags));
    }
    catch (...)
    {
        ReportError(OutError, EObjectSerializationError::ObjectCreationFailed);
        return nullptr;
    }
    if (Object == nullptr)
    {
        ReportError(OutError, EObjectSerializationError::ObjectCreationFailed);
        return nullptr;
    }

    for (const FSerializedProperty& Property : Properties)
    {
        const EObjectSerializationError Error = ApplyPropertyValue(Object, Property);
        if (Error != EObjectSerializationError::None)
        {
            ReportError(OutError, Error);
            FObjectRegistry::DestroyObjectTree(Object);
            return nullptr;
        }
    }

    try
    {
        Object->PostLoad();
    }
    catch (...)
    {
        ReportError(OutError, EObjectSerializationError::PostLoadFailed);
        FObjectRegistry::DestroyObjectTree(Object);
        return nullptr;
    }

    return Object;
}

bool SaveObjectToFile(
    const std::filesystem::path& FilePath,
    const PObject* Object,
    EObjectSerializationError* OutError)
{
    ReportError(OutError, EObjectSerializationError::None);
    if (FilePath.empty())
    {
        ReportError(OutError, EObjectSerializationError::InvalidArgument);
        return false;
    }

    FMemoryWriter Writer;
    if (!SaveObject(Writer, Object, OutError))
    {
        return false;
    }

    std::filesystem::path TemporaryPath = FilePath;
    TemporaryPath += ".tmp";
    std::ofstream File(TemporaryPath, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        ReportError(OutError, EObjectSerializationError::FileOpenFailed);
        return false;
    }

    const std::vector<uint8>& Data = Writer.GetData();
    File.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
    File.flush();
    const bool bWriteSucceeded = File.good();
    File.close();
    if (!bWriteSucceeded)
    {
        std::error_code ErrorCode;
        std::filesystem::remove(TemporaryPath, ErrorCode);
        ReportError(OutError, EObjectSerializationError::FileWriteFailed);
        return false;
    }

    if (!ReplaceFile(TemporaryPath, FilePath))
    {
        std::error_code ErrorCode;
        std::filesystem::remove(TemporaryPath, ErrorCode);
        ReportError(OutError, EObjectSerializationError::FileWriteFailed);
        return false;
    }

    return true;
}

PObject* LoadObjectFromFile(
    const std::filesystem::path& FilePath,
    PObject* Outer,
    EObjectSerializationError* OutError)
{
    ReportError(OutError, EObjectSerializationError::None);
    if (FilePath.empty())
    {
        ReportError(OutError, EObjectSerializationError::InvalidArgument);
        return nullptr;
    }

    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File)
    {
        ReportError(OutError, EObjectSerializationError::FileOpenFailed);
        return nullptr;
    }

    const std::streampos EndPosition = File.tellg();
    if (EndPosition < 0)
    {
        ReportError(OutError, EObjectSerializationError::FileReadFailed);
        return nullptr;
    }
    if (static_cast<std::size_t>(EndPosition) > MaxObjectFileSize)
    {
        ReportError(OutError, EObjectSerializationError::FileTooLarge);
        return nullptr;
    }

    std::vector<uint8> Data(static_cast<std::size_t>(EndPosition));
    File.seekg(0, std::ios::beg);
    if (!Data.empty())
    {
        File.read(reinterpret_cast<char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
    }
    if (!File)
    {
        ReportError(OutError, EObjectSerializationError::FileReadFailed);
        return nullptr;
    }

    FMemoryReader Reader(Data);
    PObject* Object = LoadObject(Reader, Outer, OutError);
    if (Object == nullptr)
    {
        return nullptr;
    }
    if (Reader.HasError())
    {
        ReportError(OutError, EObjectSerializationError::InvalidArchive);
        FObjectRegistry::DestroyObjectTree(Object);
        return nullptr;
    }
    if (Reader.GetRemainingSize() != 0)
    {
        ReportError(OutError, EObjectSerializationError::TrailingData);
        FObjectRegistry::DestroyObjectTree(Object);
        return nullptr;
    }

    return Object;
}
}
