#include "Pico/Editor/AssetDependencyService.h"

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/CharacterProfile.h"
#include "Pico/Asset/Material.h"
#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Property.h"

#include <algorithm>

namespace Pico
{
namespace
{
void GatherProperties(
    const PClass* Class,
    std::vector<const PProperty*>& OutProperties)
{
    if (Class == nullptr) return;
    GatherProperties(Class->GetSuperClass(), OutProperties);
    for (const PProperty& Property : Class->GetProperties())
    {
        OutProperties.push_back(&Property);
    }
}

void GatherObjectReferences(
    const PObject* Object,
    std::vector<FObjectAssetReference>& OutReferences)
{
    if (Object == nullptr || Object->GetClass() == nullptr) return;
    std::vector<const PProperty*> Properties;
    GatherProperties(Object->GetClass(), Properties);
    for (const PProperty* Property : Properties)
    {
        if (Property == nullptr
            || Property->GetType() != EPropertyType::AssetPath
            || !Property->HasAnyFlags(EPropertyFlags::Serializable)
            || Property->HasAnyFlags(EPropertyFlags::Transient))
        {
            continue;
        }
        FAssetPath AssetPath;
        if (Property->GetValue(Object, AssetPath) && AssetPath.IsValid())
        {
            OutReferences.push_back(
                {Object->GetHandle(), Object->GetPathName(), Property->GetName(), AssetPath});
        }
    }
}

void GatherWorldObjects(const PWorld* World, std::vector<const PObject*>& OutObjects)
{
    if (World == nullptr) return;
    OutObjects.push_back(World);
    for (const PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        OutObjects.push_back(Level);
        for (const PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr) continue;
            OutObjects.push_back(Actor);
            for (const PActorComponent* Component : Actor->GetComponents())
            {
                if (Component != nullptr) OutObjects.push_back(Component);
            }
        }
    }
}
}

std::vector<FObjectAssetReference> FAssetDependencyService::GatherWorldReferences(
    const PWorld* World)
{
    std::vector<FObjectAssetReference> References;
    std::vector<const PObject*> Objects;
    GatherWorldObjects(World, Objects);
    for (const PObject* Object : Objects)
    {
        GatherObjectReferences(Object, References);
    }
    return References;
}

std::vector<FObjectAssetReference> FAssetDependencyService::FindWorldReferencers(
    const PWorld* World,
    const FAssetPath& AssetPath)
{
    std::vector<FObjectAssetReference> References = GatherWorldReferences(World);
    std::erase_if(
        References,
        [&AssetPath](const FObjectAssetReference& Reference)
        {
            return Reference.AssetPath != AssetPath;
        });
    return References;
}

std::size_t FAssetDependencyService::ReplaceWorldReferences(
    PWorld* World,
    const FAssetPath& OldAssetPath,
    const FAssetPath& NewAssetPath)
{
    std::size_t UpdatedCount = 0;
    for (const FObjectAssetReference& Reference :
        FindWorldReferencers(World, OldAssetPath))
    {
        PObject* Object = ResolveObject(Reference.ObjectHandle);
        const PProperty* Property = Object != nullptr && Object->GetClass() != nullptr
            ? Object->GetClass()->FindProperty(Reference.PropertyName) : nullptr;
        if (Property != nullptr && Property->SetValue(Object, NewAssetPath))
        {
            ++UpdatedCount;
        }
    }
    return UpdatedCount;
}

std::vector<FAssetPath> FAssetDependencyService::GetAssetDependencies(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry)
{
    std::vector<FAssetPath> Dependencies;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr) return Dependencies;
    const auto Add = [&Dependencies](const FAssetPath& Path)
    {
        if (Path.IsValid()) Dependencies.push_back(Path);
    };
    if (Record->Type == EAssetType::Material)
    {
        FMaterialData Material;
        if (LoadMaterialFromFile(Record->FilePath, Material)
            && Material.BaseColorTexture.IsValid())
        {
            Dependencies.push_back(Material.BaseColorTexture);
        }
    }
    else if (Record->Type == EAssetType::SkeletalMesh)
    {
        FSkeletalMeshData Mesh;
        if (LoadSkeletalMeshFromFile(Record->FilePath, Mesh))
        {
            Add(Mesh.SkeletonAsset);
            for (const FAssetPath& Material : Mesh.DefaultMaterials) Add(Material);
        }
    }
    else if (Record->Type == EAssetType::AnimationClip)
    {
        FAnimationClipData Clip;
        if (LoadAnimationClipFromFile(Record->FilePath, Clip)) Add(Clip.SkeletonAsset);
    }
    else if (Record->Type == EAssetType::AnimationSet)
    {
        FAnimationSetData Set;
        if (LoadAnimationSetFromFile(Record->FilePath, Set))
        {
            Add(Set.SkeletonAsset);
            Add(Set.IdleAnimation);
            Add(Set.WalkAnimation);
            Add(Set.JumpAnimation);
        }
    }
    else if (Record->Type == EAssetType::AnimationMontage)
    {
        FAnimationMontageData Montage;
        if (LoadAnimationMontageFromFile(Record->FilePath, Montage))
        {
            Add(Montage.SkeletonAsset);
            for (const FAnimationMontageSegment& Segment : Montage.Segments)
                Add(Segment.AnimationAsset);
        }
    }
    else if (Record->Type == EAssetType::CharacterProfile)
    {
        FCharacterProfileData Profile;
        if (LoadCharacterProfileFromFile(Record->FilePath, Profile))
        {
            Add(Profile.SkeletalMesh);
            Add(Profile.AnimationSet);
            Add(Profile.DefaultMontage);
            for (const FAssetPath& Material : Profile.MaterialOverrides) Add(Material);
        }
    }
    return Dependencies;
}

std::vector<FAssetPath> FAssetDependencyService::FindAssetReferencers(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry)
{
    std::vector<FAssetPath> Referencers;
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        const std::vector<FAssetPath> Dependencies =
            GetAssetDependencies(Record.AssetPath, Registry);
        if (std::find(Dependencies.begin(), Dependencies.end(), AssetPath)
            != Dependencies.end())
        {
            Referencers.push_back(Record.AssetPath);
        }
    }
    return Referencers;
}
}
