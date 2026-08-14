#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Name.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
class FAssetRegistry;
class PActor;
class PClass;

struct FActorBlueprintObjectDefaults
{
    FName ObjectName;
    std::vector<std::pair<FName, std::string>> Properties;
};

struct FActorBlueprintData
{
    int32 Version = 1;
    FName ParentClassName;
    FName GeneratedClassName;
    FActorBlueprintObjectDefaults ActorDefaults;
    std::vector<FActorBlueprintObjectDefaults> ComponentDefaults;
};

enum class EActorBlueprintError
{
    None,
    InvalidArgument,
    FileReadFailed,
    FileWriteFailed,
    InvalidFormat,
    ParentClassNotFound,
    GeneratedClassConflict,
    ClassRegistrationFailed,
    PropertyOverrideFailed
};

bool LoadActorBlueprintFromFile(
    const std::filesystem::path& FilePath,
    FActorBlueprintData& OutData,
    EActorBlueprintError* OutError = nullptr);
bool SaveActorBlueprintToFile(
    const std::filesystem::path& FilePath,
    const FActorBlueprintData& Data,
    EActorBlueprintError* OutError = nullptr);
bool CreateActorBlueprintAsset(
    const std::filesystem::path& FilePath,
    const FAssetPath& AssetPath,
    const PClass* ParentClass,
    EActorBlueprintError* OutError = nullptr);
bool SaveActorBlueprintDefaults(
    const std::filesystem::path& FilePath,
    const FAssetPath& AssetPath,
    const PActor* SourceActor,
    EActorBlueprintError* OutError = nullptr);

bool CompileActorBlueprint(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    EActorBlueprintError* OutError = nullptr);
bool CompileProjectActorBlueprints(
    const FAssetRegistry& Registry,
    EActorBlueprintError* OutError = nullptr);
const PClass* FindActorBlueprintGeneratedClass(const FAssetPath& AssetPath);
FAssetPath FindActorBlueprintAsset(const PClass* GeneratedClass);

std::string_view ToString(EActorBlueprintError Error);
}
