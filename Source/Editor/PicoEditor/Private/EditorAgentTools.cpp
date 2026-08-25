#include "Pico/Editor/EditorAgentTools.h"

#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/EditorPropertyService.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorTransactionManager.h"
#include "Pico/Editor/EditorWorldDocument.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/Log.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorBlueprint.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Property.h"
#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

FAgentToolResult Success(const FAgentToolCall& Call, FJson Output)
{
    return {Call.Id, true, Output.dump(), {}, false};
}

FAgentToolResult Failure(const FAgentToolCall& Call, std::string Error)
{
    return {Call.Id, false, "{}", std::move(Error), false};
}

bool IsSafeObjectName(std::string_view Name)
{
    if (Name.empty() || Name.size() > 64) return false;
    for (const unsigned char Character : Name)
    {
        if (!std::isalnum(Character) && Character != '_') return false;
    }
    return true;
}

bool IsSafeRunId(std::string_view RunId)
{
    return !RunId.empty() && RunId.size() <= 128
        && std::all_of(RunId.begin(), RunId.end(), [](unsigned char Character)
        {
            return std::isalnum(Character) || Character == '-'
                || Character == '_';
        });
}

std::string StableOperationSuffix(std::string_view Text)
{
    std::uint64_t Hash = 14695981039346656037ull;
    for (const unsigned char Character : Text)
    {
        Hash ^= Character;
        Hash *= 1099511628211ull;
    }
    constexpr char Digits[] = "0123456789abcdef";
    std::string Result(16, '0');
    for (int Index = 15; Index >= 0; --Index)
    {
        Result[static_cast<std::size_t>(Index)] = Digits[Hash & 0xfu];
        Hash >>= 4u;
    }
    return Result;
}

const char* PropertyTypeName(EPropertyType Type)
{
    switch (Type)
    {
    case EPropertyType::Int32: return "Int32";
    case EPropertyType::Float: return "Float";
    case EPropertyType::Bool: return "Bool";
    case EPropertyType::Vector3: return "Vector3";
    case EPropertyType::Rotator: return "Rotator";
    case EPropertyType::Transform: return "Transform";
    case EPropertyType::AssetPath: return "AssetPath";
    case EPropertyType::Object: return "ObjectReference";
    case EPropertyType::DynamicMulticastDelegate: return "DynamicMulticastDelegate";
    }
    return "Unknown";
}

void GatherProperties(const PClass* Class, std::vector<const PProperty*>& Out)
{
    if (Class == nullptr) return;
    GatherProperties(Class->GetSuperClass(), Out);
    for (const PProperty& Property : Class->GetProperties())
        Out.push_back(&Property);
}

FJson VectorToJson(const FVector3& Value)
{
    return {{"x", Value.X}, {"y", Value.Y}, {"z", Value.Z}};
}

FJson RotatorToJson(const FRotator& Value)
{
    return {{"pitch", Value.Pitch}, {"yaw", Value.Yaw}, {"roll", Value.Roll}};
}

bool PropertyValueToJson(
    const PProperty& Property,
    const PObject* Object,
    FJson& OutValue)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        if (!Property.GetValue(Object, Value)) return false;
        OutValue = Value;
        return true;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (!Property.GetValue(Object, Value)) return false;
        OutValue = Value;
        return true;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        if (!Property.GetValue(Object, Value)) return false;
        OutValue = Value;
        return true;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (!Property.GetValue(Object, Value)) return false;
        OutValue = VectorToJson(Value);
        return true;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        if (!Property.GetValue(Object, Value)) return false;
        OutValue = RotatorToJson(Value);
        return true;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        if (!Property.GetValue(Object, Value)) return false;
        OutValue = {{"location", VectorToJson(Value.Translation)},
            {"rotation", RotatorToJson(Value.Rotation.Rotator())},
            {"scale", VectorToJson(Value.Scale)}};
        return true;
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        if (!Property.GetValue(Object, Value)) return false;
        OutValue = std::string(Value.ToString());
        return true;
    }
    case EPropertyType::Object:
    {
        PObject* Referenced = Property.GetReferencedObject(Object);
        OutValue = Referenced != nullptr ? FJson(Referenced->GetPathName()) : FJson(nullptr);
        return true;
    }
    case EPropertyType::DynamicMulticastDelegate:
        OutValue = nullptr;
        return true;
    }
    return false;
}

FJson DescribeProperty(const PProperty& Property, const PObject* Object)
{
    const FPropertyMetadata& Metadata = Property.GetMetadata();
    const bool bEditable = Property.HasAnyFlags(EPropertyFlags::Editable)
        && !Property.HasAnyFlags(EPropertyFlags::ReadOnly)
        && Property.GetType() != EPropertyType::Object
        && Property.GetType() != EPropertyType::DynamicMulticastDelegate;
    FJson Description {
        {"type", PropertyTypeName(Property.GetType())},
        {"editable", bEditable},
        {"read_only", Property.HasAnyFlags(EPropertyFlags::ReadOnly)}
    };
    FJson Value;
    if (PropertyValueToJson(Property, Object, Value)) Description["value"] = Value;
    if (!Metadata.DisplayName.empty()) Description["display_name"] = Metadata.DisplayName;
    if (!Metadata.Description.empty()) Description["description"] = Metadata.Description;
    if (!Metadata.Semantic.empty()) Description["semantic"] = Metadata.Semantic;
    if (!Metadata.Units.empty()) Description["units"] = Metadata.Units;
    if (Metadata.Minimum) Description["minimum"] = *Metadata.Minimum;
    if (Metadata.Maximum) Description["maximum"] = *Metadata.Maximum;
    if (!Metadata.EnumOptions.empty())
    {
        Description["enum"] = FJson::array();
        for (const FPropertyMetadata::FEnumOption& Option : Metadata.EnumOptions)
            Description["enum"].push_back(
                {{"value", Option.Value}, {"name", Option.DisplayName}});
    }
    switch (Property.GetType())
    {
    case EPropertyType::Vector3:
        Description["json_shape"] = R"({"x":number,"y":number,"z":number})";
        break;
    case EPropertyType::Rotator:
        Description["json_shape"] = R"({"pitch":degrees,"yaw":degrees,"roll":degrees})";
        break;
    case EPropertyType::Transform:
        Description["json_shape"] =
            R"({"location":{"x":number,"y":number,"z":number},"rotation":{"pitch":degrees,"yaw":degrees,"roll":degrees},"scale":{"x":number,"y":number,"z":number}})";
        break;
    default:
        break;
    }
    return Description;
}

FJson DescribeObject(PObject* Object, bool bIncludeComponents)
{
    FJson Result {
        {"object_path", Object->GetPathName()},
        {"class", Object->GetClass()->GetName().ToString()},
        {"properties", FJson::object()}
    };
    std::vector<const PProperty*> Properties;
    GatherProperties(Object->GetClass(), Properties);
    for (const PProperty* Property : Properties)
    {
        if (Property != nullptr && Property->HasAnyFlags(
                EPropertyFlags::Editable | EPropertyFlags::ReadOnly))
        {
            Result["properties"][Property->GetName().ToString()] =
                DescribeProperty(*Property, Object);
        }
    }
    if (bIncludeComponents && Object->IsA(PActor::StaticClass()))
    {
        Result["components"] = FJson::array();
        PActor* Actor = static_cast<PActor*>(Object);
        for (PActorComponent* Component : Actor->GetComponents())
            if (Component != nullptr)
                Result["components"].push_back(DescribeObject(Component, false));
        Result["root_component"] = Actor->GetRootComponent() != nullptr
            ? FJson(Actor->GetRootComponent()->GetPathName()) : FJson(nullptr);
    }
    return Result;
}

bool ReadFiniteNumber(const FJson& Json, float& Out)
{
    if (!Json.is_number()) return false;
    const double Number = Json.get<double>();
    if (!std::isfinite(Number)
        || Number < -static_cast<double>(std::numeric_limits<float>::max())
        || Number > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }
    Out = static_cast<float>(Number);
    return true;
}

bool JsonToVector(const FJson& Json, FVector3& Out)
{
    return Json.is_object() && Json.size() == 3
        && Json.contains("x") && Json.contains("y") && Json.contains("z")
        && ReadFiniteNumber(Json.at("x"), Out.X)
        && ReadFiniteNumber(Json.at("y"), Out.Y)
        && ReadFiniteNumber(Json.at("z"), Out.Z);
}

bool JsonToRotator(const FJson& Json, FRotator& Out)
{
    return Json.is_object() && Json.size() == 3
        && Json.contains("pitch") && Json.contains("yaw") && Json.contains("roll")
        && ReadFiniteNumber(Json.at("pitch"), Out.Pitch)
        && ReadFiniteNumber(Json.at("yaw"), Out.Yaw)
        && ReadFiniteNumber(Json.at("roll"), Out.Roll);
}

bool JsonToPropertyValue(
    const PProperty& Property,
    const FJson& Json,
    FEditorPropertyValue& Out,
    std::string& OutError)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
        if (Json.is_number_integer())
        {
            const std::int64_t Value = Json.get<std::int64_t>();
            if (Value >= std::numeric_limits<int32>::min()
                && Value <= std::numeric_limits<int32>::max())
            {
                Out = static_cast<int32>(Value);
                return true;
            }
        }
        break;
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (ReadFiniteNumber(Json, Value)) { Out = Value; return true; }
        break;
    }
    case EPropertyType::Bool:
        if (Json.is_boolean()) { Out = Json.get<bool>(); return true; }
        break;
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (JsonToVector(Json, Value)) { Out = Value; return true; }
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        if (JsonToRotator(Json, Value)) { Out = Value; return true; }
        break;
    }
    case EPropertyType::Transform:
    {
        FVector3 Location;
        FVector3 Scale;
        FRotator Rotation;
        if (Json.is_object() && Json.size() == 3
            && Json.contains("location") && Json.contains("rotation")
            && Json.contains("scale")
            && JsonToVector(Json.at("location"), Location)
            && JsonToRotator(Json.at("rotation"), Rotation)
            && JsonToVector(Json.at("scale"), Scale))
        {
            Out = FTransform(Rotation, Location, Scale);
            return true;
        }
        break;
    }
    case EPropertyType::AssetPath:
        if (Json.is_string())
        {
            const std::string Text = Json.get<std::string>();
            FAssetPath Value;
            if (Text.empty() || FAssetPath::TryParse(Text, Value))
            {
                Out = Value;
                return true;
            }
        }
        break;
    case EPropertyType::Object:
    case EPropertyType::DynamicMulticastDelegate:
        OutError = "Object references and delegates are not Agent-editable";
        return false;
    }
    OutError = "Value does not match reflected type "
        + std::string(PropertyTypeName(Property.GetType()));
    return false;
}

bool JsonEquivalent(const FJson& Left, const FJson& Right)
{
    if (Left.is_number() && Right.is_number())
        return std::abs(Left.get<double>() - Right.get<double>()) <= 0.0001;
    if (Left.type() != Right.type()) return false;
    if (Left.is_object())
    {
        if (Left.size() != Right.size()) return false;
        for (auto It = Left.begin(); It != Left.end(); ++It)
            if (!Right.contains(It.key()) || !JsonEquivalent(It.value(), Right.at(It.key())))
                return false;
        return true;
    }
    if (Left.is_array())
    {
        if (Left.size() != Right.size()) return false;
        for (std::size_t Index = 0; Index < Left.size(); ++Index)
            if (!JsonEquivalent(Left[Index], Right[Index])) return false;
        return true;
    }
    return Left == Right;
}

bool FingerprintWorld(
    const FWorldAssetData& Data,
    std::string& OutFingerprint,
    std::string& OutError)
{
    FMemoryWriter Writer;
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!SerializeWorldAsset(Writer, Data, &Error) || Writer.HasError())
    {
        OutError = "Could not fingerprint World: " + std::string(ToString(Error));
        return false;
    }
    const std::vector<uint8>& Bytes = Writer.GetData();
    OutFingerprint = StableOperationSuffix(std::string_view(
        reinterpret_cast<const char*>(Bytes.data()), Bytes.size()));
    return true;
}

std::vector<std::string> DiffObjectLabels(
    const FWorldAssetData& Left,
    const FWorldAssetData& Right)
{
    std::unordered_set<uint64> RightIds;
    for (const FSceneObjectRecord& Record : Right.Objects)
        RightIds.insert(Record.Id.Value);
    std::vector<std::string> Result;
    for (const FSceneObjectRecord& Record : Left.Objects)
        if (!RightIds.contains(Record.Id.Value))
            Result.push_back(Record.ClassName + ":" + Record.ObjectName);
    std::sort(Result.begin(), Result.end());
    return Result;
}

bool ReplaceFile(
    const std::filesystem::path& Staging,
    const std::filesystem::path& Destination,
    std::string& OutError)
{
    std::error_code Error;
    std::filesystem::remove(Destination, Error);
    Error.clear();
    std::filesystem::rename(Staging, Destination, Error);
    if (!Error) return true;
    OutError = "Could not publish ChangeSet file: " + Error.message();
    return false;
}
}

struct FEditorAgentToolExecutor::FImpl
{
    struct FPendingChangeSet
    {
        std::string RunId;
        FWorldAssetData Before;
        std::vector<std::string> SelectedObjectPaths;
        std::string PrimaryObjectPath;
        std::string BeforeFingerprint;
    };

    class FTransaction final : public IAgentToolTransaction
    {
    public:
        FTransaction(
            FEngineLoop* InEngineLoop,
            FEditorSelection* InSelection,
            FEditorTransactionManager* InTransactions,
            FEditorTransactionManager::FRestoreSnapshot InRestoreSnapshot,
            std::function<void()> InOnWorldChanged)
            : EngineLoop(InEngineLoop)
            , Selection(InSelection)
            , Transactions(InTransactions)
            , RestoreSnapshot(std::move(InRestoreSnapshot))
            , OnWorldChanged(std::move(InOnWorldChanged))
        {
        }

        bool Begin(std::string_view Description, std::string& OutError) override
        {
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            EWorldSerializationError Error = EWorldSerializationError::None;
            if (!World || !Selection || !Transactions
                || !Transactions->Begin(std::string(Description), *World,
                    Selection->GetObjectPaths(), Selection->GetObjectPath(), &Error))
            {
                OutError = "Could not begin editor transaction: "
                    + std::string(ToString(Error));
                return false;
            }
            return true;
        }

        bool Commit(std::string& OutError) override
        {
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            EWorldSerializationError Error = EWorldSerializationError::None;
            if (!World || !Selection || !Transactions
                || !Transactions->Commit(*World, Selection->GetObjectPaths(),
                    Selection->GetObjectPath(), &Error))
            {
                OutError = "Could not commit editor transaction: "
                    + std::string(ToString(Error));
                return false;
            }
            if (OnWorldChanged) OnWorldChanged();
            return true;
        }

        bool Rollback(std::string& OutError) override
        {
            if (!Transactions)
            {
                OutError = "Editor transaction manager is unavailable";
                return false;
            }
            EWorldSerializationError Error = EWorldSerializationError::None;
            const bool bRestored = Transactions->Rollback(
                [this](const FEditorWorldSnapshot& Snapshot,
                       EWorldSerializationError* RestoreError)
                {
                    if (RestoreSnapshot)
                    {
                        return RestoreSnapshot(Snapshot, RestoreError);
                    }
                    if (!EngineLoop || !Selection
                        || !EngineLoop->ReplaceWorld(Snapshot.WorldData, RestoreError))
                    {
                        return false;
                    }
                    Selection->Restore(EngineLoop->GetWorld(),
                        Snapshot.SelectedObjectPaths, Snapshot.PrimaryObjectPath);
                    if (OnWorldChanged) OnWorldChanged();
                    return true;
                },
                &Error);
            if (!bRestored)
            {
                OutError = "Could not roll back editor transaction: "
                    + std::string(ToString(Error));
            }
            return bRestored;
        }

    private:
        FEngineLoop* EngineLoop = nullptr;
        FEditorSelection* Selection = nullptr;
        FEditorTransactionManager* Transactions = nullptr;
        FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot;
        std::function<void()> OnWorldChanged;
    };

    FImpl(
        FEngineLoop* InEngineLoop,
        FEditorSelection* InSelection,
        FEditorTransactionManager* InTransactions,
        IAgentToolApproval* Approval,
        std::function<void()> OnWorldChanged,
        FEditorAgentHostServices InHostServices)
        : EngineLoop(InEngineLoop)
        , Selection(InSelection)
        , HostServices(std::move(InHostServices))
        , Transaction(InEngineLoop, InSelection, InTransactions,
            HostServices.RestoreSnapshot, std::move(OnWorldChanged))
        , Registry(BuildPolicy(InEngineLoop), Approval, &Transaction)
    {
        RegisterTools();
    }

    void BeginRun(std::string_view RunId)
    {
        PendingChangeSet.reset();
        LastChangeSetError.clear();
        if (HostServices.ChangeSetDirectory.empty() || !IsSafeRunId(RunId)) return;
        PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
        if (!World) return;
        FPendingChangeSet Pending;
        Pending.RunId = RunId;
        EWorldSerializationError Error = EWorldSerializationError::None;
        if (!CaptureWorld(*World, Pending.Before, &Error))
        {
            LastChangeSetError = "Could not capture Agent Run start: "
                + std::string(ToString(Error));
            return;
        }
        if (!FingerprintWorld(Pending.Before, Pending.BeforeFingerprint,
                LastChangeSetError))
            return;
        if (Selection)
        {
            Pending.SelectedObjectPaths = Selection->GetObjectPaths();
            Pending.PrimaryObjectPath = Selection->GetObjectPath();
        }
        PendingChangeSet = std::move(Pending);
    }

    void EndRun(std::string_view RunId, EAgentStatus Status)
    {
        if (!PendingChangeSet || PendingChangeSet->RunId != RunId) return;
        FPendingChangeSet Pending = std::move(*PendingChangeSet);
        PendingChangeSet.reset();
        PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
        if (!World) return;
        FWorldAssetData After;
        EWorldSerializationError WorldError = EWorldSerializationError::None;
        if (!CaptureWorld(*World, After, &WorldError))
        {
            LastChangeSetError = "Could not capture Agent Run end: "
                + std::string(ToString(WorldError));
            return;
        }
        std::string AfterFingerprint;
        if (!FingerprintWorld(After, AfterFingerprint, LastChangeSetError)
            || AfterFingerprint == Pending.BeforeFingerprint)
            return;

        const std::filesystem::path Directory = HostServices.ChangeSetDirectory;
        const std::filesystem::path BeforePath = Directory
            / (Pending.RunId + ".before.pworld");
        const std::filesystem::path AfterPath = Directory
            / (Pending.RunId + ".after.pworld");
        const std::filesystem::path MetadataPath = Directory
            / (Pending.RunId + ".json");
        const std::filesystem::path BeforeStaging = BeforePath.string() + ".tmp";
        const std::filesystem::path AfterStaging = AfterPath.string() + ".tmp";
        const std::filesystem::path MetadataStaging = MetadataPath.string() + ".tmp";
        std::error_code FileError;
        std::filesystem::create_directories(Directory, FileError);
        if (FileError)
        {
            LastChangeSetError = "Could not create Agent ChangeSet directory: "
                + FileError.message();
            return;
        }
        if (!SaveWorldAssetDataToFile(BeforeStaging, Pending.Before, &WorldError)
            || !SaveWorldAssetDataToFile(AfterStaging, After, &WorldError))
        {
            LastChangeSetError = "Could not stage Agent ChangeSet: "
                + std::string(ToString(WorldError));
            return;
        }
        FJson Metadata = {{"format_version", 1}, {"run_id", Pending.RunId},
            {"status", ToString(Status)},
            {"before_file", BeforePath.filename().string()},
            {"after_file", AfterPath.filename().string()},
            {"before_fingerprint", Pending.BeforeFingerprint},
            {"after_fingerprint", AfterFingerprint},
            {"before_object_count", Pending.Before.Objects.size()},
            {"after_object_count", After.Objects.size()},
            {"added_objects", DiffObjectLabels(After, Pending.Before)},
            {"removed_objects", DiffObjectLabels(Pending.Before, After)},
            {"selected_object_paths", Pending.SelectedObjectPaths},
            {"primary_object_path", Pending.PrimaryObjectPath}};
        {
            std::ofstream Stream(MetadataStaging,
                std::ios::binary | std::ios::trunc);
            Stream << Metadata.dump(2) << '\n';
            Stream.flush();
            if (!Stream)
            {
                LastChangeSetError = "Could not stage Agent ChangeSet metadata";
                return;
            }
        }
        if (!ReplaceFile(BeforeStaging, BeforePath, LastChangeSetError)
            || !ReplaceFile(AfterStaging, AfterPath, LastChangeSetError)
            || !ReplaceFile(MetadataStaging, MetadataPath, LastChangeSetError))
            return;
    }

    static FAgentToolPolicy BuildPolicy(FEngineLoop* EngineLoop)
    {
        FAgentToolPolicy Policy;
        Policy.bAllowModifyWorld = true;
        Policy.bAllowWriteProject = true;
        Policy.bAllowLaunchProcess = true;
        Policy.ProjectRoot = EngineLoop ? FPaths::GetProjectRootDir() : std::filesystem::path {};
        return Policy;
    }

    std::vector<FAgentKnowledgeRecord> CollectKnowledgeRecords() const
    {
        std::vector<FAgentKnowledgeRecord> Result;
        PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
        if (World)
        {
            FJson WorldJson{{"world", World->GetPathName()},
                {"actors", FJson::array()}};
            for (PLevel* Level : World->GetLevels())
                if (Level) for (PActor* Actor : Level->GetActors())
                    if (Actor) WorldJson["actors"].push_back({
                        {"object_path", Actor->GetPathName()},
                        {"class", Actor->GetClass()
                            ? Actor->GetClass()->GetName().ToString() : "Unknown"},
                        {"location", VectorToJson(Actor->GetActorLocation())}});
            FAgentKnowledgeRecord Record;
            Record.SourceType = "world";
            Record.SourcePath = World->GetPathName();
            Record.Title = "Active World snapshot";
            Record.Content = WorldJson.dump();
            Record.Tags = {"world", "actor", "scene"};
            Record.Provenance = "Live Game Thread World snapshot";
            Result.push_back(std::move(Record));
        }

        if (EngineLoop)
        {
            FJson Assets = FJson::array();
            for (const FAssetRecord& Asset : EngineLoop->GetAssetRegistry().GetAssets())
                Assets.push_back({{"path", Asset.AssetPath.ToString()},
                    {"type", ToString(Asset.Type)}, {"size", Asset.FileSize}});
            FAgentKnowledgeRecord Record;
            Record.SourceType = "assets";
            Record.SourcePath = "/Game";
            Record.Title = "Project AssetRegistry snapshot";
            Record.Content = Assets.dump();
            Record.SourceRevision = Assets.size();
            Record.Tags = {"asset", "assetregistry", "content"};
            Record.Provenance = "Live AssetRegistry";
            Result.push_back(std::move(Record));
        }

        if (Selection && !Selection->GetObjectPath().empty())
        {
            PObject* Object = FindEditorWorldObjectByPath(
                World, Selection->GetObjectPath());
            if (Object)
            {
                FAgentKnowledgeRecord Record;
                Record.SourceType = "selection";
                Record.SourcePath = Object->GetPathName();
                Record.Title = "Current editor selection";
                Record.Content = DescribeObject(Object, true).dump();
                Record.Tags = {"selection", "reflection", "property"};
                Record.Provenance = "Live editor selection and PProperty metadata";
                Result.push_back(std::move(Record));
            }
        }

        const std::vector<FLogRecord> LogRecords = FLog::GetRecordsSince(0);
        FJson Issues = FJson::array();
        std::uint64_t LatestIssueSequence = 0;
        const std::size_t Start = LogRecords.size() > 100
            ? LogRecords.size() - 100 : 0;
        for (std::size_t Index = Start; Index < LogRecords.size(); ++Index)
        {
            const FLogRecord& Log = LogRecords[Index];
            if (Log.Level != ELogLevel::Warning && Log.Level != ELogLevel::Error)
                continue;
            LatestIssueSequence = std::max(LatestIssueSequence, Log.Sequence);
            Issues.push_back({{"sequence", Log.Sequence},
                {"severity", Log.Level == ELogLevel::Error ? "Error" : "Warning"},
                {"category", Log.Category}, {"message", Log.Message}});
        }
        if (!Issues.empty())
        {
            FAgentKnowledgeRecord Record;
            Record.SourceType = "message-log";
            Record.SourcePath = "PicoEditor/MessageLog";
            Record.Title = "Recent editor warnings and errors";
            Record.Content = Issues.dump();
            Record.SourceRevision = LatestIssueSequence;
            Record.Tags = {"log", "warning", "error", "build", "package"};
            Record.Provenance = "FLog warning/error records";
            Result.push_back(std::move(Record));
        }
        return Result;
    }

    void RegisterTools()
    {
        FAgentToolDefinition DescribeWorld;
        DescribeWorld.Name = "editor.world.describe";
        DescribeWorld.Description =
            "Read the active World actor list, classes, locations, roots, component classes, and counts; use this for live scene instances";
        DescribeWorld.Handler = [this](const FAgentToolCall& Call, const FCancellationToken*)
        {
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            if (!World) return Failure(Call, "No active World");
            std::size_t ActorCount = 0;
            std::size_t ComponentCount = 0;
            FJson Actors = FJson::array();
            for (PLevel* Level : World->GetLevels())
            {
                if (!Level) continue;
                for (PActor* Actor : Level->GetActors())
                {
                    if (!Actor) continue;
                    ++ActorCount;
                    const std::vector<PActorComponent*> Components =
                        Actor->GetComponents();
                    ComponentCount += Components.size();
                    FJson ComponentClasses = FJson::array();
                    for (PActorComponent* Component : Components)
                    {
                        if (Component && Component->GetClass())
                            ComponentClasses.push_back(
                                Component->GetClass()->GetName().ToString());
                    }
                    FJson ActorJson = {
                        {"object_path", Actor->GetPathName()},
                        {"class", Actor->GetClass()
                            ? Actor->GetClass()->GetName().ToString() : "Unknown"},
                        {"location", VectorToJson(Actor->GetActorLocation())},
                        {"root_component", Actor->GetRootComponent()
                            ? Actor->GetRootComponent()->GetPathName() : ""},
                        {"component_classes", std::move(ComponentClasses)}
                    };
                    if (Actor->IsA(PPawn::StaticClass()))
                    {
                        ActorJson["auto_possess_player"] =
                            static_cast<PPawn*>(Actor)->GetAutoPossessPlayerIndex();
                    }
                    Actors.push_back(std::move(ActorJson));
                }
            }
            return Success(Call, {{"world", World->GetPathName()},
                {"actor_count", ActorCount}, {"component_count", ComponentCount},
                {"actors", std::move(Actors)}});
        };
        bInitialized = Registry.Register(std::move(DescribeWorld));

        FAgentToolDefinition ListChanges;
        ListChanges.Name = "editor.agent.list_changes";
        ListChanges.Description =
            "List recent persisted Agent Run ChangeSets and their added or removed objects before choosing an exact RunId to revert";
        ListChanges.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FJson Changes = FJson::array();
            std::string CurrentFingerprint;
            std::size_t CurrentObjectCount = 0;
            std::string CurrentFingerprintError;
            FWorldAssetData Current;
            EWorldSerializationError CurrentWorldError = EWorldSerializationError::None;
            PWorld* CurrentWorld = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            const bool bHasCurrentFingerprint = CurrentWorld
                && CaptureWorld(*CurrentWorld, Current, &CurrentWorldError)
                && FingerprintWorld(Current, CurrentFingerprint,
                    CurrentFingerprintError);
            if (bHasCurrentFingerprint) CurrentObjectCount = Current.Objects.size();
            const std::filesystem::path Directory = HostServices.ChangeSetDirectory;
            std::error_code Error;
            if (!Directory.empty() && std::filesystem::is_directory(Directory, Error))
            {
                struct FEntry
                {
                    std::filesystem::path Path;
                    std::filesystem::file_time_type Time;
                };
                std::vector<FEntry> Entries;
                for (const auto& Entry : std::filesystem::directory_iterator(Directory, Error))
                {
                    if (Error || !Entry.is_regular_file()
                        || Entry.path().extension() != ".json")
                        continue;
                    Entries.push_back({Entry.path(), Entry.last_write_time(Error)});
                    Error.clear();
                }
                std::sort(Entries.begin(), Entries.end(),
                    [](const FEntry& Left, const FEntry& Right)
                    {
                        return Left.Time > Right.Time;
                    });
                if (Entries.size() > 20) Entries.resize(20);
                for (const FEntry& Entry : Entries)
                {
                    try
                    {
                        std::ifstream Stream(Entry.Path, std::ios::binary);
                        FJson Metadata;
                        Stream >> Metadata;
                        if (Metadata.value("format_version", 0) != 1) continue;
                        Changes.push_back({{"run_id", Metadata.value("run_id", "")},
                            {"status", Metadata.value("status", "")},
                            {"before_object_count", Metadata.value("before_object_count", 0U)},
                            {"after_object_count", Metadata.value("after_object_count", 0U)},
                            {"added_objects", Metadata.value(
                                "added_objects", std::vector<std::string> {})},
                            {"removed_objects", Metadata.value(
                                "removed_objects", std::vector<std::string> {})},
                            {"matches_current_before", bHasCurrentFingerprint
                                && CurrentFingerprint == Metadata.value(
                                    "before_fingerprint", "")},
                            {"matches_current_after", bHasCurrentFingerprint
                                && CurrentFingerprint == Metadata.value(
                                    "after_fingerprint", "")}});
                    }
                    catch (...) { }
                }
            }
            return Success(Call, {{"changes", std::move(Changes)},
                {"current_serialized_object_count", CurrentObjectCount},
                {"serialized_count_includes_world_and_levels", true},
                {"current_fingerprint_error", CurrentFingerprintError},
                {"last_recording_error", LastChangeSetError}});
        };
        bInitialized = Registry.Register(std::move(ListChanges)) && bInitialized;

        FAgentToolDefinition RevertRun;
        RevertRun.Name = "editor.agent.revert_run";
        RevertRun.Description =
            "Restore the exact World snapshot from before one Agent Run when the current World still matches that Run's recorded after-state";
        RevertRun.Permission = EAgentToolPermission::ModifyWorld;
        RevertRun.Schema.Fields = {
            {"run_id", EAgentToolValueType::String, true, {}, {}, 128}
        };
        RevertRun.Preflight = [this](
            const FAgentToolCall& Call, std::string& Error)
        {
            const std::string RunId = FJson::parse(Call.ArgumentsJson)
                .at("run_id").get<std::string>();
            if (!IsSafeRunId(RunId) || HostServices.ChangeSetDirectory.empty())
            {
                Error = "Agent ChangeSet RunId is invalid or unavailable";
                return false;
            }
            FJson Metadata;
            try
            {
                std::ifstream Stream(
                    HostServices.ChangeSetDirectory / (RunId + ".json"),
                    std::ios::binary);
                Stream >> Metadata;
            }
            catch (...)
            {
                Error = "Agent ChangeSet metadata was not found";
                return false;
            }
            if (Metadata.value("format_version", 0) != 1
                || Metadata.value("run_id", "") != RunId)
            {
                Error = "Agent ChangeSet metadata is invalid";
                return false;
            }
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            FWorldAssetData Current;
            EWorldSerializationError WorldError = EWorldSerializationError::None;
            if (!World || !CaptureWorld(*World, Current, &WorldError))
            {
                Error = "Could not capture current World before revert";
                return false;
            }
            std::string CurrentFingerprint;
            if (!FingerprintWorld(Current, CurrentFingerprint, Error)) return false;
            if (CurrentFingerprint == Metadata.value("before_fingerprint", ""))
            {
                Error = "World already matches the state before this Agent Run; "
                    "Undo or an earlier restore already completed the requested recovery";
                return false;
            }
            if (CurrentFingerprint != Metadata.value("after_fingerprint", ""))
            {
                Error = "World no longer matches this Agent Run's after-state; "
                    "later edits or Undo changed it, so revert would overwrite other work";
                return false;
            }
            return true;
        };
        RevertRun.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const std::string RunId = FJson::parse(Call.ArgumentsJson)
                .at("run_id").get<std::string>();
            if (!IsSafeRunId(RunId) || HostServices.ChangeSetDirectory.empty())
                return Failure(Call, "Agent ChangeSet RunId is invalid or unavailable");
            const std::filesystem::path MetadataPath =
                HostServices.ChangeSetDirectory / (RunId + ".json");
            FJson Metadata;
            try
            {
                std::ifstream Stream(MetadataPath, std::ios::binary);
                Stream >> Metadata;
            }
            catch (...)
            {
                return Failure(Call, "Agent ChangeSet metadata was not found");
            }
            if (Metadata.value("format_version", 0) != 1
                || Metadata.value("run_id", "") != RunId)
                return Failure(Call, "Agent ChangeSet metadata is invalid");
            const std::filesystem::path BeforeFile =
                Metadata.value("before_file", "");
            const std::filesystem::path AfterFile =
                Metadata.value("after_file", "");
            if (BeforeFile.empty() || BeforeFile != BeforeFile.filename()
                || AfterFile.empty() || AfterFile != AfterFile.filename())
                return Failure(Call, "Agent ChangeSet snapshot paths are invalid");
            FWorldAssetData Before;
            FWorldAssetData After;
            EWorldSerializationError WorldError = EWorldSerializationError::None;
            if (!LoadWorldAssetDataFromFile(
                    HostServices.ChangeSetDirectory / BeforeFile, Before, &WorldError)
                || !LoadWorldAssetDataFromFile(
                    HostServices.ChangeSetDirectory / AfterFile, After, &WorldError))
                return Failure(Call, "Could not load Agent ChangeSet snapshots: "
                    + std::string(ToString(WorldError)));
            std::string StoredAfterFingerprint;
            std::string FingerprintError;
            if (!FingerprintWorld(After, StoredAfterFingerprint, FingerprintError)
                || StoredAfterFingerprint != Metadata.value("after_fingerprint", ""))
                return Failure(Call, "Agent ChangeSet after-snapshot is inconsistent");
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            FWorldAssetData Current;
            if (!World || !CaptureWorld(*World, Current, &WorldError))
                return Failure(Call, "Could not capture current World before revert");
            std::string CurrentFingerprint;
            if (!FingerprintWorld(Current, CurrentFingerprint, FingerprintError))
                return Failure(Call, FingerprintError);
            const std::string ExpectedAfter = Metadata.value("after_fingerprint", "");
            if (CurrentFingerprint != ExpectedAfter)
                return Failure(Call,
                    "World changed after this Agent Run; refusing to overwrite later edits");
            const FEditorWorldSnapshot Snapshot {Before,
                Metadata.value("selected_object_paths", std::vector<std::string> {}),
                Metadata.value("primary_object_path", "")};
            const bool bRestored = HostServices.RestoreSnapshot
                ? HostServices.RestoreSnapshot(Snapshot, &WorldError)
                : EngineLoop->ReplaceWorld(Before, &WorldError);
            if (!bRestored)
                return Failure(Call, "Could not restore Agent ChangeSet: "
                    + std::string(ToString(WorldError)));
            if (!HostServices.RestoreSnapshot && Selection)
                Selection->Restore(EngineLoop->GetWorld(),
                    Metadata.value("selected_object_paths", std::vector<std::string> {}),
                    Metadata.value("primary_object_path", ""));
            return Success(Call, {{"reverted_run_id", RunId},
                {"restored_fingerprint", Metadata.value("before_fingerprint", "")},
                {"restored_object_count", Before.Objects.size()}});
        };
        RevertRun.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            FWorldAssetData Current;
            EWorldSerializationError WorldError = EWorldSerializationError::None;
            if (!World || !CaptureWorld(*World, Current, &WorldError))
            {
                Error = "Could not verify reverted World";
                return false;
            }
            std::string Fingerprint;
            if (!FingerprintWorld(Current, Fingerprint, Error)
                || Fingerprint != FJson::parse(Result.OutputJson)
                    .at("restored_fingerprint").get<std::string>())
            {
                if (Error.empty()) Error = "Reverted World fingerprint does not match";
                return false;
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(RevertRun)) && bInitialized;

        FAgentToolDefinition DescribeSelection;
        DescribeSelection.Name = "editor.selection.describe";
        DescribeSelection.Description = "Read the current editor object selection";
        DescribeSelection.Handler = [this](const FAgentToolCall& Call, const FCancellationToken*)
        {
            return Success(Call, {{"primary", Selection ? Selection->GetObjectPath() : ""},
                {"objects", Selection ? Selection->GetObjectPaths() : std::vector<std::string> {}}});
        };
        bInitialized = Registry.Register(std::move(DescribeSelection)) && bInitialized;

        FAgentToolDefinition SearchAssets;
        SearchAssets.Name = "editor.asset.search";
        SearchAssets.Description =
            "Search registered project assets by case-insensitive path text and optional exact asset type; returns at most 50 results";
        SearchAssets.Schema.Fields = {
            {"query", EAgentToolValueType::String, true, {}, {}, 128},
            {"type", EAgentToolValueType::String, false, {}, {}, 64}
        };
        SearchAssets.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            std::string Query = Arguments.at("query").get<std::string>();
            std::string Type = Arguments.value("type", "Any");
            std::transform(Query.begin(), Query.end(), Query.begin(),
                [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
            std::transform(Type.begin(), Type.end(), Type.begin(),
                [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
            FJson Assets = FJson::array();
            if (EngineLoop)
            {
                for (const FAssetRecord& Record : EngineLoop->GetAssetRegistry().GetAssets())
                {
                    std::string Path(Record.AssetPath.ToString());
                    std::string LowerPath = Path;
                    std::transform(LowerPath.begin(), LowerPath.end(), LowerPath.begin(),
                        [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
                    std::string RecordType(ToString(Record.Type));
                    std::transform(RecordType.begin(), RecordType.end(), RecordType.begin(),
                        [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
                    if ((Query.empty() || LowerPath.find(Query) != std::string::npos)
                        && (Type == "any" || Type.empty() || RecordType == Type))
                    {
                        Assets.push_back({{"path", Path}, {"type", ToString(Record.Type)}});
                        if (Assets.size() == 50) break;
                    }
                }
            }
            return Success(Call, {{"assets", std::move(Assets)}});
        };
        bInitialized = Registry.Register(std::move(SearchAssets)) && bInitialized;

        FAgentToolDefinition DescribeObjectTool;
        DescribeObjectTool.Name = "editor.object.describe";
        DescribeObjectTool.Description =
            "Describe an object, its reflected editable properties and current values; Actors also include component objects. Call this before setting properties";
        DescribeObjectTool.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512}
        };
        DescribeObjectTool.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Arguments.at("object_path").get<std::string>());
            if (!Object) return Failure(Call, "Object path was not found in the active World");
            return Success(Call, DescribeObject(Object, true));
        };
        bInitialized = Registry.Register(std::move(DescribeObjectTool)) && bInitialized;

        FAgentToolDefinition GetProperty;
        GetProperty.Name = "editor.object.get_property";
        GetProperty.Description =
            "Read one reflected property value after discovering its exact object path and property name";
        GetProperty.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512},
            {"property_name", EAgentToolValueType::String, true, {}, {}, 128}
        };
        GetProperty.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Arguments.at("object_path").get<std::string>());
            const std::string PropertyName =
                Arguments.at("property_name").get<std::string>();
            const PProperty* Property = Object != nullptr && Object->GetClass() != nullptr
                ? Object->GetClass()->FindProperty(FName(PropertyName)) : nullptr;
            if (!Object || !Property
                || !Property->HasAnyFlags(
                    EPropertyFlags::Editable | EPropertyFlags::ReadOnly))
            {
                return Failure(Call, "Reflected editor property was not found");
            }
            return Success(Call, {{"object_path", Object->GetPathName()},
                {"property_name", PropertyName},
                {"property", DescribeProperty(*Property, Object)}});
        };
        bInitialized = Registry.Register(std::move(GetProperty)) && bInitialized;

        FAgentToolDefinition SetProperties;
        SetProperties.Name = "editor.object.set_properties";
        SetProperties.Description =
            "Set up to 32 reflected Editable properties on one World object in one approved Undo transaction. Use editor.object.describe first and preserve unmodified fields of compound values";
        SetProperties.Permission = EAgentToolPermission::ModifyWorld;
        SetProperties.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512},
            {"properties", EAgentToolValueType::Object, true}
        };
        SetProperties.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Arguments.at("object_path").get<std::string>());
            const FJson& PropertyValues = Arguments.at("properties");
            if (!Object) return Failure(Call, "Object path was not found in the active World");
            if (PropertyValues.empty() || PropertyValues.size() > 32)
                return Failure(Call, "Properties must contain between 1 and 32 entries");

            struct FPendingValue
            {
                const PProperty* Property = nullptr;
                FEditorPropertyValue Value;
            };
            std::vector<FPendingValue> Pending;
            Pending.reserve(PropertyValues.size());
            for (auto It = PropertyValues.begin(); It != PropertyValues.end(); ++It)
            {
                if (It.key().empty() || It.key().size() > 128)
                    return Failure(Call, "Property name is invalid");
                const PProperty* Property = Object->GetClass()->FindProperty(FName(It.key()));
                if (!Property)
                    return Failure(Call, "Unknown reflected property: " + It.key());
                FEditorPropertyValue Value;
                std::string Error;
                if (!JsonToPropertyValue(*Property, It.value(), Value, Error))
                    return Failure(Call, It.key() + ": " + Error);
                Pending.push_back({Property, std::move(Value)});
            }

            FJson Applied = FJson::object();
            for (FPendingValue& Entry : Pending)
            {
                const FEditorPropertyResult Result = ApplyEditorPropertyValue(
                    EngineLoop, Object, Entry.Property, Entry.Value);
                if (!Result.bSucceeded)
                    return Failure(Call, Entry.Property->GetName().ToString()
                        + ": " + Result.Message);
                FJson Value;
                if (!PropertyValueToJson(*Entry.Property, Object, Value))
                    return Failure(Call, "Could not read back changed property");
                Applied[Entry.Property->GetName().ToString()] = std::move(Value);
            }
            if (Selection) Selection->Set(Object);
            return Success(Call, {{"object_path", Object->GetPathName()},
                {"properties", std::move(Applied)}});
        };
        SetProperties.Verifier = [this](
            const FAgentToolCall&, const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Output = FJson::parse(Result.OutputJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Output.at("object_path").get<std::string>());
            if (!Object)
            {
                Error = "Changed object no longer exists";
                return false;
            }
            for (auto It = Output.at("properties").begin();
                It != Output.at("properties").end(); ++It)
            {
                const PProperty* Property =
                    Object->GetClass()->FindProperty(FName(It.key()));
                FJson Actual;
                if (!Property || !PropertyValueToJson(*Property, Object, Actual)
                    || !JsonEquivalent(Actual, It.value()))
                {
                    Error = "Property postcondition failed: " + It.key();
                    return false;
                }
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(SetProperties)) && bInitialized;

        FAgentToolDefinition BatchSetProperties;
        BatchSetProperties.Name = "editor.object.batch_set_properties";
        BatchSetProperties.Description =
            "Set reflected Editable properties on up to 32 explicit World objects in one approved all-or-nothing Undo transaction";
        BatchSetProperties.Permission = EAgentToolPermission::ModifyWorld;
        BatchSetProperties.Schema.Fields = {
            {"edits", EAgentToolValueType::Array, true}
        };
        BatchSetProperties.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            const FJson& Edits = Arguments.at("edits");
            if (Edits.empty() || Edits.size() > 32)
                return Failure(Call, "Edits must contain between 1 and 32 objects");
            struct FPendingProperty
            {
                const PProperty* Property = nullptr;
                FEditorPropertyValue Value;
            };
            struct FPendingObject
            {
                PObject* Object = nullptr;
                std::string Path;
                std::vector<FPendingProperty> Properties;
            };
            std::vector<FPendingObject> PendingObjects;
            std::unordered_set<std::string> SeenPaths;
            std::size_t TotalProperties = 0;
            for (const FJson& Edit : Edits)
            {
                if (!Edit.is_object() || Edit.size() != 2
                    || !Edit.contains("object_path") || !Edit.at("object_path").is_string()
                    || !Edit.contains("properties") || !Edit.at("properties").is_object())
                    return Failure(Call, "Each edit requires object_path and properties only");
                const std::string Path = Edit.at("object_path").get<std::string>();
                if (Path.empty() || Path.size() > 512 || !SeenPaths.insert(Path).second)
                    return Failure(Call, "Edit object paths must be unique and valid");
                PObject* Object = FindEditorWorldObjectByPath(
                    EngineLoop ? EngineLoop->GetWorld() : nullptr, Path);
                const FJson& Values = Edit.at("properties");
                if (!Object || Values.empty() || Values.size() > 32
                    || TotalProperties + Values.size() > 128)
                    return Failure(Call, "Batch property target or property count is invalid");
                FPendingObject Pending;
                Pending.Object = Object;
                Pending.Path = Path;
                for (auto It = Values.begin(); It != Values.end(); ++It)
                {
                    if (It.key().empty() || It.key().size() > 128)
                        return Failure(Call, "Property name is invalid");
                    const PProperty* Property = Object->GetClass()->FindProperty(FName(It.key()));
                    if (!Property)
                        return Failure(Call, Path + ": unknown property " + It.key());
                    FEditorPropertyValue Value;
                    std::string Error;
                    if (!JsonToPropertyValue(*Property, It.value(), Value, Error))
                        return Failure(Call, Path + "." + It.key() + ": " + Error);
                    Pending.Properties.push_back({Property, std::move(Value)});
                }
                TotalProperties += Values.size();
                PendingObjects.push_back(std::move(Pending));
            }

            FJson AppliedObjects = FJson::array();
            for (FPendingObject& Pending : PendingObjects)
            {
                FJson Applied = FJson::object();
                for (FPendingProperty& Entry : Pending.Properties)
                {
                    const FEditorPropertyResult Result = ApplyEditorPropertyValue(
                        EngineLoop, Pending.Object, Entry.Property, Entry.Value);
                    if (!Result.bSucceeded)
                        return Failure(Call, Pending.Path + "."
                            + Entry.Property->GetName().ToString() + ": " + Result.Message);
                    FJson Value;
                    if (!PropertyValueToJson(*Entry.Property, Pending.Object, Value))
                        return Failure(Call, "Could not read back batch property value");
                    Applied[Entry.Property->GetName().ToString()] = std::move(Value);
                }
                AppliedObjects.push_back(
                    {{"object_path", Pending.Path}, {"properties", std::move(Applied)}});
            }
            if (Selection && !PendingObjects.empty())
                Selection->Set(PendingObjects.back().Object);
            return Success(Call, {{"objects", std::move(AppliedObjects)}});
        };
        BatchSetProperties.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Objects = FJson::parse(Result.OutputJson).at("objects");
            for (const FJson& Entry : Objects)
            {
                PObject* Object = FindEditorWorldObjectByPath(
                    EngineLoop ? EngineLoop->GetWorld() : nullptr,
                    Entry.at("object_path").get<std::string>());
                if (!Object)
                {
                    Error = "Batch changed object no longer exists";
                    return false;
                }
                for (auto It = Entry.at("properties").begin();
                    It != Entry.at("properties").end(); ++It)
                {
                    const PProperty* Property =
                        Object->GetClass()->FindProperty(FName(It.key()));
                    FJson Actual;
                    if (!Property || !PropertyValueToJson(*Property, Object, Actual)
                        || !JsonEquivalent(Actual, It.value()))
                    {
                        Error = "Batch property postcondition failed: " + It.key();
                        return false;
                    }
                }
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(BatchSetProperties)) && bInitialized;

        FAgentToolDefinition SpawnActor;
        SpawnActor.Name = "editor.actor.spawn";
        SpawnActor.Description = "Create an Empty or Cube Actor in the active World";
        SpawnActor.Permission = EAgentToolPermission::ModifyWorld;
        SpawnActor.Schema.Fields = {
            {"name", EAgentToolValueType::String, true, {}, {}, 64},
            {"kind", EAgentToolValueType::String, true, {}, {}, 16}
        };
        SpawnActor.Handler = [this](const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            const std::string Name = Arguments.at("name").get<std::string>();
            const std::string Kind = Arguments.at("kind").get<std::string>();
            if (!IsSafeObjectName(Name) || (Kind != "Empty" && Kind != "Cube"))
            {
                return Failure(Call, "Actor name or kind is invalid");
            }
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            PActor* Actor = World ? World->SpawnActor<PActor>(Name) : nullptr;
            if (!Actor) return Failure(Call, "Could not create Actor");
            PSceneComponent* Root = Kind == "Cube"
                ? static_cast<PSceneComponent*>(Actor->CreateComponent<PCubeComponent>("CubeComponent"))
                : Actor->CreateComponent<PSceneComponent>("DefaultSceneRoot");
            if (!Root || !Actor->SetRootComponent(Root))
            {
                if (World) World->DestroyActor(Actor);
                return Failure(Call, "Could not create Actor root component");
            }
            if (Selection) Selection->Set(Actor);
            return Success(Call, {{"object_path", Actor->GetPathName()}, {"kind", Kind},
                {"root_component_path", Root->GetPathName()}});
        };
        SpawnActor.Verifier = [this](const FAgentToolCall&, const FAgentToolResult& Result, std::string& Error)
        {
            const std::string Path = FJson::parse(Result.OutputJson).at("object_path").get<std::string>();
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr, Path);
            if (!Object || !Object->IsA(PActor::StaticClass())
                || !static_cast<PActor*>(Object)->GetRootComponent())
            {
                Error = "Created Actor postcondition was not satisfied";
                return false;
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(SpawnActor)) && bInitialized;

        FAgentToolDefinition SpawnBlueprint;
        SpawnBlueprint.Name = "editor.actor.spawn_blueprint";
        SpawnBlueprint.Description =
            "Spawn a normal instance of a registered Actor Blueprint at a location. Pawn instances default to Auto Possess disabled, so use this for additional characters or NPCs";
        SpawnBlueprint.Permission = EAgentToolPermission::ModifyWorld;
        SpawnBlueprint.Schema.Fields = {
            {"blueprint_asset", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"name", EAgentToolValueType::String, true, {}, {}, 64},
            {"x", EAgentToolValueType::Number, true, -1000000.0, 1000000.0},
            {"y", EAgentToolValueType::Number, true, -1000000.0, 1000000.0},
            {"z", EAgentToolValueType::Number, true, -1000000.0, 1000000.0},
            {"auto_possess_player", EAgentToolValueType::Integer, false, -1.0, 3.0}
        };
        SpawnBlueprint.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            const std::string Name = Arguments.at("name").get<std::string>();
            FAssetPath BlueprintPath;
            if (!IsSafeObjectName(Name)
                || !FAssetPath::TryParse(
                    Arguments.at("blueprint_asset").get<std::string>(), BlueprintPath)
                || BlueprintPath.GetExtension() != ".pblueprint")
            {
                return Failure(Call, "Actor name or Blueprint asset path is invalid");
            }
            const FAssetRecord* Record = EngineLoop
                ? EngineLoop->GetAssetRegistry().Find(BlueprintPath) : nullptr;
            const PClass* ActorClass = FindActorBlueprintGeneratedClass(BlueprintPath);
            if (!Record || Record->Type != EAssetType::ActorBlueprint
                || !ActorClass || !ActorClass->IsChildOf(PActor::StaticClass())
                || !ActorClass->CanConstruct())
            {
                return Failure(Call,
                    "Blueprint is not a registered constructible Actor asset");
            }
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            PActor* Actor = World ? World->SpawnActor(ActorClass, Name) : nullptr;
            if (!Actor) return Failure(Call, "Could not spawn Blueprint Actor");
            if (Actor->IsA(PPawn::StaticClass()))
            {
                static_cast<PPawn*>(Actor)->SetAutoPossessPlayerIndex(
                    Arguments.value("auto_possess_player", -1));
            }
            const FVector3 Location(Arguments.at("x").get<float>(),
                Arguments.at("y").get<float>(), Arguments.at("z").get<float>());
            if (!Actor->SetActorLocation(Location))
            {
                World->DestroyActor(Actor);
                return Failure(Call, "Blueprint Actor has no movable scene root");
            }
            if (Selection) Selection->Set(Actor);
            return Success(Call, {{"object_path", Actor->GetPathName()},
                {"class", ActorClass->GetName().ToString()},
                {"blueprint_asset", BlueprintPath.ToString()},
                {"auto_possess_player", Actor->IsA(PPawn::StaticClass())
                    ? static_cast<PPawn*>(Actor)->GetAutoPossessPlayerIndex() : -1},
                {"location", VectorToJson(Location)}});
        };
        SpawnBlueprint.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Output = FJson::parse(Result.OutputJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Output.at("object_path").get<std::string>());
            FAssetPath BlueprintPath;
            const bool bValidPath = FAssetPath::TryParse(
                Output.at("blueprint_asset").get<std::string>(), BlueprintPath);
            if (!Object || !Object->IsA(PActor::StaticClass()) || !bValidPath
                || Object->GetClass() != FindActorBlueprintGeneratedClass(BlueprintPath))
            {
                Error = "Spawned Blueprint Actor postcondition failed";
                return false;
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(SpawnBlueprint)) && bInitialized;

        FAgentToolDefinition DeleteActor;
        DeleteActor.Name = "editor.actor.delete";
        DeleteActor.Description =
            "Delete one Actor from the active World by stable object path as an Undoable operation";
        DeleteActor.Permission = EAgentToolPermission::ModifyWorld;
        DeleteActor.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512}
        };
        DeleteActor.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const std::string Path = FJson::parse(Call.ArgumentsJson)
                .at("object_path").get<std::string>();
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            PObject* Object = FindEditorWorldObjectByPath(World, Path);
            if (!World || !Object || !Object->IsA(PActor::StaticClass()))
                return Failure(Call, "Actor object path was not found in the active World");
            if (!World->DestroyActor(static_cast<PActor*>(Object)))
                return Failure(Call, "Could not delete Actor");
            if (Selection) Selection->Set(World);
            return Success(Call, {{"deleted_object_path", Path}});
        };
        DeleteActor.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const std::string Path = FJson::parse(Result.OutputJson)
                .at("deleted_object_path").get<std::string>();
            if (FindEditorWorldObjectByPath(
                    EngineLoop ? EngineLoop->GetWorld() : nullptr, Path))
            {
                Error = "Deleted Actor still resolves in the active World";
                return false;
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(DeleteActor)) && bInitialized;

        FAgentToolDefinition DeleteActors;
        DeleteActors.Name = "editor.actor.delete_many";
        DeleteActors.Description =
            "Delete up to 64 explicit Actors from the active World in one approved all-or-nothing Undo transaction";
        DeleteActors.Permission = EAgentToolPermission::ModifyWorld;
        DeleteActors.Schema.Fields = {
            {"object_paths", EAgentToolValueType::Array, true}
        };
        DeleteActors.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            const FJson& Paths = Arguments.at("object_paths");
            if (Paths.empty() || Paths.size() > 64)
                return Failure(Call, "object_paths must contain between 1 and 64 Actors");
            std::vector<FObjectHandle> Handles;
            std::vector<std::string> StablePaths;
            std::unordered_set<std::string> Seen;
            for (const FJson& Value : Paths)
            {
                if (!Value.is_string())
                    return Failure(Call, "Every delete target must be an object path string");
                const std::string Path = Value.get<std::string>();
                if (Path.empty() || Path.size() > 512 || !Seen.insert(Path).second)
                    return Failure(Call, "Delete target paths must be unique and valid");
                PObject* Object = FindEditorWorldObjectByPath(
                    EngineLoop ? EngineLoop->GetWorld() : nullptr, Path);
                if (!Object || !Object->IsA(PActor::StaticClass()))
                    return Failure(Call, "Actor delete target was not found: " + Path);
                Handles.push_back(Object->GetHandle());
                StablePaths.push_back(Path);
            }
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            if (!World) return Failure(Call, "Active World is unavailable");
            for (FObjectHandle Handle : Handles)
            {
                PObject* Object = ResolveObject(Handle);
                PActor* Actor = Object && Object->IsA(PActor::StaticClass())
                    ? static_cast<PActor*>(Object) : nullptr;
                if (!Actor || Actor->GetWorld() != World || !World->DestroyActor(Actor))
                    return Failure(Call, "Could not delete every requested Actor");
            }
            if (Selection) Selection->Set(World);
            return Success(Call, {{"deleted_object_paths", StablePaths},
                {"deleted_count", StablePaths.size()}});
        };
        DeleteActors.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Paths = FJson::parse(Result.OutputJson)
                .at("deleted_object_paths");
            for (const FJson& Path : Paths)
                if (FindEditorWorldObjectByPath(
                        EngineLoop ? EngineLoop->GetWorld() : nullptr,
                        Path.get<std::string>()))
                {
                    Error = "A batch-deleted Actor still resolves: "
                        + Path.get<std::string>();
                    return false;
                }
            return true;
        };
        bInitialized = Registry.Register(std::move(DeleteActors)) && bInitialized;

        FAgentToolDefinition CreateRoom;
        CreateRoom.Name = "editor.scene.create_room";
        CreateRoom.Description =
            "Create a collision-enabled floor and four walls centered at the requested location as one Undoable scene operation";
        CreateRoom.Permission = EAgentToolPermission::ModifyWorld;
        CreateRoom.Schema.Fields = {
            {"name", EAgentToolValueType::String, true, {}, {}, 48},
            {"center_x", EAgentToolValueType::Number, true, -100000.0, 100000.0},
            {"center_y", EAgentToolValueType::Number, true, -100000.0, 100000.0},
            {"width", EAgentToolValueType::Number, true, 200.0, 100000.0},
            {"depth", EAgentToolValueType::Number, true, 200.0, 100000.0},
            {"wall_height", EAgentToolValueType::Number, true, 100.0, 10000.0}
        };
        CreateRoom.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            const std::string BaseName = Arguments.at("name").get<std::string>();
            if (!IsSafeObjectName(BaseName)) return Failure(Call, "Room name is invalid");
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            if (!World) return Failure(Call, "No active World");
            const float X = Arguments.at("center_x").get<float>();
            const float Y = Arguments.at("center_y").get<float>();
            const float Width = Arguments.at("width").get<float>();
            const float Depth = Arguments.at("depth").get<float>();
            const float Height = Arguments.at("wall_height").get<float>();
            constexpr float Thickness = 20.0f;
            FJson Paths = FJson::array();
            auto SpawnPart = [&](const std::string& Suffix, const FVector3& Location,
                                 const FVector3& Extent, const FVector3& Color)
            {
                PActor* Actor = nullptr;
                for (int Index = 1; Index < 1000 && !Actor; ++Index)
                    Actor = World->SpawnActor<PActor>(BaseName + "_" + Suffix
                        + (Index == 1 ? "" : "_" + std::to_string(Index)));
                if (!Actor) return false;
                PCubeComponent* Cube = Actor->CreateComponent<PCubeComponent>("CubeComponent");
                if (!Cube || !Actor->SetRootComponent(Cube))
                {
                    World->DestroyActor(Actor);
                    return false;
                }
                Cube->SetExtent(Extent);
                Cube->SetColor(Color);
                Cube->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                Cube->SetPhysicsBodyType(EPhysicsBodyType::Static);
                Actor->SetActorLocation(Location);
                Paths.push_back(Actor->GetPathName());
                return true;
            };
            const bool bCreated =
                SpawnPart("Floor", {X, Y, -Thickness * 0.5f},
                    {Width * 0.5f, Depth * 0.5f, Thickness * 0.5f},
                    {0.46f, 0.48f, 0.50f})
                && SpawnPart("WallNorth", {X, Y + Depth * 0.5f, Height * 0.5f},
                    {Width * 0.5f, Thickness * 0.5f, Height * 0.5f},
                    {0.58f, 0.60f, 0.64f})
                && SpawnPart("WallSouth", {X, Y - Depth * 0.5f, Height * 0.5f},
                    {Width * 0.5f, Thickness * 0.5f, Height * 0.5f},
                    {0.58f, 0.60f, 0.64f})
                && SpawnPart("WallEast", {X + Width * 0.5f, Y, Height * 0.5f},
                    {Thickness * 0.5f, Depth * 0.5f, Height * 0.5f},
                    {0.58f, 0.60f, 0.64f})
                && SpawnPart("WallWest", {X - Width * 0.5f, Y, Height * 0.5f},
                    {Thickness * 0.5f, Depth * 0.5f, Height * 0.5f},
                    {0.58f, 0.60f, 0.64f});
            if (!bCreated) return Failure(Call, "Could not create every room part");
            return Success(Call, {{"actors", std::move(Paths)}, {"parts", 5}});
        };
        bInitialized = Registry.Register(std::move(CreateRoom)) && bInitialized;

        FAgentToolDefinition CreateThirdPerson;
        CreateThirdPerson.Name = "editor.gameplay.create_third_person_character";
        CreateThirdPerson.Description =
            "Create one auto-possessed third-person Pawn from an Actor Blueprint and Character Profile, plus a PlayerStart";
        CreateThirdPerson.Permission = EAgentToolPermission::ModifyWorld;
        CreateThirdPerson.Schema.Fields = {
            {"blueprint_asset", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"character_profile", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"x", EAgentToolValueType::Number, true, -100000.0, 100000.0},
            {"y", EAgentToolValueType::Number, true, -100000.0, 100000.0},
            {"z", EAgentToolValueType::Number, true, -100000.0, 100000.0}
        };
        CreateThirdPerson.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath BlueprintPath;
            FAssetPath ProfilePath;
            if (!FAssetPath::TryParse(Arguments.at("blueprint_asset").get<std::string>(), BlueprintPath)
                || !FAssetPath::TryParse(Arguments.at("character_profile").get<std::string>(), ProfilePath)
                || BlueprintPath.GetExtension() != ".pblueprint"
                || ProfilePath.GetExtension() != ".pcharprofile")
            {
                return Failure(Call, "Blueprint or Character Profile asset path is invalid");
            }
            const PClass* PawnClass = FindActorBlueprintGeneratedClass(BlueprintPath);
            if (!PawnClass || !PawnClass->IsChildOf(PPawn::StaticClass())
                || !PawnClass->CanConstruct())
            {
                return Failure(Call, "Actor Blueprint does not generate a constructible Pawn class");
            }
            PWorld* World = EngineLoop ? EngineLoop->GetWorld() : nullptr;
            if (!World) return Failure(Call, "No active World");
            for (PLevel* Level : World->GetLevels())
                if (Level) for (PActor* Actor : Level->GetActors())
                    if (Actor && Actor->IsA(PPawn::StaticClass())
                        && static_cast<PPawn*>(Actor)->GetAutoPossessPlayerIndex() == 0)
                        return Failure(Call, "Player 0 already has an auto-possessed Pawn");
            PActor* Spawned = World->SpawnActor(PawnClass, "AI_ThirdPersonCharacter");
            if (!Spawned) return Failure(Call, "Could not spawn Blueprint Pawn");
            PPawn* Pawn = static_cast<PPawn*>(Spawned);
            Pawn->SetAutoPossessPlayerIndex(0);
            PSkeletalMeshComponent* Mesh = nullptr;
            for (PActorComponent* Component : Pawn->GetComponents())
                if (Component && Component->IsA(PSkeletalMeshComponent::StaticClass()))
                {
                    Mesh = static_cast<PSkeletalMeshComponent*>(Component);
                    break;
                }
            if (!Mesh)
            {
                World->DestroyActor(Pawn);
                return Failure(Call, "Blueprint Pawn has no Skeletal Mesh Component");
            }
            Mesh->SetCharacterProfileAsset(ProfilePath);
            Pawn->SetActorLocation({Arguments.at("x").get<float>(),
                Arguments.at("y").get<float>(), Arguments.at("z").get<float>()});
            PPlayerStart* Start = World->SpawnActor<PPlayerStart>("AI_PlayerStart");
            if (!Start)
            {
                World->DestroyActor(Pawn);
                return Failure(Call, "Could not create PlayerStart");
            }
            Start->SetActorLocation(Pawn->GetActorLocation());
            if (Selection) Selection->Set(Pawn);
            return Success(Call, {{"pawn", Pawn->GetPathName()},
                {"player_start", Start->GetPathName()},
                {"profile", ProfilePath.ToString()}});
        };
        bInitialized = Registry.Register(std::move(CreateThirdPerson)) && bInitialized;

        FAgentToolDefinition SetLocation;
        SetLocation.Name = "editor.actor.set_location";
        SetLocation.Description = "Set an existing Actor world location";
        SetLocation.Permission = EAgentToolPermission::ModifyWorld;
        SetLocation.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512},
            {"x", EAgentToolValueType::Number, true, -1000000.0, 1000000.0},
            {"y", EAgentToolValueType::Number, true, -1000000.0, 1000000.0},
            {"z", EAgentToolValueType::Number, true, -1000000.0, 1000000.0}
        };
        SetLocation.Handler = [this](const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Arguments.at("object_path").get<std::string>());
            if (!Object || !Object->IsA(PActor::StaticClass()))
            {
                return Failure(Call, "Actor object path was not found");
            }
            const FVector3 Location(
                Arguments.at("x").get<float>(), Arguments.at("y").get<float>(),
                Arguments.at("z").get<float>());
            PActor* Actor = static_cast<PActor*>(Object);
            if (!Actor->SetActorLocation(Location))
            {
                return Failure(Call, "Actor has no movable scene root");
            }
            if (Selection) Selection->Set(Actor);
            return Success(Call, {{"object_path", Actor->GetPathName()},
                {"x", Location.X}, {"y", Location.Y}, {"z", Location.Z}});
        };
        SetLocation.Verifier = [this](const FAgentToolCall&, const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Output = FJson::parse(Result.OutputJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Output.at("object_path").get<std::string>());
            const FVector3 Expected(Output.at("x").get<float>(),
                Output.at("y").get<float>(), Output.at("z").get<float>());
            if (!Object || !Object->IsA(PActor::StaticClass())
                || !static_cast<PActor*>(Object)->GetActorLocation().Equals(Expected))
            {
                Error = "Actor location postcondition was not satisfied";
                return false;
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(SetLocation)) && bInitialized;

        FAgentToolDefinition ValidateGameplay;
        ValidateGameplay.Name = "editor.play.validate";
        ValidateGameplay.Description =
            "Validate the active World and gameplay object chain before Play or packaging";
        ValidateGameplay.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            if (!HostServices.Commands)
                return Failure(Call, "Editor gameplay validation service is unavailable");
            const FEditorCommandResult Result =
                HostServices.Commands->ValidateGameplayForPlay();
            return Result.bSucceeded
                ? Success(Call, {{"valid", true}, {"message", Result.Message}})
                : Failure(Call, Result.Message);
        };
        bInitialized = Registry.Register(std::move(ValidateGameplay)) && bInitialized;

        FAgentToolDefinition StartPlay;
        StartPlay.Name = "editor.play.start";
        StartPlay.Description =
            "Start the editor's configured Play Session for the active saved World and keep it running until stopped";
        StartPlay.Permission = EAgentToolPermission::LaunchProcess;
        StartPlay.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            if (!HostServices.StartPlay)
                return Failure(Call, "Editor Play service is unavailable");
            const auto Result = HostServices.StartPlay();
            return Result.first
                ? Success(Call, {{"started", true}, {"message", Result.second}})
                : Failure(Call, Result.second);
        };
        bInitialized = Registry.Register(std::move(StartPlay)) && bInitialized;

        FAgentToolDefinition StopPlay;
        StopPlay.Name = "editor.play.stop";
        StopPlay.Description =
            "Stop the currently active editor Play Session and all processes owned by it";
        StopPlay.Permission = EAgentToolPermission::LaunchProcess;
        StopPlay.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            if (!HostServices.StopPlay)
                return Failure(Call, "Editor Play service is unavailable");
            const auto Result = HostServices.StopPlay();
            return Result.first
                ? Success(Call, {{"stopped", true}, {"message", Result.second}})
                : Failure(Call, Result.second);
        };
        bInitialized = Registry.Register(std::move(StopPlay)) && bInitialized;

        FAgentToolDefinition SaveWorld;
        SaveWorld.Name = "editor.world.save";
        SaveWorld.Description =
            "Save the active World to its current project asset path";
        SaveWorld.Permission = EAgentToolPermission::WriteProject;
        SaveWorld.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            if (!HostServices.WorldDocument)
                return Failure(Call, "Editor World document service is unavailable");
            const FEditorDocumentResult Result = HostServices.WorldDocument->Save();
            return Result.bSucceeded
                ? Success(Call, {{"saved", true},
                    {"asset_path", HostServices.WorldDocument->GetAssetPath().ToString()},
                    {"file", HostServices.WorldDocument->GetFilePath().string()}})
                : Failure(Call, Result.Message);
        };
        bInitialized = Registry.Register(std::move(SaveWorld)) && bInitialized;

        FAgentToolDefinition CreateProject;
        CreateProject.Name = "editor.project.create_from_third_person_template";
        CreateProject.Description =
            "Create a content-only Pico project under Engine/Projects by copying the proven current third-person project Content and Config; never overwrites an existing project; after the completed Agent turn Pico performs a clean editor-process handoff and restores this conversation in the new project";
        CreateProject.Permission = EAgentToolPermission::WriteProject;
        CreateProject.Schema.Fields = {
            {"project_name", EAgentToolValueType::String, true, {}, {}, 48}
        };
        CreateProject.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken* CancellationToken)
        {
            const std::string Name =
                FJson::parse(Call.ArgumentsJson).at("project_name").get<std::string>();
            if (!IsSafeObjectName(Name))
                return Failure(Call, "Project name may contain only letters, digits, and '_'");
            const std::filesystem::path MaintainedTemplate =
                FPaths::GetEngineRootDir() / "Projects/PicoSandbox";
            const std::filesystem::path SourceRoot =
                std::filesystem::is_directory(MaintainedTemplate)
                    ? MaintainedTemplate : FPaths::GetProjectRootDir();
            const std::filesystem::path DestinationRoot =
                FPaths::GetEngineRootDir() / "Projects" / Name;
            const std::filesystem::path StagingParent =
                FPaths::GetEngineRootDir() / "Projects/.AgentStaging";
            const std::filesystem::path StagingRoot =
                StagingParent / (Name + "-" + StableOperationSuffix(Call.Id));
            std::error_code Error;
            if (std::filesystem::exists(DestinationRoot, Error))
            {
                FConfigFile Completion;
                FConfigFile ExistingConfig;
                const std::filesystem::path ExistingProjectFile =
                    DestinationRoot / (Name + ".pico");
                const bool bThisOperationAlreadyCommitted =
                    Completion.Load(DestinationRoot / ".PicoProject.complete")
                    && Completion.GetString("Operation", "Id", "") == Call.Id
                    && Completion.GetString("Operation", "Tool", "") == Call.Name
                    && Completion.GetString("Operation", "State", "") == "Complete"
                    && std::filesystem::is_regular_file(ExistingProjectFile)
                    && ExistingConfig.Load(DestinationRoot / "Config/Pico.ini");
                if (bThisOperationAlreadyCommitted)
                {
                    return Success(Call, {{"project_name", Name},
                        {"project_file", ExistingProjectFile.string()},
                        {"startup_map", ExistingConfig.GetString(
                            "Editor", "StartupMap", "")},
                        {"template", "ThirdPerson"},
                        {"editor_handoff", "scheduled_after_turn"},
                        {"reconciled", true}});
                }
                return Failure(Call, "A project with that name already exists");
            }
            std::filesystem::remove_all(StagingRoot, Error);
            Error.clear();
            std::filesystem::create_directories(StagingRoot, Error);
            if (Error) return Failure(Call, "Could not create project staging directory: " + Error.message());
            auto CopyTree = [&](const char* Folder)
            {
                const std::filesystem::path Source = SourceRoot / Folder;
                const std::filesystem::path Destination = StagingRoot / Folder;
                if (!std::filesystem::is_directory(Source)) return false;
                std::filesystem::copy(Source, Destination,
                    std::filesystem::copy_options::recursive, Error);
                return !Error;
            };
            if ((CancellationToken && CancellationToken->IsCancellationRequested())
                || !CopyTree("Content") || !CopyTree("Config"))
            {
                std::filesystem::remove_all(StagingRoot, Error);
                return Failure(Call, "Could not copy template Content and Config");
            }
            FConfigFile Descriptor;
            Descriptor.SetString("Project", "Name", Name);
            Descriptor.SetString("Project", "FileVersion", "1");
            Descriptor.SetString("Project", "EngineVersion", "0.1.0");
            const std::filesystem::path StagedProjectFile =
                StagingRoot / (Name + ".pico");
            if (!Descriptor.Save(StagedProjectFile))
            {
                std::filesystem::remove_all(StagingRoot, Error);
                return Failure(Call, "Could not write project descriptor");
            }
            FConfigFile Config;
            const std::filesystem::path ConfigFile = StagingRoot / "Config/Pico.ini";
            if (!Config.Load(ConfigFile))
            {
                std::filesystem::remove_all(StagingRoot, Error);
                return Failure(Call, "Copied project Config could not be loaded");
            }
            Config.SetString("Project", "Name", Name);
            if (!Config.Save(ConfigFile))
            {
                std::filesystem::remove_all(StagingRoot, Error);
                return Failure(Call, "Could not finalize project Config");
            }
            FConfigFile Completion;
            Completion.SetString("Operation", "Id", Call.Id);
            Completion.SetString("Operation", "Tool", Call.Name);
            Completion.SetString("Operation", "State", "Complete");
            if (!Completion.Save(StagingRoot / ".PicoProject.complete"))
            {
                std::filesystem::remove_all(StagingRoot, Error);
                return Failure(Call, "Could not write project completion marker");
            }
            if (CancellationToken && CancellationToken->IsCancellationRequested())
            {
                std::filesystem::remove_all(StagingRoot, Error);
                return Failure(Call, "Project creation was cancelled before commit");
            }
            std::filesystem::rename(StagingRoot, DestinationRoot, Error);
            if (Error)
            {
                const std::string RenameError = Error.message();
                Error.clear();
                std::filesystem::remove_all(StagingRoot, Error);
                return Failure(Call, "Could not commit completed project: " + RenameError);
            }
            const std::filesystem::path ProjectFile = DestinationRoot / (Name + ".pico");
            return Success(Call, {{"project_name", Name},
                {"project_file", ProjectFile.string()},
                {"startup_map", Config.GetString("Editor", "StartupMap", "")},
                {"template", "ThirdPerson"},
                {"editor_handoff", "scheduled_after_turn"}});
        };
        CreateProject.Verifier = [](const FAgentToolCall&, const FAgentToolResult& Result,
                                    std::string& Error)
        {
            const std::filesystem::path ProjectFile =
                FJson::parse(Result.OutputJson).at("project_file").get<std::string>();
            if (!std::filesystem::is_regular_file(ProjectFile))
            {
                Error = "Created project descriptor was not found";
                return false;
            }
            return true;
        };
        bInitialized = Registry.Register(std::move(CreateProject)) && bInitialized;

        FAgentToolDefinition PackageProject;
        PackageProject.Name = "editor.project.package";
        PackageProject.Description =
            "Package/export a distributable build only when the user explicitly requests packaging; never use this tool to run or preview the project";
        PackageProject.Permission = EAgentToolPermission::LaunchProcess;
        PackageProject.Schema.Fields = {
            {"output_root", EAgentToolValueType::String, true, {}, {}, 512},
            {"package_name", EAgentToolValueType::String, true, {}, {}, 64},
            {"smoke_test", EAgentToolValueType::Boolean, true}
        };
        PackageProject.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            if (!HostServices.StartPackage)
                return Failure(Call, "Editor packaging service is unavailable");
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            const std::filesystem::path OutputRoot =
                Arguments.at("output_root").get<std::string>();
            const std::string PackageName =
                Arguments.at("package_name").get<std::string>();
            if (OutputRoot.empty() || !OutputRoot.is_absolute()
                || !IsSafeObjectName(PackageName))
                return Failure(Call, "Output root or package name is invalid");
            const auto Result = HostServices.StartPackage(OutputRoot, PackageName,
                Arguments.at("smoke_test").get<bool>());
            return Result.first
                ? Success(Call, {{"state", "running"}, {"started", true},
                    {"message", Result.second},
                    {"output", (OutputRoot / PackageName).string()}})
                : Failure(Call, Result.second);
        };
        bInitialized = Registry.Register(std::move(PackageProject)) && bInitialized;
    }

    FEngineLoop* EngineLoop = nullptr;
    FEditorSelection* Selection = nullptr;
    FEditorAgentHostServices HostServices;
    FTransaction Transaction;
    FAgentToolRegistry Registry;
    std::optional<FPendingChangeSet> PendingChangeSet;
    std::string LastChangeSetError;
    bool bInitialized = true;
};

FEditorAgentToolExecutor::FEditorAgentToolExecutor(
    FEngineLoop* EngineLoop,
    FEditorSelection* Selection,
    FEditorTransactionManager* Transactions,
    IAgentToolApproval* Approval,
    std::function<void()> OnWorldChanged,
    FEditorAgentHostServices HostServices)
    : Impl(std::make_unique<FImpl>(EngineLoop, Selection, Transactions,
        Approval, std::move(OnWorldChanged), std::move(HostServices)))
{
}

FEditorAgentToolExecutor::~FEditorAgentToolExecutor() = default;
bool FEditorAgentToolExecutor::IsInitialized() const { return Impl && Impl->bInitialized; }
void FEditorAgentToolExecutor::BeginRun(std::string_view RunId)
{
    if (Impl) Impl->BeginRun(RunId);
}
void FEditorAgentToolExecutor::EndRun(std::string_view RunId, EAgentStatus Status)
{
    if (Impl) Impl->EndRun(RunId, Status);
}
std::vector<std::string> FEditorAgentToolExecutor::GetToolNames() const
{
    return Impl ? Impl->Registry.GetToolNames() : std::vector<std::string> {};
}
std::string FEditorAgentToolExecutor::BuildToolCatalogJson() const
{
    return Impl ? Impl->Registry.BuildToolCatalogJson() : "[]";
}
std::vector<FAgentKnowledgeRecord> FEditorAgentToolExecutor::CollectKnowledgeRecords() const
{
    return Impl ? Impl->CollectKnowledgeRecords()
        : std::vector<FAgentKnowledgeRecord> {};
}
const std::vector<FAgentToolStageTrace>& FEditorAgentToolExecutor::GetLastTrace() const
{
    static const std::vector<FAgentToolStageTrace> Empty;
    return Impl ? Impl->Registry.GetLastTrace() : Empty;
}
bool FEditorAgentToolExecutor::RequiresApproval(const FAgentToolCall& Call) const
{
    return Impl && Impl->Registry.RequiresApproval(Call);
}
bool FEditorAgentToolExecutor::IsReadOnly(const FAgentToolCall& Call) const
{
    return Impl && Impl->Registry.IsReadOnly(Call);
}
void FEditorAgentToolExecutor::PrepareApproval(const FAgentToolCall& Call)
{
    if (Impl) Impl->Registry.PrepareApproval(Call);
}
std::string FEditorAgentToolExecutor::GetLastExecutionTraceJson() const
{
    return Impl ? Impl->Registry.GetLastExecutionTraceJson() : "[]";
}
FAgentToolResult FEditorAgentToolExecutor::Execute(
    const FAgentToolCall& Call,
    const FCancellationToken* CancellationToken)
{
    return Impl ? Impl->Registry.Execute(Call, CancellationToken)
        : FAgentToolResult {Call.Id, false, "{}", "Editor tool executor is unavailable", false};
}

FAgentToolResult FEditorAgentToolExecutor::WaitForAsyncCompletion(
    const FAgentToolCall& Call,
    FAgentToolResult StartedResult,
    const FCancellationToken* CancellationToken) const
{
    if (!Impl || Call.Name != "editor.project.package"
        || !StartedResult.bSucceeded)
    {
        return StartedResult;
    }
    if (!Impl->HostServices.WaitForPackage)
    {
        return FAgentToolResult {Call.Id, false, "{}",
            "Package process started, but no completion service is available", false};
    }

    const FEditorAgentPackageCompletion Completion =
        Impl->HostServices.WaitForPackage(CancellationToken);
    if (!Completion.bSucceeded)
    {
        return FAgentToolResult {Call.Id, false, "{}",
            Completion.Message.empty()
                ? "Package process did not complete successfully"
                : Completion.Message,
            false};
    }
    return Success(Call, {{"state", "completed"}, {"started", true},
        {"succeeded", true}, {"exit_code", Completion.ExitCode},
        {"output", Completion.OutputDirectory.string()},
        {"message", Completion.Message}});
}
}
