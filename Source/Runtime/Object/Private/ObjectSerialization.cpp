#include "Pico/Object/ObjectSerialization.h"

#include "Pico/Core/Log.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/SerializationFile.h"
#include "Pico/Object/SerializedProperty.h"

#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace Pico
{
namespace
{
constexpr uint32 ObjectMagic = 0x4a424f50;
constexpr uint32 ObjectFormatVersion = 3;
constexpr uint32 MinimumObjectFormatVersion = 1;
constexpr uint32 MaxSerializedPropertyCount = 64 * 1024;
constexpr std::size_t MaxObjectFileSize = 16 * 1024 * 1024;

void ReportError(EObjectSerializationError* OutError, EObjectSerializationError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
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
    if (!Archive.IsSaving()
        || Object == nullptr
        || Object->GetClass() == nullptr
        || HasAnyFlags(Object->GetFlags(), EObjectFlags::ClassDefaultObject))
    {
        ReportError(OutError, EObjectSerializationError::InvalidArgument);
        return false;
    }

    uint32 Magic = ObjectMagic;
    uint32 Version = ObjectFormatVersion;
    std::string ClassName = Object->GetClass()->GetName().ToString();
    std::string ObjectName = Object->GetName().ToString();
    uint32 Flags = static_cast<uint32>(Object->GetFlags());
    std::vector<FSerializedPropertyRecord> Properties;
    if (!CaptureSerializedProperties(Object, Properties))
    {
        ReportError(OutError, EObjectSerializationError::PropertyAccessFailed);
        return false;
    }
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

    for (FSerializedPropertyRecord& Property : Properties)
    {
        if (!SerializePropertyRecord(Archive, Property))
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
    if (Version < MinimumObjectFormatVersion || Version > ObjectFormatVersion)
    {
        ReportError(OutError, EObjectSerializationError::UnsupportedVersion);
        return nullptr;
    }

    std::vector<FSerializedPropertyRecord> Properties;
    Properties.reserve(PropertyCount);
    for (uint32 Index = 0; Index < PropertyCount; ++Index)
    {
        FSerializedPropertyRecord Property;
        const EPropertyType MaximumPropertyType = Version == 1
            ? EPropertyType::Bool
            : (Version == 2 ? EPropertyType::Transform : EPropertyType::AssetPath);
        if (!SerializePropertyRecord(Archive, Property)
            || Property.Type > MaximumPropertyType)
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

    for (const FSerializedPropertyRecord& Property : Properties)
    {
        const ESerializedPropertyApplyResult ApplyResult =
            ApplySerializedProperty(Object, Property);
        if (ApplyResult != ESerializedPropertyApplyResult::None)
        {
            EObjectSerializationError Error = EObjectSerializationError::PropertyAccessFailed;
            if (ApplyResult == ESerializedPropertyApplyResult::TypeMismatch)
            {
                Error = EObjectSerializationError::PropertyTypeMismatch;
            }
            else if (ApplyResult == ESerializedPropertyApplyResult::InvalidArgument)
            {
                Error = EObjectSerializationError::InvalidArgument;
            }
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
        PICO_LOG(
            LogObject,
            Error,
            "Could not open temporary object file '{}'",
            TemporaryPath.string());
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

    if (!Detail::ReplaceSerializedFile(TemporaryPath, FilePath))
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
