#include "Pico/Editor/EditorSceneClipboard.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
void ReportError(
    EEditorClipboardError* OutError,
    EEditorClipboardError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
}

using FRecordMap =
    std::unordered_map<uint64, const FSceneObjectRecord*>;

FRecordMap BuildRecordMap(const FWorldAssetData& Data)
{
    FRecordMap Records;
    Records.reserve(Data.Objects.size());
    for (const FSceneObjectRecord& Record : Data.Objects)
    {
        Records.emplace(Record.Id.Value, &Record);
    }
    return Records;
}

const FSceneObjectRecord* FindRecord(
    const FRecordMap& Records,
    FSceneObjectId Id)
{
    const auto Found = Records.find(Id.Value);
    return Found != Records.end() ? Found->second : nullptr;
}

const PClass* FindRecordClass(const FSceneObjectRecord& Record)
{
    return FClassRegistry::FindClass(FName(Record.ClassName));
}

bool IsChildOf(
    const FSceneObjectRecord& Record,
    const PClass* BaseClass)
{
    const PClass* Class = FindRecordClass(Record);
    return Class != nullptr && Class->IsChildOf(BaseClass);
}

bool TryBuildObjectPath(
    const FRecordMap& Records,
    FSceneObjectId ObjectId,
    std::string& OutPath)
{
    std::vector<std::string_view> Names;
    std::unordered_set<uint64> Visited;
    const FSceneObjectRecord* Record = FindRecord(Records, ObjectId);
    while (Record != nullptr)
    {
        if (!Visited.insert(Record->Id.Value).second)
        {
            return false;
        }
        Names.push_back(Record->ObjectName);
        if (!Record->OuterId.IsValid())
        {
            break;
        }
        Record = FindRecord(Records, Record->OuterId);
    }
    if (Record == nullptr || Names.empty())
    {
        return false;
    }

    std::string Path;
    for (auto Name = Names.rbegin(); Name != Names.rend(); ++Name)
    {
        if (!Path.empty())
        {
            Path.push_back('.');
        }
        Path.append(*Name);
    }
    OutPath = std::move(Path);
    return true;
}

const FSceneObjectRecord* FindRecordByPath(
    const FWorldAssetData& Data,
    const FRecordMap& Records,
    std::string_view Path)
{
    for (const FSceneObjectRecord& Record : Data.Objects)
    {
        std::string RecordPath;
        if (TryBuildObjectPath(Records, Record.Id, RecordPath)
            && RecordPath == Path)
        {
            return &Record;
        }
    }
    return nullptr;
}

bool IsNameAvailable(
    const std::vector<FSceneObjectRecord>& Records,
    FSceneObjectId OuterId,
    std::string_view Name)
{
    return std::none_of(
        Records.begin(),
        Records.end(),
        [OuterId, Name](const FSceneObjectRecord& Record)
        {
            return Record.OuterId == OuterId && Record.ObjectName == Name;
        });
}

std::string MakeUniqueName(
    const std::vector<FSceneObjectRecord>& Records,
    FSceneObjectId OuterId,
    std::string_view BaseName)
{
    if (IsNameAvailable(Records, OuterId, BaseName))
    {
        return std::string(BaseName);
    }

    std::string Candidate = std::string(BaseName) + "_Copy";
    if (IsNameAvailable(Records, OuterId, Candidate))
    {
        return Candidate;
    }

    for (uint64 Suffix = 2; Suffix != 0; ++Suffix)
    {
        Candidate =
            std::string(BaseName) + "_Copy_" + std::to_string(Suffix);
        if (IsNameAvailable(Records, OuterId, Candidate))
        {
            return Candidate;
        }
    }
    return {};
}

FSceneObjectId RemapId(
    FSceneObjectId Id,
    const std::unordered_map<uint64, FSceneObjectId>& IdMap)
{
    if (!Id.IsValid())
    {
        return {};
    }
    const auto Found = IdMap.find(Id.Value);
    return Found != IdMap.end() ? Found->second : FSceneObjectId {};
}
}

std::string_view ToString(EEditorClipboardError Error)
{
    switch (Error)
    {
    case EEditorClipboardError::None:
        return "None";
    case EEditorClipboardError::InvalidArgument:
        return "InvalidArgument";
    case EEditorClipboardError::ObjectNotFound:
        return "ObjectNotFound";
    case EEditorClipboardError::UnsupportedObject:
        return "UnsupportedObject";
    case EEditorClipboardError::EmptyClipboard:
        return "EmptyClipboard";
    case EEditorClipboardError::InvalidDestination:
        return "InvalidDestination";
    case EEditorClipboardError::IdOverflow:
        return "IdOverflow";
    case EEditorClipboardError::InvalidSceneData:
        return "InvalidSceneData";
    }
    return "Unknown";
}

bool FEditorSceneClipboard::Copy(
    const PWorld& World,
    std::string_view ObjectPath,
    EEditorClipboardError* OutError)
{
    ReportError(OutError, EEditorClipboardError::None);
    if (ObjectPath.empty())
    {
        ReportError(OutError, EEditorClipboardError::InvalidArgument);
        return false;
    }

    FWorldAssetData Data;
    if (!CaptureWorld(World, Data))
    {
        ReportError(OutError, EEditorClipboardError::InvalidSceneData);
        return false;
    }

    const FRecordMap Records = BuildRecordMap(Data);
    const FSceneObjectRecord* Selected =
        FindRecordByPath(Data, Records, ObjectPath);
    if (Selected == nullptr)
    {
        ReportError(OutError, EEditorClipboardError::ObjectNotFound);
        return false;
    }

    EEditorClipboardContentType NewContentType =
        EEditorClipboardContentType::None;
    std::unordered_set<uint64> IncludedIds;
    if (IsChildOf(*Selected, PActor::StaticClass()))
    {
        NewContentType = EEditorClipboardContentType::Actor;
        IncludedIds.insert(Selected->Id.Value);
        for (const FSceneObjectRecord& Record : Data.Objects)
        {
            if (Record.OuterId == Selected->Id)
            {
                IncludedIds.insert(Record.Id.Value);
            }
        }
    }
    else if (IsChildOf(*Selected, PSceneComponent::StaticClass()))
    {
        NewContentType = EEditorClipboardContentType::SceneComponent;
        IncludedIds.insert(Selected->Id.Value);

        bool bAddedChild = true;
        while (bAddedChild)
        {
            bAddedChild = false;
            for (const FSceneRelationRecord& Relation : Data.Relations)
            {
                if (Relation.AttachParentId.IsValid()
                    && IncludedIds.contains(Relation.AttachParentId.Value)
                    && IncludedIds.insert(Relation.ObjectId.Value).second)
                {
                    bAddedChild = true;
                }
            }
        }
    }
    else
    {
        ReportError(OutError, EEditorClipboardError::UnsupportedObject);
        return false;
    }

    std::vector<FSceneObjectRecord> CopiedObjects;
    for (const FSceneObjectRecord& Record : Data.Objects)
    {
        if (IncludedIds.contains(Record.Id.Value))
        {
            CopiedObjects.push_back(Record);
        }
    }

    std::vector<FSceneRelationRecord> CopiedRelations;
    for (const FSceneRelationRecord& Relation : Data.Relations)
    {
        if (!IncludedIds.contains(Relation.ObjectId.Value))
        {
            continue;
        }

        const bool bRootRelation =
            Relation.RootComponentId.IsValid()
            && IncludedIds.contains(Relation.RootComponentId.Value);
        const bool bAttachmentRelation =
            Relation.AttachParentId.IsValid()
            && IncludedIds.contains(Relation.AttachParentId.Value);
        if (bRootRelation || bAttachmentRelation)
        {
            CopiedRelations.push_back(Relation);
        }
    }

    ContentType = NewContentType;
    RootObjectId = Selected->Id;
    SourceObjectPath = ObjectPath;
    Objects = std::move(CopiedObjects);
    Relations = std::move(CopiedRelations);
    return true;
}

bool FEditorSceneClipboard::BuildPaste(
    const PWorld& World,
    std::string_view DestinationPath,
    FWorldAssetData& OutWorldData,
    std::string& OutPastedObjectPath,
    EEditorClipboardError* OutError) const
{
    ReportError(OutError, EEditorClipboardError::None);
    OutPastedObjectPath.clear();
    if (!HasContent())
    {
        ReportError(OutError, EEditorClipboardError::EmptyClipboard);
        return false;
    }

    FWorldAssetData Data;
    if (!CaptureWorld(World, Data))
    {
        ReportError(OutError, EEditorClipboardError::InvalidSceneData);
        return false;
    }

    const FRecordMap Records = BuildRecordMap(Data);
    const FSceneObjectRecord* Destination =
        DestinationPath.empty()
        ? nullptr
        : FindRecordByPath(Data, Records, DestinationPath);

    FSceneObjectId TargetLevelId;
    FSceneObjectId TargetActorId;
    FSceneObjectId AttachParentId;
    if (ContentType == EEditorClipboardContentType::Actor)
    {
        TargetLevelId = Data.CurrentLevelId;
    }
    else
    {
        if (Destination == nullptr)
        {
            ReportError(OutError, EEditorClipboardError::InvalidDestination);
            return false;
        }
        if (IsChildOf(*Destination, PActor::StaticClass()))
        {
            TargetActorId = Destination->Id;
            const auto RootRelation = std::find_if(
                Data.Relations.begin(),
                Data.Relations.end(),
                [TargetActorId](const FSceneRelationRecord& Relation)
                {
                    return Relation.ObjectId == TargetActorId
                        && Relation.RootComponentId.IsValid();
                });
            if (RootRelation != Data.Relations.end())
            {
                AttachParentId = RootRelation->RootComponentId;
            }
        }
        else if (IsChildOf(*Destination, PSceneComponent::StaticClass()))
        {
            TargetActorId = Destination->OuterId;
            AttachParentId = Destination->Id;
        }
        else
        {
            ReportError(OutError, EEditorClipboardError::InvalidDestination);
            return false;
        }
    }

    uint64 MaximumId = 0;
    for (const FSceneObjectRecord& Record : Data.Objects)
    {
        MaximumId = std::max(MaximumId, Record.Id.Value);
    }
    if (Objects.size()
        > std::numeric_limits<uint64>::max() - MaximumId)
    {
        ReportError(OutError, EEditorClipboardError::IdOverflow);
        return false;
    }

    std::unordered_map<uint64, FSceneObjectId> IdMap;
    IdMap.reserve(Objects.size());
    for (const FSceneObjectRecord& Record : Objects)
    {
        IdMap.emplace(Record.Id.Value, FSceneObjectId { ++MaximumId });
    }

    for (const FSceneObjectRecord& SourceRecord : Objects)
    {
        FSceneObjectRecord Record = SourceRecord;
        Record.Id = RemapId(SourceRecord.Id, IdMap);
        if (!Record.Id.IsValid())
        {
            ReportError(OutError, EEditorClipboardError::InvalidSceneData);
            return false;
        }

        if (ContentType == EEditorClipboardContentType::Actor)
        {
            Record.OuterId =
                SourceRecord.Id == RootObjectId
                ? TargetLevelId
                : RemapId(SourceRecord.OuterId, IdMap);
        }
        else
        {
            Record.OuterId = TargetActorId;
        }
        if (!Record.OuterId.IsValid())
        {
            ReportError(OutError, EEditorClipboardError::InvalidSceneData);
            return false;
        }

        Record.ObjectName =
            MakeUniqueName(Data.Objects, Record.OuterId, Record.ObjectName);
        if (Record.ObjectName.empty())
        {
            ReportError(OutError, EEditorClipboardError::InvalidSceneData);
            return false;
        }
        Data.Objects.push_back(std::move(Record));
    }

    for (const FSceneRelationRecord& SourceRelation : Relations)
    {
        FSceneRelationRecord Relation;
        Relation.ObjectId = RemapId(SourceRelation.ObjectId, IdMap);
        Relation.RootComponentId =
            RemapId(SourceRelation.RootComponentId, IdMap);
        Relation.AttachParentId =
            RemapId(SourceRelation.AttachParentId, IdMap);
        if (!Relation.ObjectId.IsValid()
            || (SourceRelation.RootComponentId.IsValid()
                && !Relation.RootComponentId.IsValid())
            || (SourceRelation.AttachParentId.IsValid()
                && !Relation.AttachParentId.IsValid()))
        {
            ReportError(OutError, EEditorClipboardError::InvalidSceneData);
            return false;
        }
        Data.Relations.push_back(Relation);
    }

    const FSceneObjectId NewRootId = RemapId(RootObjectId, IdMap);
    if (ContentType == EEditorClipboardContentType::SceneComponent)
    {
        if (AttachParentId.IsValid())
        {
            Data.Relations.push_back(
                FSceneRelationRecord { NewRootId, {}, AttachParentId });
        }
        else
        {
            Data.Relations.push_back(
                FSceneRelationRecord { TargetActorId, NewRootId, {} });
        }
    }

    if (!ValidateWorldAssetData(Data))
    {
        ReportError(OutError, EEditorClipboardError::InvalidSceneData);
        return false;
    }

    const FRecordMap PastedRecords = BuildRecordMap(Data);
    if (!TryBuildObjectPath(
            PastedRecords,
            NewRootId,
            OutPastedObjectPath))
    {
        ReportError(OutError, EEditorClipboardError::InvalidSceneData);
        return false;
    }

    OutWorldData = std::move(Data);
    return true;
}

void FEditorSceneClipboard::Clear()
{
    ContentType = EEditorClipboardContentType::None;
    RootObjectId = {};
    SourceObjectPath.clear();
    Objects.clear();
    Relations.clear();
}

bool FEditorSceneClipboard::HasContent() const
{
    return ContentType != EEditorClipboardContentType::None
        && RootObjectId.IsValid()
        && !Objects.empty();
}

EEditorClipboardContentType FEditorSceneClipboard::GetContentType() const
{
    return ContentType;
}

std::string_view FEditorSceneClipboard::GetSourceObjectPath() const
{
    return SourceObjectPath;
}
}
