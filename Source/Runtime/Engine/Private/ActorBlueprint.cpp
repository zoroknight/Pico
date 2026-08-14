#include "Pico/Engine/ActorBlueprint.h"

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Math/Rotator.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Property.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace Pico
{
namespace
{
struct FCompiledActorBlueprint
{
    FAssetPath AssetPath;
    std::unique_ptr<PClass> GeneratedClass;
};

std::vector<FCompiledActorBlueprint>& GetCompiledBlueprints()
{
    static std::vector<FCompiledActorBlueprint> Blueprints;
    return Blueprints;
}

void SetError(EActorBlueprintError* OutError, EActorBlueprintError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

std::string JoinNames(const std::vector<FActorBlueprintObjectDefaults>& Objects)
{
    std::string Result;
    for (const FActorBlueprintObjectDefaults& Object : Objects)
    {
        if (!Result.empty()) Result.push_back(';');
        Result += Object.ObjectName.ToString();
    }
    return Result;
}

std::vector<std::string> Split(std::string_view Value, char Delimiter)
{
    std::vector<std::string> Parts;
    std::size_t Begin = 0;
    while (Begin <= Value.size())
    {
        const std::size_t End = Value.find(Delimiter, Begin);
        Parts.emplace_back(Value.substr(
            Begin,
            End == std::string_view::npos ? Value.size() - Begin : End - Begin));
        if (End == std::string_view::npos) break;
        Begin = End + 1;
    }
    return Parts;
}

template <typename TValue>
bool ParseNumber(std::string_view Text, TValue& OutValue)
{
    const char* Begin = Text.data();
    const char* End = Begin + Text.size();
    const auto Result = [&]()
    {
        if constexpr (std::is_floating_point_v<TValue>)
            return std::from_chars(
                Begin, End, OutValue, std::chars_format::general);
        else
            return std::from_chars(Begin, End, OutValue);
    }();
    return Result.ec == std::errc {} && Result.ptr == End;
}

bool ParseFloats(std::string_view Text, std::size_t Count, float* OutValues)
{
    const std::vector<std::string> Parts = Split(Text, ',');
    if (Parts.size() != Count) return false;
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        if (!ParseNumber<float>(Parts[Index], OutValues[Index])) return false;
    }
    return true;
}

std::string FormatFloats(const float* Values, std::size_t Count)
{
    std::ostringstream Stream;
    Stream << std::setprecision(9);
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        if (Index != 0) Stream << ',';
        Stream << Values[Index];
    }
    return Stream.str();
}

void GatherProperties(const PClass* Class, std::vector<const PProperty*>& Out)
{
    if (Class == nullptr) return;
    GatherProperties(Class->GetSuperClass(), Out);
    for (const PProperty& Property : Class->GetProperties())
        Out.push_back(&Property);
}

bool ExportProperty(const PProperty& Property, const PObject* Object, std::string& Out)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        if (!Property.GetValue(Object, Value)) return false;
        Out = std::to_string(Value);
        return true;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (!Property.GetValue(Object, Value)) return false;
        Out = FormatFloats(&Value, 1);
        return true;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        if (!Property.GetValue(Object, Value)) return false;
        Out = Value ? "true" : "false";
        return true;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (!Property.GetValue(Object, Value)) return false;
        const float Values[] = {Value.X, Value.Y, Value.Z};
        Out = FormatFloats(Values, 3);
        return true;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        if (!Property.GetValue(Object, Value)) return false;
        const float Values[] = {Value.Pitch, Value.Yaw, Value.Roll};
        Out = FormatFloats(Values, 3);
        return true;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        if (!Property.GetValue(Object, Value)) return false;
        const FRotator Rotation = Value.Rotation.Rotator();
        const float Values[] = {
            Value.Translation.X, Value.Translation.Y, Value.Translation.Z,
            Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
            Value.Scale.X, Value.Scale.Y, Value.Scale.Z};
        Out = FormatFloats(Values, 9);
        return true;
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        if (!Property.GetValue(Object, Value)) return false;
        Out = std::string(Value.ToString());
        return true;
    }
    default: return false;
    }
}

bool ImportProperty(const PProperty& Property, PObject* Object, std::string_view Text)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        return ParseNumber<int32>(Text, Value)
            && Property.SetValueSilently(Object, Value);
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        return ParseNumber<float>(Text, Value)
            && std::isfinite(Value)
            && Property.SetValueSilently(Object, Value);
    }
    case EPropertyType::Bool:
    {
        const bool bTrue = Text == "true" || Text == "1";
        const bool bFalse = Text == "false" || Text == "0";
        return (bTrue || bFalse) && Property.SetValueSilently(Object, bTrue);
    }
    case EPropertyType::Vector3:
    {
        float Values[3] {};
        return ParseFloats(Text, 3, Values)
            && Property.SetValueSilently(Object, FVector3(Values[0], Values[1], Values[2]));
    }
    case EPropertyType::Rotator:
    {
        float Values[3] {};
        return ParseFloats(Text, 3, Values)
            && Property.SetValueSilently(Object, FRotator(Values[0], Values[1], Values[2]));
    }
    case EPropertyType::Transform:
    {
        float Values[9] {};
        return ParseFloats(Text, 9, Values)
            && Property.SetValueSilently(
                Object,
                FTransform(
                    FRotator(Values[3], Values[4], Values[5]),
                    FVector3(Values[0], Values[1], Values[2]),
                    FVector3(Values[6], Values[7], Values[8])));
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        if (!Text.empty() && !FAssetPath::TryParse(Text, Value)) return false;
        return Property.SetValueSilently(Object, Value);
    }
    default: return false;
    }
}

bool CopyProperties(const PObject* Source, PObject* Destination)
{
    if (Source == nullptr || Destination == nullptr) return false;
    std::vector<const PProperty*> Properties;
    GatherProperties(Source->GetClass(), Properties);
    for (const PProperty* Property : Properties)
    {
        if (Property == nullptr
            || Property->HasAnyFlags(EPropertyFlags::Transient)
            || Property->GetType() == EPropertyType::Object
            || Property->GetType() == EPropertyType::DynamicMulticastDelegate)
            continue;
        std::string Value;
        if (!ExportProperty(*Property, Source, Value)
            || !ImportProperty(*Property, Destination, Value))
            return false;
    }
    return true;
}

bool ApplyOverrides(
    const FActorBlueprintObjectDefaults& Defaults,
    PObject* Object)
{
    if (Object == nullptr) return false;
    for (const auto& [Name, Value] : Defaults.Properties)
    {
        const PProperty* Property = Object->GetClass()->FindProperty(Name);
        if (Property == nullptr
            || Property->HasAnyFlags(EPropertyFlags::Transient)
            || !ImportProperty(*Property, Object, Value))
            return false;
    }
    return true;
}

const FDefaultSubobjectRecord* FindDefaultSubobject(
    const PClass* Class,
    FName Name)
{
    if (Class == nullptr) return nullptr;
    const auto& Records = Class->GetDefaultSubobjects();
    const auto Found = std::find_if(
        Records.begin(), Records.end(),
        [Name](const FDefaultSubobjectRecord& Record) { return Record.Name == Name; });
    return Found != Records.end() ? &*Found : nullptr;
}

bool ApplyDataToClass(const FActorBlueprintData& Data, PClass* GeneratedClass)
{
    if (GeneratedClass == nullptr || GeneratedClass->GetSuperClass() == nullptr)
        return false;
    PObject* GeneratedDefault = GeneratedClass->GetMutableDefaultObject();
    const PObject* ParentDefault = GeneratedClass->GetSuperClass()->GetDefaultObject();
    if (!CopyProperties(ParentDefault, GeneratedDefault)
        || !ApplyOverrides(Data.ActorDefaults, GeneratedDefault))
        return false;

    for (const FDefaultSubobjectRecord& GeneratedRecord : GeneratedClass->GetDefaultSubobjects())
    {
        const FDefaultSubobjectRecord* ParentRecord = FindDefaultSubobject(
            GeneratedClass->GetSuperClass(), GeneratedRecord.Name);
        if (ParentRecord == nullptr
            || !CopyProperties(ParentRecord->Template.get(), GeneratedRecord.Template.get()))
            return false;
        const auto Defaults = std::find_if(
            Data.ComponentDefaults.begin(), Data.ComponentDefaults.end(),
            [&GeneratedRecord](const FActorBlueprintObjectDefaults& Candidate)
            { return Candidate.ObjectName == GeneratedRecord.Name; });
        if (Defaults != Data.ComponentDefaults.end()
            && !ApplyOverrides(*Defaults, GeneratedRecord.Template.get()))
            return false;
    }
    return true;
}

FActorBlueprintObjectDefaults CaptureOverrides(
    FName ObjectName,
    const PObject* Source,
    const PObject* ParentDefault)
{
    FActorBlueprintObjectDefaults Result;
    Result.ObjectName = ObjectName;
    if (Source == nullptr || ParentDefault == nullptr) return Result;
    std::vector<const PProperty*> Properties;
    GatherProperties(Source->GetClass(), Properties);
    for (const PProperty* Property : Properties)
    {
        if (Property == nullptr
            || !Property->HasAnyFlags(EPropertyFlags::Serializable)
            || Property->HasAnyFlags(EPropertyFlags::Transient)
            || Property->GetType() == EPropertyType::Object
            || Property->GetType() == EPropertyType::DynamicMulticastDelegate)
            continue;
        std::string Value;
        std::string ParentValue;
        if (ExportProperty(*Property, Source, Value)
            && ExportProperty(*Property, ParentDefault, ParentValue)
            && Value != ParentValue)
            Result.Properties.emplace_back(Property->GetName(), std::move(Value));
    }
    return Result;
}

std::string MakeGeneratedClassName(const FAssetPath& AssetPath)
{
    std::string Result = "PBG_";
    for (char Character : AssetPath.ToString())
    {
        const unsigned char Code = static_cast<unsigned char>(Character);
        Result.push_back(std::isalnum(Code) ? Character : '_');
    }
    Result += "_C";
    return Result;
}
}

bool LoadActorBlueprintFromFile(
    const std::filesystem::path& FilePath,
    FActorBlueprintData& OutData,
    EActorBlueprintError* OutError)
{
    SetError(OutError, EActorBlueprintError::None);
    FConfigFile Config;
    if (FilePath.empty() || !Config.Load(FilePath))
    {
        SetError(OutError, EActorBlueprintError::FileReadFailed);
        return false;
    }
    FActorBlueprintData Data;
    Data.Version = Config.GetInt("Blueprint", "Version", 0);
    Data.ParentClassName = FName(Config.GetString("Blueprint", "ParentClass", ""));
    Data.GeneratedClassName = FName(Config.GetString("Blueprint", "GeneratedClass", ""));
    if (Data.Version != 1 || Data.ParentClassName.IsNone()
        || Data.GeneratedClassName.IsNone())
    {
        SetError(OutError, EActorBlueprintError::InvalidFormat);
        return false;
    }
    Data.ActorDefaults.ObjectName = FName("Actor");
    for (const auto& [Key, Value] : Config.GetSectionEntries("ActorDefaults"))
        Data.ActorDefaults.Properties.emplace_back(FName(Key), Value);
    const std::string Components = Config.GetString("Blueprint", "Components", "");
    for (const std::string& Name : Split(Components, ';'))
    {
        if (Name.empty()) continue;
        FActorBlueprintObjectDefaults Defaults;
        Defaults.ObjectName = FName(Name);
        for (const auto& [Key, Value] : Config.GetSectionEntries("Component." + Name))
            Defaults.Properties.emplace_back(FName(Key), Value);
        Data.ComponentDefaults.push_back(std::move(Defaults));
    }
    OutData = std::move(Data);
    return true;
}

bool SaveActorBlueprintToFile(
    const std::filesystem::path& FilePath,
    const FActorBlueprintData& Data,
    EActorBlueprintError* OutError)
{
    SetError(OutError, EActorBlueprintError::None);
    if (FilePath.empty() || Data.ParentClassName.IsNone()
        || Data.GeneratedClassName.IsNone())
    {
        SetError(OutError, EActorBlueprintError::InvalidArgument);
        return false;
    }
    FConfigFile Config;
    Config.SetString("Blueprint", "Version", std::to_string(Data.Version));
    Config.SetString("Blueprint", "ParentClass", Data.ParentClassName.ToString());
    Config.SetString("Blueprint", "GeneratedClass", Data.GeneratedClassName.ToString());
    Config.SetString("Blueprint", "Components", JoinNames(Data.ComponentDefaults));
    for (const auto& [Name, Value] : Data.ActorDefaults.Properties)
        Config.SetString("ActorDefaults", Name.ToString(), Value);
    for (const FActorBlueprintObjectDefaults& Defaults : Data.ComponentDefaults)
    {
        const std::string Section = "Component." + Defaults.ObjectName.ToString();
        for (const auto& [Name, Value] : Defaults.Properties)
            Config.SetString(Section, Name.ToString(), Value);
    }
    if (!Config.Save(FilePath))
    {
        SetError(OutError, EActorBlueprintError::FileWriteFailed);
        return false;
    }
    return true;
}

bool CreateActorBlueprintAsset(
    const std::filesystem::path& FilePath,
    const FAssetPath& AssetPath,
    const PClass* ParentClass,
    EActorBlueprintError* OutError)
{
    if (!AssetPath.IsValid() || ParentClass == nullptr
        || !ParentClass->IsChildOf(PActor::StaticClass()) || !ParentClass->CanConstruct())
    {
        SetError(OutError, EActorBlueprintError::InvalidArgument);
        return false;
    }
    FActorBlueprintData Data;
    Data.ParentClassName = ParentClass->GetName();
    Data.GeneratedClassName = FName(MakeGeneratedClassName(AssetPath));
    Data.ActorDefaults.ObjectName = FName("Actor");
    for (const FDefaultSubobjectRecord& Record : ParentClass->GetDefaultSubobjects())
        Data.ComponentDefaults.push_back({Record.Name, {}});
    return SaveActorBlueprintToFile(FilePath, Data, OutError);
}

bool SaveActorBlueprintDefaults(
    const std::filesystem::path& FilePath,
    const FAssetPath& AssetPath,
    const PActor* SourceActor,
    EActorBlueprintError* OutError)
{
    const PClass* GeneratedClass = FindActorBlueprintGeneratedClass(AssetPath);
    const PClass* ParentClass = GeneratedClass != nullptr
        ? GeneratedClass->GetSuperClass() : nullptr;
    if (SourceActor == nullptr || SourceActor->GetClass() != GeneratedClass
        || ParentClass == nullptr)
    {
        SetError(OutError, EActorBlueprintError::InvalidArgument);
        return false;
    }
    FActorBlueprintData Data;
    Data.ParentClassName = ParentClass->GetName();
    Data.GeneratedClassName = GeneratedClass->GetName();
    Data.ActorDefaults = CaptureOverrides(
        FName("Actor"), SourceActor, ParentClass->GetDefaultObject());
    for (const FDefaultSubobjectRecord& Record : GeneratedClass->GetDefaultSubobjects())
    {
        const PObject* Source = FindObject(
            const_cast<PActor*>(SourceActor), Record.Name);
        const FDefaultSubobjectRecord* ParentRecord =
            FindDefaultSubobject(ParentClass, Record.Name);
        Data.ComponentDefaults.push_back(CaptureOverrides(
            Record.Name,
            Source,
            ParentRecord != nullptr ? ParentRecord->Template.get() : nullptr));
    }
    if (!SaveActorBlueprintToFile(FilePath, Data, OutError)
        || !ApplyDataToClass(Data, const_cast<PClass*>(GeneratedClass)))
    {
        SetError(OutError, EActorBlueprintError::PropertyOverrideFailed);
        return false;
    }
    return true;
}

bool CompileActorBlueprint(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    EActorBlueprintError* OutError)
{
    SetError(OutError, EActorBlueprintError::None);
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::ActorBlueprint)
    {
        SetError(OutError, EActorBlueprintError::InvalidArgument);
        return false;
    }
    FActorBlueprintData Data;
    if (!LoadActorBlueprintFromFile(Record->FilePath, Data, OutError)) return false;
    const PClass* ParentClass = FClassRegistry::FindClass(Data.ParentClassName);
    if (ParentClass == nullptr || !ParentClass->IsChildOf(PActor::StaticClass()))
    {
        SetError(OutError, EActorBlueprintError::ParentClassNotFound);
        return false;
    }
    std::vector<FCompiledActorBlueprint>& Blueprints = GetCompiledBlueprints();
    auto Existing = std::find_if(
        Blueprints.begin(), Blueprints.end(),
        [&AssetPath](const FCompiledActorBlueprint& Entry)
        { return Entry.AssetPath == AssetPath; });
    if (Existing != Blueprints.end()
        && FClassRegistry::FindClass(Existing->GeneratedClass->GetName())
            == Existing->GeneratedClass.get())
    {
        if (!ApplyDataToClass(Data, Existing->GeneratedClass.get()))
        {
            SetError(OutError, EActorBlueprintError::PropertyOverrideFailed);
            return false;
        }
        return true;
    }
    if (const PClass* Conflict = FClassRegistry::FindClass(Data.GeneratedClassName))
    {
        (void)Conflict;
        SetError(OutError, EActorBlueprintError::GeneratedClassConflict);
        return false;
    }
    std::unique_ptr<PClass> Generated =
        PClass::CreateDynamicDerived(Data.GeneratedClassName, ParentClass);
    if (Generated == nullptr || !FClassRegistry::RegisterClass(Generated.get()))
    {
        SetError(OutError, EActorBlueprintError::ClassRegistrationFailed);
        return false;
    }
    if (!ApplyDataToClass(Data, Generated.get()))
    {
        SetError(OutError, EActorBlueprintError::PropertyOverrideFailed);
        return false;
    }
    if (Existing != Blueprints.end())
        Existing->GeneratedClass = std::move(Generated);
    else
        Blueprints.push_back({AssetPath, std::move(Generated)});
    return true;
}

bool CompileProjectActorBlueprints(
    const FAssetRegistry& Registry,
    EActorBlueprintError* OutError)
{
    SetError(OutError, EActorBlueprintError::None);
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        if (Record.Type == EAssetType::ActorBlueprint
            && !CompileActorBlueprint(Record.AssetPath, Registry, OutError))
            return false;
    }
    return true;
}

const PClass* FindActorBlueprintGeneratedClass(const FAssetPath& AssetPath)
{
    const auto& Blueprints = GetCompiledBlueprints();
    const auto Found = std::find_if(
        Blueprints.begin(), Blueprints.end(),
        [&AssetPath](const FCompiledActorBlueprint& Entry)
        { return Entry.AssetPath == AssetPath; });
    return Found != Blueprints.end()
        && FClassRegistry::FindClass(Found->GeneratedClass->GetName())
            == Found->GeneratedClass.get()
        ? Found->GeneratedClass.get() : nullptr;
}

FAssetPath FindActorBlueprintAsset(const PClass* GeneratedClass)
{
    const auto& Blueprints = GetCompiledBlueprints();
    const auto Found = std::find_if(
        Blueprints.begin(), Blueprints.end(),
        [GeneratedClass](const FCompiledActorBlueprint& Entry)
        { return Entry.GeneratedClass.get() == GeneratedClass; });
    return Found != Blueprints.end() ? Found->AssetPath : FAssetPath {};
}

std::string_view ToString(EActorBlueprintError Error)
{
    switch (Error)
    {
    case EActorBlueprintError::None: return "None";
    case EActorBlueprintError::InvalidArgument: return "InvalidArgument";
    case EActorBlueprintError::FileReadFailed: return "FileReadFailed";
    case EActorBlueprintError::FileWriteFailed: return "FileWriteFailed";
    case EActorBlueprintError::InvalidFormat: return "InvalidFormat";
    case EActorBlueprintError::ParentClassNotFound: return "ParentClassNotFound";
    case EActorBlueprintError::GeneratedClassConflict: return "GeneratedClassConflict";
    case EActorBlueprintError::ClassRegistrationFailed: return "ClassRegistrationFailed";
    case EActorBlueprintError::PropertyOverrideFailed: return "PropertyOverrideFailed";
    }
    return "Unknown";
}
}
