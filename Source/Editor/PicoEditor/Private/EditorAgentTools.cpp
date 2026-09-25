#include "Pico/Editor/EditorAgentTools.h"

#include "Pico/Agent/AgentGameAssembly.h"
#include "Pico/Asset/CharacterProfile.h"
#include "Pico/Asset/Material.h"
#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Asset/StaticMesh.h"
#include "Pico/Asset/Texture.h"
#include "Pico/Asset/ThirdPersonControlProfile.h"
#include "Pico/Editor/AssetDependencyService.h"
#include "Pico/Editor/AssetSemanticMetadataService.h"
#include "Pico/Editor/EditorAssetService.h"
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
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/GameplayAbilities/GameplayAbility.h"
#include "Pico/GameplayAbilities/GameplayEffect.h"
#include "Pico/Graph/GraphAsset.h"
#include "Pico/Graph/GraphCompiler.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/Property.h"
#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

std::string StableRevision(std::string_view Bytes)
{
    std::uint64_t Hash = 14695981039346656037ull;
    for (const unsigned char Byte : Bytes)
    {
        Hash ^= Byte;
        Hash *= 1099511628211ull;
    }
    std::ostringstream Stream;
    Stream << std::hex << std::setfill('0') << std::setw(16) << Hash;
    return Stream.str();
}

std::string FileRevision(const std::filesystem::path& File)
{
    std::ifstream Stream(File, std::ios::binary);
    if (!Stream) return {};
    std::ostringstream Bytes;
    Bytes << Stream.rdbuf();
    return Stream ? StableRevision(Bytes.str()) : std::string {};
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

PGameplayAbilitySystemComponent* FindAbilitySystem(PObject* Object)
{
    if (Object == nullptr) return nullptr;
    if (Object->IsA(PGameplayAbilitySystemComponent::StaticClass()))
        return static_cast<PGameplayAbilitySystemComponent*>(Object);
    PActor* Actor = Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    if (Actor == nullptr && Object->IsA(PActorComponent::StaticClass()))
        Actor = static_cast<PActorComponent*>(Object)->GetOwner();
    if (Actor == nullptr) return nullptr;
    for (PActorComponent* Component : Actor->GetComponents())
        if (Component != nullptr
            && Component->IsA(PGameplayAbilitySystemComponent::StaticClass()))
            return static_cast<PGameplayAbilitySystemComponent*>(Component);
    return nullptr;
}

std::string GetAbilitySemanticName(const PClass* AbilityClass)
{
    const auto* Ability = AbilityClass != nullptr
        ? static_cast<const PGameplayAbility*>(AbilityClass->GetDefaultObject())
        : nullptr;
    if (Ability != nullptr)
    {
        constexpr std::string_view Prefix = "Ability.Projectile.";
        for (const FGameplayTag& Tag : Ability->GetAbilityTags().GetTags())
        {
            const std::string_view Name = Tag.ToString();
            if (Name.starts_with(Prefix))
                return std::string(Name.substr(Prefix.size())) + "Shot";
        }
    }
    return AbilityClass != nullptr
        ? AbilityClass->GetName().ToString() : std::string();
}

const PClass* FindGameplayAbilityClassByTag(std::string_view TagName)
{
    const FGameplayTag Tag = FGameplayTagsManager::Get().RequestGameplayTag(TagName);
    if (!Tag.IsValid()) return nullptr;
    for (const PClass* Class : FClassRegistry::GetClasses())
    {
        if (Class == nullptr || !Class->IsChildOf(PGameplayAbility::StaticClass()))
            continue;
        const auto* Ability = static_cast<const PGameplayAbility*>(
            Class->GetDefaultObject());
        if (Ability != nullptr && Ability->GetAbilityTags().HasTagExact(Tag))
            return Class;
    }
    return nullptr;
}

FJson DescribeMiniGasProfile(PObject* Owner)
{
    if (Owner == nullptr || Owner->GetClass() == nullptr) return nullptr;
    FJson Profile = FJson::object();
    const std::vector<std::string> BooleanProperties = {
        "bGravityShotEnabled", "bBurnShotEnabled", "bFreezeShotEnabled"};
    const std::vector<std::string> NumberProperties = {
        "InitialHealth", "InitialMana",
        "GravityManaCost", "GravityCooldownSeconds", "GravityRange",
        "GravityProjectileSpeed", "GravityEffectDuration", "GravityLaunchVelocity",
        "BurnManaCost", "BurnCooldownSeconds", "BurnRange", "BurnProjectileSpeed",
        "BurnEffectDuration", "BurnTickInterval", "BurnDamagePerTick",
        "FreezeManaCost", "FreezeCooldownSeconds", "FreezeRange",
        "FreezeProjectileSpeed", "FreezeEffectDuration"};
    const std::vector<std::string> ColorProperties = {
        "GravityProjectileColor", "BurnProjectileColor", "FreezeProjectileColor"};
    for (const std::string& Name : BooleanProperties)
    {
        const PProperty* Property = Owner->GetClass()->FindProperty(FName(Name));
        bool Value = false;
        if (Property != nullptr && Property->GetValue(Owner, Value)) Profile[Name] = Value;
    }
    for (const std::string& Name : NumberProperties)
    {
        const PProperty* Property = Owner->GetClass()->FindProperty(FName(Name));
        float Value = 0.0f;
        if (Property != nullptr && Property->GetValue(Owner, Value)) Profile[Name] = Value;
    }
    for (const std::string& Name : ColorProperties)
    {
        const PProperty* Property = Owner->GetClass()->FindProperty(FName(Name));
        FVector3 Value;
        if (Property != nullptr && Property->GetValue(Owner, Value))
            Profile[Name] = {{"x", Value.X}, {"y", Value.Y}, {"z", Value.Z}};
    }
    return Profile.empty() ? FJson(nullptr) : Profile;
}

FJson DescribeAbilitySystem(PGameplayAbilitySystemComponent* AbilitySystem)
{
    if (AbilitySystem == nullptr) return FJson::object();
    FJson Abilities = FJson::array();
    for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
    {
        const auto* Ability = Spec.AbilityClass != nullptr
            ? static_cast<const PGameplayAbility*>(Spec.AbilityClass->GetDefaultObject())
            : nullptr;
        Abilities.push_back({{"handle", Spec.Handle.Value},
            {"semantic_name", GetAbilitySemanticName(Spec.AbilityClass)},
            {"internal_class", Spec.AbilityClass
                ? Spec.AbilityClass->GetName().ToString() : ""},
            {"cost", Ability ? Spec.ResolveCost(Ability->GetDefaultCost()) : 0.0f},
            {"cooldown", Ability
                ? Spec.ResolveCooldown(Ability->GetDefaultCooldown()) : 0.0f},
            {"level", Spec.Level}, {"input_id", Spec.InputId},
            {"input_key", Spec.InputId >= 0 && Spec.InputId < 3
                ? std::to_string(Spec.InputId + 1) : ""},
            {"active", Spec.IsActive()}});
    }
    FJson Effects = FJson::array();
    for (const FActiveGameplayEffect& Effect : AbilitySystem->GetActiveGameplayEffects())
        Effects.push_back({{"handle", Effect.Handle.Value},
            {"class", Effect.Spec.EffectClass
                ? Effect.Spec.EffectClass->GetName().ToString() : ""},
            {"stacking_key", Effect.Spec.StackingKey},
            {"remaining", Effect.RemainingDuration},
            {"stacks", Effect.StackCount}});
    FJson Attributes = FJson::object();
    if (const PAttributeSet* Set = AbilitySystem->GetAttributeSet())
        for (const EGameplayAttribute Attribute : {EGameplayAttribute::Health,
            EGameplayAttribute::MaxHealth, EGameplayAttribute::Mana,
            EGameplayAttribute::MoveSpeed})
            Attributes[std::string(ToString(Attribute))] =
                Set->GetCurrentValue(Attribute);
    return {{"schema_revision", 2},
        {"naming_note", "internal_class names are compatibility identifiers; semantic_name is the current gameplay ability"},
        {"object_path", AbilitySystem->GetPathName()},
        {"owner", AbilitySystem->GetAbilityOwnerActor()
            ? AbilitySystem->GetAbilityOwnerActor()->GetPathName() : ""},
        {"avatar", AbilitySystem->GetAbilityAvatarActor()
            ? AbilitySystem->GetAbilityAvatarActor()->GetPathName() : ""},
        {"attributes", std::move(Attributes)},
        {"mini_gas_profile", DescribeMiniGasProfile(
            AbilitySystem->GetAbilityOwnerActor())},
        {"owned_tags", AbilitySystem->GetOwnedGameplayTags().ExportText()},
        {"abilities", std::move(Abilities)}, {"active_effects", std::move(Effects)}};
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

FJson BoundsToJson(const FStaticMeshBounds& Bounds)
{
    return {{"min", VectorToJson(Bounds.Min)}, {"max", VectorToJson(Bounds.Max)}};
}

std::string AssetPathString(const FAssetPath& Path)
{
    return std::string(Path.ToString());
}

FJson BuildTypedAssetSummary(const FAssetRecord& Asset)
{
    FJson Summary{{"descriptor_schema_version", 1},
        {"asset_type", std::string(ToString(Asset.Type))},
        {"inspection_status", "loaded"}};
    const auto Fail = [&Summary](std::string_view Error)
    {
        Summary["inspection_status"] = "unreadable";
        Summary["inspection_error"] = Error;
    };

    switch (Asset.Type)
    {
    case EAssetType::World:
    {
        FWorldAssetData World;
        EWorldSerializationError Error = EWorldSerializationError::None;
        if (!LoadWorldAssetDataFromFile(Asset.FilePath, World, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        std::size_t PropertyCount = 0;
        for (const FSceneObjectRecord& Object : World.Objects)
            PropertyCount += Object.Properties.size();
        Summary["object_count"] = World.Objects.size();
        Summary["relation_count"] = World.Relations.size();
        Summary["serialized_property_count"] = PropertyCount;
        break;
    }
    case EAssetType::StaticMesh:
    {
        FStaticMeshData Mesh;
        EStaticMeshError Error = EStaticMeshError::None;
        if (!LoadStaticMeshFromFile(Asset.FilePath, Mesh, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        FJson Slots = FJson::array();
        for (const FStaticMeshSection& Section : Mesh.Sections)
            Slots.push_back(Section.MaterialSlotName);
        Summary["vertex_count"] = Mesh.Vertices.size();
        Summary["index_count"] = Mesh.Indices.size();
        Summary["triangle_count"] = Mesh.Indices.size() / 3;
        Summary["section_count"] = Mesh.Sections.size();
        Summary["material_slots"] = std::move(Slots);
        Summary["uv_channel_count"] = Mesh.Vertices.empty() ? 0 : 1;
        Summary["has_vertex_normals"] = !Mesh.Vertices.empty();
        Summary["bounds"] = BoundsToJson(Mesh.Bounds);
        break;
    }
    case EAssetType::Texture:
    {
        FTextureData Texture;
        ETextureError Error = ETextureError::None;
        if (!LoadTextureFromFile(Asset.FilePath, Texture, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        bool bUsesTransparency = false;
        for (std::size_t Index = 3; Index < Texture.Pixels.size(); Index += 4)
        {
            if (Texture.Pixels[Index] != 255)
            {
                bUsesTransparency = true;
                break;
            }
        }
        Summary["width"] = Texture.Width;
        Summary["height"] = Texture.Height;
        Summary["pixel_format"] = "RGBA8";
        Summary["channel_count"] = 4;
        Summary["has_alpha_channel"] = true;
        Summary["uses_transparency"] = bUsesTransparency;
        Summary["pixel_data_bytes"] = Texture.Pixels.size();
        break;
    }
    case EAssetType::Material:
    {
        FMaterialData Material;
        EMaterialError Error = EMaterialError::None;
        if (!LoadMaterialFromFile(Asset.FilePath, Material, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        const bool bUsesTexture = !Material.BaseColorTexture.IsEmpty();
        Summary["shading_model"] = "PicoLitPBR";
        Summary["base_color_source"] = bUsesTexture ? "texture" : "constant";
        Summary["base_color"] = VectorToJson(Material.BaseColor);
        Summary["base_color_texture"] = bUsesTexture
            ? FJson(AssetPathString(Material.BaseColorTexture)) : FJson(nullptr);
        Summary["metallic"] = Material.Metallic;
        Summary["roughness"] = Material.Roughness;
        Summary["has_normal_texture"] = false;
        Summary["has_height_texture"] = false;
        Summary["has_vertex_displacement"] = false;
        Summary["semantic_surface"] = "unknown";
        break;
    }
    case EAssetType::Skeleton:
    {
        FSkeletonData Skeleton;
        ESkeletalAssetError Error = ESkeletalAssetError::None;
        if (!LoadSkeletonFromFile(Asset.FilePath, Skeleton, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        const std::size_t RootCount = static_cast<std::size_t>(std::count_if(
            Skeleton.Bones.begin(), Skeleton.Bones.end(),
            [](const FSkeletonBone& Bone) { return Bone.ParentIndex < 0; }));
        Summary["bone_count"] = Skeleton.Bones.size();
        Summary["root_bone_count"] = RootCount;
        break;
    }
    case EAssetType::SkeletalMesh:
    {
        FSkeletalMeshData Mesh;
        ESkeletalAssetError Error = ESkeletalAssetError::None;
        if (!LoadSkeletalMeshFromFile(Asset.FilePath, Mesh, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        FJson Materials = FJson::array();
        for (const FAssetPath& Material : Mesh.DefaultMaterials)
            Materials.push_back(AssetPathString(Material));
        std::size_t MaxBoneInfluences = 0;
        for (const FSkeletalMeshVertex& Vertex : Mesh.Vertices)
        {
            const std::size_t Used = static_cast<std::size_t>(std::count_if(
                Vertex.BoneWeights.begin(), Vertex.BoneWeights.end(),
                [](float Weight) { return Weight > 0.0f; }));
            MaxBoneInfluences = std::max(MaxBoneInfluences, Used);
        }
        Summary["skeleton"] = AssetPathString(Mesh.SkeletonAsset);
        Summary["vertex_count"] = Mesh.Vertices.size();
        Summary["index_count"] = Mesh.Indices.size();
        Summary["triangle_count"] = Mesh.Indices.size() / 3;
        Summary["section_count"] = Mesh.Sections.size();
        Summary["default_materials"] = std::move(Materials);
        Summary["uv_channel_count"] = Mesh.Vertices.empty() ? 0 : 1;
        Summary["max_bone_influences"] = MaxBoneInfluences;
        Summary["bounds"] = BoundsToJson(Mesh.Bounds);
        break;
    }
    case EAssetType::AnimationClip:
    {
        FAnimationClipData Clip;
        ESkeletalAssetError Error = ESkeletalAssetError::None;
        if (!LoadAnimationClipFromFile(Asset.FilePath, Clip, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        std::size_t KeyCount = 0;
        for (const FBoneAnimationTrack& Track : Clip.Tracks)
            KeyCount += Track.TranslationKeys.size() + Track.RotationKeys.size()
                + Track.ScaleKeys.size();
        Summary["skeleton"] = AssetPathString(Clip.SkeletonAsset);
        Summary["clip_name"] = Clip.Name;
        Summary["duration_seconds"] = Clip.Duration;
        Summary["looping"] = Clip.bLooping;
        Summary["track_count"] = Clip.Tracks.size();
        Summary["key_count"] = KeyCount;
        Summary["notify_count"] = Clip.Notifies.size();
        Summary["has_root_motion_track"] = Clip.RootBoneIndex >= 0
            && std::any_of(Clip.Tracks.begin(), Clip.Tracks.end(),
                [&Clip](const FBoneAnimationTrack& Track)
                {
                    return Track.BoneIndex
                        == static_cast<uint32>(Clip.RootBoneIndex);
                });
        break;
    }
    case EAssetType::AnimationSet:
    {
        FAnimationSetData Set;
        ESkeletalAssetError Error = ESkeletalAssetError::None;
        if (!LoadAnimationSetFromFile(Asset.FilePath, Set, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        Summary["skeleton"] = AssetPathString(Set.SkeletonAsset);
        Summary["idle_animation"] = AssetPathString(Set.IdleAnimation);
        Summary["walk_animation"] = AssetPathString(Set.WalkAnimation);
        Summary["jump_animation"] = AssetPathString(Set.JumpAnimation);
        break;
    }
    case EAssetType::AnimationMontage:
    {
        FAnimationMontageData Montage;
        ESkeletalAssetError Error = ESkeletalAssetError::None;
        if (!LoadAnimationMontageFromFile(Asset.FilePath, Montage, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        Summary["skeleton"] = AssetPathString(Montage.SkeletonAsset);
        Summary["slot_name"] = Montage.SlotName;
        Summary["blend_in_seconds"] = Montage.BlendInTime;
        Summary["blend_out_seconds"] = Montage.BlendOutTime;
        Summary["segment_count"] = Montage.Segments.size();
        Summary["section_count"] = Montage.Sections.size();
        Summary["notify_count"] = Montage.Notifies.size();
        break;
    }
    case EAssetType::CharacterProfile:
    {
        FCharacterProfileData Profile;
        ECharacterProfileError Error = ECharacterProfileError::None;
        if (!LoadCharacterProfileFromFile(Asset.FilePath, Profile, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        FJson Overrides = FJson::array();
        for (const FAssetPath& Material : Profile.MaterialOverrides)
            if (!Material.IsEmpty()) Overrides.push_back(AssetPathString(Material));
        Summary["skeletal_mesh"] = AssetPathString(Profile.SkeletalMesh);
        Summary["animation_set"] = AssetPathString(Profile.AnimationSet);
        Summary["default_montage"] = Profile.DefaultMontage.IsEmpty()
            ? FJson(nullptr) : FJson(AssetPathString(Profile.DefaultMontage));
        Summary["material_overrides"] = std::move(Overrides);
        Summary["mesh_transform"] = {{"location", VectorToJson(Profile.MeshTransform.Translation)},
            {"rotation", RotatorToJson(Profile.MeshTransform.Rotation.Rotator())},
            {"scale", VectorToJson(Profile.MeshTransform.Scale)}};
        break;
    }
    case EAssetType::ThirdPersonControlProfile:
    {
        FThirdPersonControlProfileData Profile;
        EThirdPersonControlProfileError Error = EThirdPersonControlProfileError::None;
        if (!LoadThirdPersonControlProfileFromFile(Asset.FilePath, Profile, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        Summary["profile_version"] = Profile.Version;
        Summary["movement_reference"] = ToString(Profile.MovementReference);
        Summary["max_walk_speed"] = Profile.MaxWalkSpeed;
        Summary["rotation_rate"] = Profile.RotationRate;
        Summary["orient_rotation_to_movement"] = Profile.bOrientRotationToMovement;
        Summary["camera_arm_length"] = Profile.DefaultCameraArmLength;
        Summary["camera_uses_control_rotation"] = Profile.bCameraUsesControlRotation;
        Summary["camera_pitch_range"] = {Profile.MinimumCameraPitch, Profile.MaximumCameraPitch};
        break;
    }
    case EAssetType::ActorBlueprint:
    {
        FActorBlueprintData Blueprint;
        EActorBlueprintError Error = EActorBlueprintError::None;
        if (!LoadActorBlueprintFromFile(Asset.FilePath, Blueprint, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        std::size_t PropertyOverrideCount = Blueprint.ActorDefaults.Properties.size();
        FJson Components = FJson::array();
        for (const FActorBlueprintObjectDefaults& Component : Blueprint.ComponentDefaults)
        {
            PropertyOverrideCount += Component.Properties.size();
            Components.push_back({{"name", Component.ObjectName.ToString()},
                {"class", Component.ComponentClassName.ToString()},
                {"property_override_count", Component.Properties.size()}});
        }
        Summary["blueprint_version"] = Blueprint.Version;
        Summary["parent_class"] = Blueprint.ParentClassName.ToString();
        Summary["generated_class"] = Blueprint.GeneratedClassName.ToString();
        Summary["component_count"] = Blueprint.ComponentDefaults.size();
        Summary["components"] = std::move(Components);
        Summary["property_override_count"] = PropertyOverrideCount;
        break;
    }
    case EAssetType::PicoGraph:
    {
        FPicoGraphAsset Graph;
        EGraphAssetError Error = EGraphAssetError::None;
        if (!LoadGraphAssetFromFile(Asset.FilePath, Graph, &Error))
        {
            Fail(ToString(Error));
            break;
        }
        FJson NodeTypes = FJson::object();
        for (const FGraphNode& Node : Graph.Nodes)
            NodeTypes[Node.TypeName] = NodeTypes.value(Node.TypeName, 0) + 1;
        Summary["graph_version"] = Graph.Version;
        Summary["graph_id"] = Graph.GraphId;
        Summary["variable_count"] = Graph.Variables.size();
        Summary["node_count"] = Graph.Nodes.size();
        Summary["link_count"] = Graph.Links.size();
        Summary["node_types"] = std::move(NodeTypes);
        break;
    }
    }
    return Summary;
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
    if (Object->IsA(PActor::StaticClass()))
    {
        const PActor* Actor = static_cast<const PActor*>(Object);
        if (Actor->GetReplicateMovement() && !Actor->GetIsReplicated())
        {
            Result["configuration_warnings"] = FJson::array({
                "Replicate Movement requires Replicates to create an ActorChannel"
            });
        }
        const FAssetPath BlueprintAsset = FindActorBlueprintAsset(Object->GetClass());
        if (BlueprintAsset.IsValid())
            Result["actor_blueprint_asset"] = BlueprintAsset.ToString();
    }
    if (Object->IsA(PPrimitiveComponent::StaticClass()))
    {
        const auto* Primitive = static_cast<const PPrimitiveComponent*>(Object);
        FJson Warnings = Result.contains("configuration_warnings")
            ? Result["configuration_warnings"] : FJson::array();
        const PActor* Owner = Primitive->GetOwner();
        if (Primitive->GetPhysicsBodyType() == EPhysicsBodyType::Dynamic
            && Owner != nullptr && Owner->GetIsReplicated()
            && !Owner->GetReplicateMovement())
        {
            Warnings.push_back(
                "A replicated Dynamic root body normally requires Replicate Movement");
        }
        if (Object->IsA(PSkeletalMeshComponent::StaticClass())
            && Primitive->GetCollisionProfile() == ECollisionProfile::Pawn)
        {
            Warnings.push_back(
                "Character Pawn blocking should normally live on the capsule, not the skeletal mesh");
        }
        if (Primitive->GetCollisionProfile() == ECollisionProfile::PhysicsActor
            && Primitive->GetPhysicsBodyType() != EPhysicsBodyType::Dynamic)
        {
            Warnings.push_back(
                "Physics Actor profile is normally paired with Dynamic Physics Body Type");
        }
        if (!Warnings.empty()) Result["configuration_warnings"] = std::move(Warnings);
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

std::string ObjectRevision(PObject* Object)
{
    return Object != nullptr
        ? StableRevision(DescribeObject(Object, true).dump()) : std::string {};
}

bool ReadFiniteNumber(const FJson& Json, float& Out);
bool JsonToVector(const FJson& Json, FVector3& Out);

FJson MaterialToJson(const FMaterialData& Material)
{
    return {{"base_color", VectorToJson(Material.BaseColor)},
        {"metallic", Material.Metallic}, {"roughness", Material.Roughness},
        {"base_color_texture", Material.BaseColorTexture.IsEmpty()
            ? FJson(nullptr)
            : FJson(std::string(Material.BaseColorTexture.ToString()))}};
}

FJson SemanticMetadataToJson(const FAssetSemanticMetadata& Metadata)
{
    return {{"schema_version", Metadata.SchemaVersion},
        {"display_name", Metadata.DisplayName},
        {"description", Metadata.Description},
        {"semantic_tags", Metadata.SemanticTags},
        {"intended_use", Metadata.IntendedUse},
        {"surface_tags", Metadata.SurfaceTags},
        {"provenance", Metadata.Provenance},
        {"source_asset_revision", Metadata.SourceAssetRevision}};
}

bool SetSemanticMetadataField(
    std::string_view Field,
    const FJson& Value,
    FAssetSemanticMetadata& Metadata,
    std::string& OutError)
{
    if (Field == "display_name" || Field == "description")
    {
        if (!Value.is_string())
        {
            OutError = std::string(Field) + " must be a string";
            return false;
        }
        if (Field == "display_name") Metadata.DisplayName = Value.get<std::string>();
        else Metadata.Description = Value.get<std::string>();
        return true;
    }
    if (Field == "semantic_tags" || Field == "intended_use"
        || Field == "surface_tags")
    {
        if (!Value.is_array())
        {
            OutError = std::string(Field) + " must be a string array";
            return false;
        }
        std::vector<std::string> Values;
        for (const FJson& Entry : Value)
        {
            if (!Entry.is_string())
            {
                OutError = std::string(Field) + " must contain only strings";
                return false;
            }
            Values.push_back(Entry.get<std::string>());
        }
        if (Field == "semantic_tags") Metadata.SemanticTags = std::move(Values);
        else if (Field == "intended_use") Metadata.IntendedUse = std::move(Values);
        else Metadata.SurfaceTags = std::move(Values);
        return true;
    }
    OutError = "Unsupported semantic metadata field: " + std::string(Field);
    return false;
}

bool JsonToMaterialField(
    std::string_view Field,
    const FJson& Value,
    FMaterialData& Material,
    std::string& OutError)
{
    if (Field == "base_color")
    {
        FVector3 Color;
        if (!JsonToVector(Value, Color) || Color.X < 0.0f || Color.X > 1.0f
            || Color.Y < 0.0f || Color.Y > 1.0f
            || Color.Z < 0.0f || Color.Z > 1.0f)
        {
            OutError = "base_color must contain x/y/z values in [0, 1]";
            return false;
        }
        Material.BaseColor = Color;
        return true;
    }
    if (Field == "metallic" || Field == "roughness")
    {
        float Number = 0.0f;
        if (!ReadFiniteNumber(Value, Number) || Number < 0.0f || Number > 1.0f)
        {
            OutError = std::string(Field) + " must be in [0, 1]";
            return false;
        }
        if (Field == "metallic") Material.Metallic = Number;
        else Material.Roughness = Number;
        return true;
    }
    if (Field == "base_color_texture")
    {
        if (Value.is_null())
        {
            Material.BaseColorTexture = {};
            return true;
        }
        FAssetPath Texture;
        if (!Value.is_string()
            || !FAssetPath::TryParse(Value.get<std::string>(), Texture)
            || Texture.GetExtension() != ".ptex")
        {
            OutError = "base_color_texture must be null or a /Game/*.ptex path";
            return false;
        }
        Material.BaseColorTexture = Texture;
        return true;
    }
    OutError = "Unsupported material update field: " + std::string(Field);
    return false;
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

class FEditorCapabilityProvider : public IAgentCapabilityProvider
{
public:
    FEditorCapabilityProvider(
        std::string InName,
        std::vector<std::string> InRevisionReadSet,
        std::vector<std::string> InRevisionWriteSet)
        : Name(std::move(InName))
        , RevisionReadSet(std::move(InRevisionReadSet))
        , RevisionWriteSet(std::move(InRevisionWriteSet))
    {
    }

    bool AddTool(FAgentToolDefinition Definition)
    {
        if (Definition.Name.empty() || !Definition.Handler) return false;
        Definition.CapabilityProvider = Name;
        if (Definition.RevisionReadSet.empty())
            Definition.RevisionReadSet = RevisionReadSet;
        if (Definition.Permission != EAgentToolPermission::ReadOnly
            && Definition.RevisionWriteSet.empty())
            Definition.RevisionWriteSet = RevisionWriteSet;
        if (!Definition.Verifier)
        {
            Definition.Verifier = [](
                const FAgentToolCall&,
                const FAgentToolResult& Result,
                std::string& OutError)
            {
                if (Result.bSucceeded) return true;
                OutError = Result.Error.empty()
                    ? "Capability tool returned a failed result" : Result.Error;
                return false;
            };
        }
        Definitions.push_back(std::move(Definition));
        return true;
    }

    std::string_view GetName() const override { return Name; }
    const std::vector<FAgentToolDefinition>& GetToolDefinitions() const override
    {
        return Definitions;
    }
    std::vector<FAgentKnowledgeRecord> CollectKnowledgeRecords() const override
    {
        FJson Tools = FJson::array();
        for (const FAgentToolDefinition& Definition : Definitions)
            Tools.push_back({{"name", Definition.Name},
                {"permission", ToString(Definition.Permission)},
                {"revision_read_set", Definition.RevisionReadSet},
                {"revision_write_set", Definition.RevisionWriteSet}});
        FAgentKnowledgeRecord Record;
        Record.SourceType = "agent-capability";
        Record.SourcePath = "PicoEditor/Capabilities/" + Name;
        Record.Title = Name + " tool capability manifest";
        Record.Content = FJson({{"provider", Name}, {"tools", std::move(Tools)}}).dump();
        Record.Tags = {"agent", "tool", "capability", Name};
        Record.Provenance = "IAgentCapabilityProvider runtime manifest";
        return {std::move(Record)};
    }

private:
    std::string Name;
    std::vector<std::string> RevisionReadSet;
    std::vector<std::string> RevisionWriteSet;
    std::vector<FAgentToolDefinition> Definitions;
};

class FWorldToolProvider final : public FEditorCapabilityProvider
{
public:
    FWorldToolProvider() : FEditorCapabilityProvider("WorldToolProvider",
        {"World.Revision", "Selection.Revision"},
        {"World.Revision", "Selection.Revision"}) {}
};

class FObjectToolProvider final : public FEditorCapabilityProvider
{
public:
    FObjectToolProvider() : FEditorCapabilityProvider("ObjectToolProvider",
        {"ObjectRegistry.Revision", "Reflection.MetadataRevision"},
        {"ObjectRegistry.Revision", "World.Revision"}) {}
};

class FAssetToolProvider final : public FEditorCapabilityProvider
{
public:
    FAssetToolProvider() : FEditorCapabilityProvider("AssetToolProvider",
        {"AssetRegistry.Revision"}, {"AssetRegistry.Revision"}) {}
};

class FBlueprintGraphToolProvider final : public FEditorCapabilityProvider
{
public:
    FBlueprintGraphToolProvider() : FEditorCapabilityProvider(
        "BlueprintGraphToolProvider", {"GraphAsset.Revision", "Reflection.MetadataRevision"},
        {"GraphAsset.Revision"}) {}
};

class FGameplayToolProvider final : public FEditorCapabilityProvider
{
public:
    FGameplayToolProvider() : FEditorCapabilityProvider("GameplayToolProvider",
        {"World.Revision", "Gameplay.SchemaRevision", "BlueprintDefaults.Revision"},
        {"World.Revision", "BlueprintDefaults.Revision"}) {}
};

class FProjectProcessToolProvider final : public FEditorCapabilityProvider
{
public:
    FProjectProcessToolProvider() : FEditorCapabilityProvider(
        "ProjectProcessToolProvider",
        {"ProjectDescriptor.Revision", "WorldAsset.Revision", "Process.State"},
        {"ProjectDescriptor.Revision", "WorldAsset.Revision", "Process.State"}) {}
};
}

struct FEditorAgentToolExecutor::FImpl
{
    struct FCachedTypedSummary
    {
        std::uint64_t SourceRevision = 0;
        std::uint64_t SizeBytes = 0;
        FJson Summary;
    };

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
        bInitialized = Registry.RegisterProvider(WorldTools) && bInitialized;
        bInitialized = Registry.RegisterProvider(ObjectTools) && bInitialized;
        bInitialized = Registry.RegisterProvider(AssetTools) && bInitialized;
        bInitialized = Registry.RegisterProvider(BlueprintGraphTools) && bInitialized;
        bInitialized = Registry.RegisterProvider(GameplayTools) && bInitialized;
        bInitialized = Registry.RegisterProvider(ProjectProcessTools) && bInitialized;
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

    std::vector<FAgentAssetDescriptor> BuildAssetDescriptors() const
    {
        std::vector<FAgentAssetDescriptor> Descriptors;
        if (!EngineLoop) return Descriptors;
        const FAssetRegistry& AssetRegistry = EngineLoop->GetAssetRegistry();
        for (const FAssetRecord& Asset : AssetRegistry.GetAssets())
        {
            FAgentAssetDescriptor Descriptor;
            const std::string AssetPath(Asset.AssetPath.ToString());
            Descriptor.Id = "asset:" + AssetPath;
            Descriptor.Kind = std::string(ToString(Asset.Type));
            Descriptor.VirtualPath = AssetPath;
            Descriptor.SourceRevision = static_cast<std::uint64_t>(
                Asset.LastWriteTime.time_since_epoch().count());
            Descriptor.SizeBytes = Asset.FileSize;
            for (const FAssetPath& Dependency :
                FAssetDependencyService::GetAssetDependencies(
                    Asset.AssetPath, AssetRegistry))
            {
                Descriptor.Dependencies.emplace_back(Dependency.ToString());
            }
            Descriptor.Tags = {"asset", Descriptor.Kind};
            const std::uint64_t Revision = static_cast<std::uint64_t>(
                Asset.LastWriteTime.time_since_epoch().count());
            const std::string CacheKey(Asset.AssetPath.ToString());
            auto Cached = TypedSummaryCache.find(CacheKey);
            if (Cached == TypedSummaryCache.end()
                || Cached->second.SourceRevision != Revision
                || Cached->second.SizeBytes != Asset.FileSize)
            {
                FCachedTypedSummary Entry;
                Entry.SourceRevision = Revision;
                Entry.SizeBytes = Asset.FileSize;
                Entry.Summary = BuildTypedAssetSummary(Asset);
                Cached = TypedSummaryCache.insert_or_assign(
                    CacheKey, std::move(Entry)).first;
            }
            FJson Summary = Cached->second.Summary;
            Summary["file_size"] = Descriptor.SizeBytes;
            Summary["dependency_count"] = Descriptor.Dependencies.size();
            FAssetSemanticMetadata Metadata;
            const FAssetSemanticMetadataResult MetadataResult =
                FAssetSemanticMetadataService::Load(Asset.FilePath, Metadata);
            if (MetadataResult.bSucceeded && MetadataResult.bExists)
            {
                const std::string AssetRevision = FileRevision(Asset.FilePath);
                Summary["semantic_metadata"] = SemanticMetadataToJson(Metadata);
                Summary["semantic_metadata"]["revision"] = MetadataResult.Revision;
                Summary["semantic_metadata"]["stale"] =
                    !Metadata.SourceAssetRevision.empty()
                    && Metadata.SourceAssetRevision != AssetRevision;
                Descriptor.Tags.insert(Descriptor.Tags.end(),
                    Metadata.SemanticTags.begin(), Metadata.SemanticTags.end());
                Descriptor.Tags.insert(Descriptor.Tags.end(),
                    Metadata.SurfaceTags.begin(), Metadata.SurfaceTags.end());
            }
            Descriptor.SummaryJson = std::move(Summary).dump();
            Descriptor.Provenance =
                "Live AssetRegistry, typed asset loader, and asset dependency service";
            Descriptor.ValidatorId = "asset.typed-descriptor.v1";
            Descriptors.push_back(std::move(Descriptor));
        }
        PWorld* World = EngineLoop->GetWorld();
        if (World)
        {
            FAgentAssetDescriptor Descriptor;
            Descriptor.Id = "world:" + World->GetPathName();
            Descriptor.Kind = "WorldSnapshot";
            Descriptor.VirtualPath = World->GetPathName();
            std::size_t ActorCount = 0;
            for (PLevel* Level : World->GetLevels())
                if (Level) ActorCount += Level->GetActors().size();
            for (const FObjectAssetReference& Reference :
                FAssetDependencyService::GatherWorldReferences(World))
            {
                const std::string Path(Reference.AssetPath.ToString());
                if (std::find(Descriptor.Dependencies.begin(),
                        Descriptor.Dependencies.end(), Path)
                    == Descriptor.Dependencies.end())
                    Descriptor.Dependencies.push_back(Path);
            }
            Descriptor.SourceRevision = ActorCount;
            Descriptor.Tags = {"world", "scene", "actor"};
            Descriptor.SummaryJson = FJson{{"actor_count", ActorCount},
                {"asset_reference_count", Descriptor.Dependencies.size()}}.dump();
            Descriptor.Provenance = "Live Game Thread World and reflected asset references";
            Descriptor.ValidatorId = "world.snapshot.references";
            Descriptors.push_back(std::move(Descriptor));
        }
        return Descriptors;
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
            Record.SourceRevision = WorldJson["actors"].size();
            Record.EntityIds.push_back(World->GetPathName());
            for (const FJson& Actor : WorldJson["actors"])
                Record.EntityIds.push_back(Actor.value("object_path", ""));
            Record.RevisionDomain = "World.Revision";
            Record.Fields = {{"world_path", World->GetPathName()},
                {"actor_count", std::to_string(WorldJson["actors"].size())}};
            Record.Kind = EAgentKnowledgeKind::Entity;
            Record.Provenance = "Live Game Thread World snapshot";
            Result.push_back(std::move(Record));
        }

        if (EngineLoop)
        {
            const std::vector<FAgentAssetDescriptor> Assets =
                BuildAssetDescriptors();
            FAgentKnowledgeRecord Record;
            Record.SourceType = "asset-descriptor";
            Record.SourcePath = "/Game";
            Record.Title = "Versioned project asset and World descriptors";
            Record.Content = SerializeAgentAssetDescriptors(Assets);
            Record.SourceRevision = Assets.size();
            Record.Tags = {"asset", "descriptor", "dependency", "world"};
            Record.Provenance =
                "Live AssetRegistry, dependency service, and World snapshot";
            for (const FAgentAssetDescriptor& Asset : Assets)
            {
                Record.EntityIds.push_back(Asset.Id);
                Record.EntityIds.push_back(Asset.VirtualPath);
            }
            Record.RevisionDomain = "Asset.Revision";
            Record.Fields = {{"asset_count", std::to_string(Assets.size())}};
            Record.Kind = EAgentKnowledgeKind::Entity;
            Result.push_back(std::move(Record));
        }

        if (Selection && Selection->Num() > 0)
        {
            const std::vector<PObject*> SelectedObjects = Selection->ResolveAll();
            PObject* PrimaryObject = Selection->Resolve();
            if (!SelectedObjects.empty() && PrimaryObject)
            {
                FJson Objects = FJson::array();
                std::vector<std::string> ObjectPaths;
                ObjectPaths.reserve(SelectedObjects.size());
                for (PObject* Object : SelectedObjects)
                {
                    if (!Object) continue;
                    ObjectPaths.push_back(Object->GetPathName());
                    Objects.push_back(DescribeObject(Object, true));
                }
                FAgentKnowledgeRecord Record;
                Record.SourceType = "selection";
                Record.SourcePath = PrimaryObject->GetPathName();
                Record.Title = "Current editor selection ("
                    + std::to_string(ObjectPaths.size()) + " objects)";
                Record.Content = FJson{{"primary", PrimaryObject->GetPathName()},
                    {"objects", std::move(Objects)}}.dump();
                Record.Tags = {"selection", "reflection", "property"};
                Record.SourceRevision = Selection->GetRevision();
                Record.EntityIds = ObjectPaths;
                Record.RevisionDomain = "Selection.Revision";
                Record.Fields = {{"primary_object_path", PrimaryObject->GetPathName()},
                    {"selection_count", std::to_string(ObjectPaths.size())}};
                Record.Kind = EAgentKnowledgeKind::Entity;
                Record.Provenance = "Live editor selection and PProperty metadata";
                Result.push_back(std::move(Record));

                if (PGameplayAbilitySystemComponent* AbilitySystem =
                        FindAbilitySystem(PrimaryObject))
                {
                    FAgentKnowledgeRecord GameplayRecord;
                    GameplayRecord.SourceType = "gameplay-abilities";
                    GameplayRecord.SourcePath = AbilitySystem->GetPathName();
                    GameplayRecord.Title = "Selected Ability System state";
                    GameplayRecord.Content = DescribeAbilitySystem(AbilitySystem).dump();
                    GameplayRecord.Tags = {"gas", "ability", "attribute", "effect", "tag"};
                    GameplayRecord.EntityIds = {PrimaryObject->GetPathName(),
                        AbilitySystem->GetPathName()};
                    GameplayRecord.RevisionDomain = "Gameplay.Revision";
                    GameplayRecord.Fields = {
                        {"ability_system_path", AbilitySystem->GetPathName()}};
                    GameplayRecord.Kind = EAgentKnowledgeKind::Entity;
                    GameplayRecord.Provenance =
                        "Live ASC, AttributeSet, AbilitySpec, GameplayTag and ActiveEffect state";
                    Result.push_back(std::move(GameplayRecord));
                }
            }
        }

        {
            FJson Schema = FJson::array();
            for (const FGraphNodeSchema& Node :
                GetDefaultGraphSchemaRegistry().GetSchemas())
            {
                FJson Pins = FJson::array();
                for (const FGraphPinSchema& Pin : Node.Pins)
                    Pins.push_back({{"name", Pin.Name},
                        {"direction", ToString(Pin.Direction)},
                        {"type", ToString(Pin.Type)},
                        {"default", Pin.DefaultValue}});
                Schema.push_back({{"type", Node.TypeName},
                    {"display_name", Node.DisplayName},
                    {"opcode", ToString(Node.Opcode)}, {"pins", std::move(Pins)}});
            }
            FAgentKnowledgeRecord Record;
            Record.SourceType = "picograph-schema";
            Record.SourcePath = "PicoGraph/SchemaRegistry";
            Record.Title = "PicoGraph registered node schema";
            Record.Content = Schema.dump();
            Record.SourceRevision = PicoGraphBytecodeVersion;
            Record.Tags = {"graph", "schema", "node", "pin", "bytecode"};
            Record.Provenance = "Live FGraphSchemaRegistry";
            Record.RevisionDomain = "Graph.SchemaRevision";
            Record.Fields = {{"schema", "PicoGraph"},
                {"bytecode_version", std::to_string(PicoGraphBytecodeVersion)}};
            Record.Kind = EAgentKnowledgeKind::Procedure;
            Result.push_back(std::move(Record));
        }

        {
            FJson Classes = FJson::array();
            for (const PClass* Class : FClassRegistry::GetClasses())
            {
                if (Class == nullptr) continue;
                FJson Properties = FJson::array();
                for (const PProperty& Property : Class->GetProperties())
                {
                    const FPropertyMetadata& Metadata = Property.GetMetadata();
                    Properties.push_back({{"name", Property.GetName().ToString()},
                        {"type_id", static_cast<int>(Property.GetType())},
                        {"editable", Property.HasAnyFlags(EPropertyFlags::Editable)
                            && !Property.HasAnyFlags(EPropertyFlags::ReadOnly)},
                        {"display_name", Metadata.DisplayName},
                        {"description", Metadata.Description},
                        {"semantic", Metadata.Semantic}});
                }
                FJson Functions = FJson::array();
                for (const PFunction& Function : Class->GetFunctions())
                    if (Function.HasAnyFlags(EFunctionFlags::Callable))
                        Functions.push_back({{"name", Function.GetName().ToString()},
                            {"parameter_count", Function.GetParameters().size()},
                            {"pure", Function.HasAnyFlags(EFunctionFlags::Pure)}});
                if (!Properties.empty() || !Functions.empty())
                    Classes.push_back({{"class", Class->GetName().ToString()},
                        {"properties", std::move(Properties)},
                        {"functions", std::move(Functions)}});
            }
            FAgentKnowledgeRecord Record;
            Record.SourceType = "reflection-schema";
            Record.SourcePath = "PicoObject/ClassRegistry";
            Record.Title = "Live reflected Gameplay and object schema";
            Record.Content = Classes.dump();
            Record.SourceRevision = Classes.size();
            Record.Tags = {"reflection", "class", "property", "function", "graph"};
            Record.Provenance = "Live PClass/PProperty/PFunction metadata";
            Record.RevisionDomain = "Reflection.SchemaRevision";
            Record.Fields = {{"class_count", std::to_string(Classes.size())}};
            Record.Kind = EAgentKnowledgeKind::Procedure;
            Result.push_back(std::move(Record));
        }

        {
            FAgentKnowledgeRecord Record;
            Record.SourceType = "gameplay-schema";
            Record.SourcePath = "PicoGameplayAbilities/MiniGAS";
            Record.Title = "Pico Mini GAS safe configuration schema";
            Record.Content = FJson({
                {"schema_revision", 2},
                {"profile_owner", "reflected Mini GAS Profile owner"},
                {"abilities", FJson::array({
                    {{"name", "GravityShot"}, {"input", "1"},
                        {"enabled", "bGravityShotEnabled"},
                        {"ability_tag", "Ability.Projectile.Gravity"}},
                    {{"name", "BurnShot"}, {"input", "2"},
                        {"enabled", "bBurnShotEnabled"},
                        {"ability_tag", "Ability.Projectile.Burn"}},
                    {{"name", "FreezeShot"}, {"input", "3"},
                        {"enabled", "bFreezeShotEnabled"},
                        {"ability_tag", "Ability.Projectile.Freeze"}}})},
                {"configurable_groups", FJson::array({
                    "initial health", "initial mana", "enabled", "mana cost", "cooldown", "range", "projectile speed",
                    "projectile color", "effect duration", "effect strength"})},
                {"persistence", FJson::array({
                    "Placed World Pawn changes affect only that serialized instance",
                    "GameMode-spawned players inherit Actor Blueprint generated defaults",
                    "InitialHealth and InitialMana are persistent inputs; Replicated fields are read-only runtime mirrors"})},
                {"constraints", FJson::array({
                    "Only registered Ability classes may be granted",
                    "All three projectiles and impact Effects are server authoritative",
                    "The Agent may configure existing abilities but may not generate arbitrary C++"})}
            }).dump();
            Record.Tags = {"gas", "schema", "gravity", "burn", "freeze", "network"};
            Record.Provenance = "PicoGameplayAbilities Runtime contract";
            Record.SourceRevision = 2;
            Record.EntityIds = {"PicoGameplayAbilities/MiniGAS"};
            Record.RevisionDomain = "Gameplay.SchemaRevision";
            Record.Fields = {{"schema", "MiniGAS"}, {"schema_revision", "2"}};
            Record.Kind = EAgentKnowledgeKind::Procedure;
            Result.push_back(std::move(Record));
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
            Record.RevisionDomain = "MessageLog.Revision";
            Record.Fields = {{"latest_sequence",
                std::to_string(LatestIssueSequence)}};
            Record.Kind = EAgentKnowledgeKind::Episode;
            Result.push_back(std::move(Record));
        }
        for (const IAgentCapabilityProvider* Provider : {
            static_cast<const IAgentCapabilityProvider*>(&WorldTools),
            static_cast<const IAgentCapabilityProvider*>(&ObjectTools),
            static_cast<const IAgentCapabilityProvider*>(&AssetTools),
            static_cast<const IAgentCapabilityProvider*>(&BlueprintGraphTools),
            static_cast<const IAgentCapabilityProvider*>(&GameplayTools),
            static_cast<const IAgentCapabilityProvider*>(&ProjectProcessTools)})
        {
            std::vector<FAgentKnowledgeRecord> Records =
                Provider->CollectKnowledgeRecords();
            Result.insert(Result.end(),
                std::make_move_iterator(Records.begin()),
                std::make_move_iterator(Records.end()));
        }
        return Result;
    }

    bool RegisterTool(FAgentToolDefinition Definition)
    {
        const std::string& Name = Definition.Name;
        if (Name.starts_with("editor.world.")
            || Name.starts_with("editor.actor.")
            || Name.starts_with("editor.scene."))
            return WorldTools.AddTool(std::move(Definition));
        if (Name.starts_with("editor.object.")
            || Name.starts_with("editor.selection.")
            || Name.starts_with("editor.component."))
            return ObjectTools.AddTool(std::move(Definition));
        if (Name.starts_with("editor.asset.")
            || Name.starts_with("editor.material."))
            return AssetTools.AddTool(std::move(Definition));
        if (Name.starts_with("editor.graph."))
            return BlueprintGraphTools.AddTool(std::move(Definition));
        if (Name.starts_with("editor.gameplay.")
            || Name.starts_with("editor.actor_blueprint."))
            return GameplayTools.AddTool(std::move(Definition));
        if (Name.starts_with("editor.project.")
            || Name.starts_with("editor.play.")
            || Name.starts_with("editor.agent."))
            return ProjectProcessTools.AddTool(std::move(Definition));
        PICO_LOG(LogAgent, Error, "No capability provider owns tool '{}'", Name);
        return false;
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
                    const FAssetPath BlueprintAsset =
                        FindActorBlueprintAsset(Actor->GetClass());
                    if (BlueprintAsset.IsValid())
                        ActorJson["actor_blueprint_asset"] = BlueprintAsset.ToString();
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
        bInitialized = RegisterTool(std::move(DescribeWorld));

        FAgentToolDefinition DescribeAsc;
        DescribeAsc.Name = "editor.gameplay.asc.describe";
        DescribeAsc.Description =
            "Describe the existing AbilitySystemComponent, attributes, tags, granted Ability specs, and active Effects for an Actor or Component";
        DescribeAsc.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512}};
        DescribeAsc.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Arguments.at("object_path").get<std::string>());
            PGameplayAbilitySystemComponent* AbilitySystem = FindAbilitySystem(Object);
            return AbilitySystem != nullptr
                ? Success(Call, DescribeAbilitySystem(AbilitySystem))
                : Failure(Call, "Object has no AbilitySystemComponent");
        };
        bInitialized = RegisterTool(std::move(DescribeAsc)) && bInitialized;

        FAgentToolDefinition ConfigureLoadout;
        ConfigureLoadout.Name = "editor.gameplay.configure_ability_loadout";
        ConfigureLoadout.Description =
            "Configure Gravity, Burn, and Freeze on one placed World Pawn instance only; for GameMode-spawned players persist the same reflected flags through editor.actor_blueprint.set_defaults";
        ConfigureLoadout.Permission = EAgentToolPermission::ModifyWorld;
        ConfigureLoadout.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512},
            {"gravity", EAgentToolValueType::Boolean, true},
            {"burn", EAgentToolValueType::Boolean, true},
            {"freeze", EAgentToolValueType::Boolean, true},
            {"gravity_cost", EAgentToolValueType::Number, false, 0.0, 10000.0},
            {"gravity_cooldown", EAgentToolValueType::Number, false, 0.0, 3600.0},
            {"gravity_range", EAgentToolValueType::Number, false, 0.01, 100000.0},
            {"gravity_projectile_speed", EAgentToolValueType::Number, false, 0.01, 100000.0},
            {"gravity_duration", EAgentToolValueType::Number, false, 0.0, 3600.0},
            {"gravity_launch_velocity", EAgentToolValueType::Number, false, 0.0, 100000.0},
            {"burn_cost", EAgentToolValueType::Number, false, 0.0, 10000.0},
            {"burn_cooldown", EAgentToolValueType::Number, false, 0.0, 3600.0},
            {"burn_range", EAgentToolValueType::Number, false, 0.01, 100000.0},
            {"burn_projectile_speed", EAgentToolValueType::Number, false, 0.01, 100000.0},
            {"burn_duration", EAgentToolValueType::Number, false, 0.0, 3600.0},
            {"burn_tick_interval", EAgentToolValueType::Number, false, 0.01, 3600.0},
            {"burn_damage_per_tick", EAgentToolValueType::Number, false, 0.0, 10000.0},
            {"freeze_cost", EAgentToolValueType::Number, false, 0.0, 10000.0},
            {"freeze_cooldown", EAgentToolValueType::Number, false, 0.0, 3600.0},
            {"freeze_range", EAgentToolValueType::Number, false, 0.01, 100000.0},
            {"freeze_projectile_speed", EAgentToolValueType::Number, false, 0.01, 100000.0},
            {"freeze_duration", EAgentToolValueType::Number, false, 0.0, 3600.0}};
        ConfigureLoadout.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Arguments.at("object_path").get<std::string>());
            PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
                ? static_cast<PActor*>(Object) : nullptr;
            if (Actor == nullptr || Actor->GetClass() == nullptr)
                return Failure(Call, "Actor was not found");
            const int32 Bits = (Arguments.at("gravity").get<bool>() ? 1 : 0)
                | (Arguments.at("burn").get<bool>() ? 2 : 0)
                | (Arguments.at("freeze").get<bool>() ? 4 : 0);
            const auto ApplyProperty = [this, Actor](
                const char* PropertyName, FEditorPropertyValue Value,
                std::string& Error)
            {
                const PProperty* Property = Actor->GetClass()->FindProperty(FName(PropertyName));
                if (Property == nullptr)
                {
                    Error = std::string("Actor has no reflected property ") + PropertyName;
                    return false;
                }
                const FEditorPropertyResult Applied = ApplyEditorPropertyValue(
                    EngineLoop, Actor, Property, Value);
                if (!Applied.bSucceeded) Error = Applied.Message;
                return Applied.bSucceeded;
            };
            std::string ApplyError;
            if (!ApplyProperty("bGravityShotEnabled",
                    FEditorPropertyValue(Arguments.at("gravity").get<bool>()), ApplyError)
                || !ApplyProperty("bBurnShotEnabled",
                    FEditorPropertyValue(Arguments.at("burn").get<bool>()), ApplyError)
                || !ApplyProperty("bFreezeShotEnabled",
                    FEditorPropertyValue(Arguments.at("freeze").get<bool>()), ApplyError))
                return Failure(Call, ApplyError);
            const std::pair<const char*, const char*> NumberProperties[] = {
                {"gravity_cost", "GravityManaCost"},
                {"gravity_cooldown", "GravityCooldownSeconds"},
                {"gravity_range", "GravityRange"},
                {"gravity_projectile_speed", "GravityProjectileSpeed"},
                {"gravity_duration", "GravityEffectDuration"},
                {"gravity_launch_velocity", "GravityLaunchVelocity"},
                {"burn_cost", "BurnManaCost"},
                {"burn_cooldown", "BurnCooldownSeconds"},
                {"burn_range", "BurnRange"},
                {"burn_projectile_speed", "BurnProjectileSpeed"},
                {"burn_duration", "BurnEffectDuration"},
                {"burn_tick_interval", "BurnTickInterval"},
                {"burn_damage_per_tick", "BurnDamagePerTick"},
                {"freeze_cost", "FreezeManaCost"},
                {"freeze_cooldown", "FreezeCooldownSeconds"},
                {"freeze_range", "FreezeRange"},
                {"freeze_projectile_speed", "FreezeProjectileSpeed"},
                {"freeze_duration", "FreezeEffectDuration"}};
            for (const auto& [ArgumentName, PropertyName] : NumberProperties)
            {
                if (!Arguments.contains(ArgumentName)) continue;
                if (!ApplyProperty(PropertyName,
                        FEditorPropertyValue(Arguments.at(ArgumentName).get<float>()),
                        ApplyError))
                    return Failure(Call, ApplyError);
            }

            PGameplayAbilitySystemComponent* AbilitySystem = FindAbilitySystem(Actor);
            if (AbilitySystem != nullptr && Actor->HasBegunPlay())
            {
                struct FAbilityEntry
                {
                    int32 Bit;
                    const char* AbilityTag;
                    int32 InputId;
                    const char* CostProperty;
                    const char* CooldownProperty;
                };
                const FAbilityEntry Entries[] = {
                    {1, "Ability.Projectile.Gravity", 0,
                        "GravityManaCost", "GravityCooldownSeconds"},
                    {2, "Ability.Projectile.Burn", 1,
                        "BurnManaCost", "BurnCooldownSeconds"},
                    {4, "Ability.Projectile.Freeze", 2,
                        "FreezeManaCost", "FreezeCooldownSeconds"}};
                for (const FAbilityEntry& Entry : Entries)
                {
                    const PClass* Class = FindGameplayAbilityClassByTag(
                        Entry.AbilityTag);
                    FGameplayAbilitySpecHandle Existing;
                    for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
                        if (Spec.AbilityClass == Class) { Existing = Spec.Handle; break; }
                    if ((Bits & Entry.Bit) != 0 && !Existing.IsValid() && Class != nullptr)
                        Existing = AbilitySystem->GiveAbility(Class, 1, Entry.InputId);
                    else if ((Bits & Entry.Bit) == 0 && Existing.IsValid())
                        AbilitySystem->ClearAbility(Existing);
                    if ((Bits & Entry.Bit) != 0 && Existing.IsValid())
                    {
                        float Cost = 0.0f;
                        float Cooldown = 0.0f;
                        const PProperty* CostProperty = Actor->GetClass()->FindProperty(
                            FName(Entry.CostProperty));
                        const PProperty* CooldownProperty = Actor->GetClass()->FindProperty(
                            FName(Entry.CooldownProperty));
                        if (CostProperty != nullptr && CooldownProperty != nullptr
                            && CostProperty->GetValue(Actor, Cost)
                            && CooldownProperty->GetValue(Actor, Cooldown))
                            AbilitySystem->ConfigureAbilitySpec(Existing, Cost, Cooldown);
                    }
                }
            }
            if (Selection) Selection->Set(Actor);
            return Success(Call, {{"object_path", Actor->GetPathName()},
                {"loadout_bits", Bits}, {"gravity", (Bits & 1) != 0},
                {"burn", (Bits & 2) != 0}, {"freeze", (Bits & 4) != 0},
                {"mini_gas_profile", DescribeMiniGasProfile(Actor)}});
        };
        ConfigureLoadout.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Output = FJson::parse(Result.OutputJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Output.at("object_path").get<std::string>());
            if (Object == nullptr
                || DescribeMiniGasProfile(Object) != Output.at("mini_gas_profile"))
            {
                Error = "Mini GAS profile postcondition was not satisfied";
                return false;
            }
            return true;
        };
        bInitialized = RegisterTool(std::move(ConfigureLoadout)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(ListChanges)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(RevertRun)) && bInitialized;

        FAgentToolDefinition DescribeSelection;
        DescribeSelection.Name = "editor.selection.describe";
        DescribeSelection.Description = "Read the current editor object selection";
        DescribeSelection.Handler = [this](const FAgentToolCall& Call, const FCancellationToken*)
        {
            return Success(Call, {{"primary", Selection ? Selection->GetObjectPath() : ""},
                {"objects", Selection ? Selection->GetObjectPaths() : std::vector<std::string> {}}});
        };
        bInitialized = RegisterTool(std::move(DescribeSelection)) && bInitialized;

        FAgentToolDefinition SearchAssets;
        SearchAssets.Name = "editor.asset.search";
        SearchAssets.Description =
            "Search registered project assets by case-insensitive path or user-confirmed semantic metadata text and optional exact asset type; returns at most 50 results";
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
                for (const FAgentAssetDescriptor& Descriptor : BuildAssetDescriptors())
                {
                    if (!Descriptor.Id.starts_with("asset:")) continue;
                    std::string SearchText = Descriptor.VirtualPath + "\n"
                        + Descriptor.SummaryJson;
                    for (const std::string& Tag : Descriptor.Tags)
                        SearchText += "\n" + Tag;
                    std::transform(SearchText.begin(), SearchText.end(), SearchText.begin(),
                        [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
                    std::string RecordType = Descriptor.Kind;
                    std::transform(RecordType.begin(), RecordType.end(), RecordType.begin(),
                        [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
                    if ((Query.empty() || SearchText.find(Query) != std::string::npos)
                        && (Type == "any" || Type.empty() || RecordType == Type))
                    {
                        FJson Match = {{"path", Descriptor.VirtualPath},
                            {"type", Descriptor.Kind}, {"tags", Descriptor.Tags}};
                        const FJson Summary = FJson::parse(Descriptor.SummaryJson);
                        if (Summary.contains("semantic_metadata"))
                            Match["semantic_metadata"] = Summary.at("semantic_metadata");
                        Assets.push_back(std::move(Match));
                        if (Assets.size() == 50) break;
                    }
                }
            }
            return Success(Call, {{"assets", std::move(Assets)}});
        };
        bInitialized = RegisterTool(std::move(SearchAssets)) && bInitialized;

        FAgentToolDefinition DescribeAssetCatalog;
        DescribeAssetCatalog.Name = "editor.asset.describe_catalog";
        DescribeAssetCatalog.Description =
            "Return versioned typed AssetDescriptor records for project assets and the active World, including deterministic technical characteristics, dependencies, provenance, and verifier ids";
        DescribeAssetCatalog.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const std::vector<FAgentAssetDescriptor> Descriptors =
                BuildAssetDescriptors();
            return Success(Call, {{"format_version", 1},
                {"descriptor_count", Descriptors.size()},
                {"descriptors", FJson::parse(
                    SerializeAgentAssetDescriptors(Descriptors))}});
        };
        bInitialized = RegisterTool(std::move(DescribeAssetCatalog))
            && bInitialized;

        FAgentToolDefinition DescribeAsset;
        DescribeAsset.Name = "editor.asset.describe";
        DescribeAsset.Description =
            "Describe one exact registered asset with its typed technical summary, dependencies, registered-project-asset referencers, active-World references, and content revision; other Worlds are not scanned";
        DescribeAsset.Schema.Fields = {
            {"asset_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath}};
        DescribeAsset.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath Path;
            if (!FAssetPath::TryParse(
                    Arguments.at("asset_path").get<std::string>(), Path))
                return Failure(Call, "Asset path is invalid");
            const FAssetRecord* Record = EngineLoop
                ? EngineLoop->GetAssetRegistry().Find(Path) : nullptr;
            if (Record == nullptr) return Failure(Call, "Asset was not found");
            FJson Dependencies = FJson::array();
            for (const FAssetPath& Dependency :
                FAssetDependencyService::GetAssetDependencies(
                    Path, EngineLoop->GetAssetRegistry()))
                Dependencies.push_back(std::string(Dependency.ToString()));
            FJson Referencers = FJson::array();
            for (const FAssetPath& Referencer :
                FAssetDependencyService::FindAssetReferencers(
                    Path, EngineLoop->GetAssetRegistry()))
                Referencers.push_back(std::string(Referencer.ToString()));
            FJson WorldReferences = FJson::array();
            for (const FObjectAssetReference& Reference :
                FAssetDependencyService::FindWorldReferencers(
                    EngineLoop->GetWorld(), Path))
                WorldReferences.push_back({{"object_path", Reference.ObjectPath},
                    {"property", Reference.PropertyName.ToString()}});
            FJson Summary = BuildTypedAssetSummary(*Record);
            FAssetSemanticMetadata Metadata;
            const FAssetSemanticMetadataResult MetadataResult =
                FAssetSemanticMetadataService::Load(Record->FilePath, Metadata);
            if (!MetadataResult.bSucceeded)
                return Failure(Call, MetadataResult.Message);
            if (MetadataResult.bExists)
            {
                Summary["semantic_metadata"] = SemanticMetadataToJson(Metadata);
                Summary["semantic_metadata"]["revision"] = MetadataResult.Revision;
                Summary["semantic_metadata"]["stale"] =
                    !Metadata.SourceAssetRevision.empty()
                    && Metadata.SourceAssetRevision != FileRevision(Record->FilePath);
            }
            return Success(Call, {{"asset_path", Path.ToString()},
                {"type", ToString(Record->Type)},
                {"revision", FileRevision(Record->FilePath)},
                {"file_size", Record->FileSize},
                {"summary", std::move(Summary)},
                {"dependencies", std::move(Dependencies)},
                {"asset_referencers", std::move(Referencers)},
                {"world_references", std::move(WorldReferences)},
                {"reference_scope", {{"asset_referencers", "registered_project_assets"},
                    {"world_references", "loaded_active_world"},
                    {"other_worlds_scanned", false},
                    {"indirect_references_included", false}}}});
        };
        bInitialized = RegisterTool(std::move(DescribeAsset)) && bInitialized;

        FAgentToolDefinition FindAssetReferences;
        FindAssetReferences.Name = "editor.asset.find_references";
        FindAssetReferences.Description =
            "Report registered project assets and loaded active-World properties that explicitly reference one exact asset; other Worlds and indirect references are not scanned";
        FindAssetReferences.Schema.Fields = {
            {"asset_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath}};
        FindAssetReferences.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FAssetPath Path;
            const std::string Text = FJson::parse(Call.ArgumentsJson)
                .at("asset_path").get<std::string>();
            if (!FAssetPath::TryParse(Text, Path) || !EngineLoop
                || EngineLoop->GetAssetRegistry().Find(Path) == nullptr)
                return Failure(Call, "Asset was not found");
            FJson Assets = FJson::array();
            for (const FAssetPath& Referencer :
                FAssetDependencyService::FindAssetReferencers(
                    Path, EngineLoop->GetAssetRegistry()))
                Assets.push_back(std::string(Referencer.ToString()));
            FJson World = FJson::array();
            for (const FObjectAssetReference& Reference :
                FAssetDependencyService::FindWorldReferencers(
                    EngineLoop->GetWorld(), Path))
                World.push_back({{"object_path", Reference.ObjectPath},
                    {"property", Reference.PropertyName.ToString()}});
            return Success(Call, {{"asset_path", Path.ToString()},
                {"asset_referencers", std::move(Assets)},
                {"world_references", std::move(World)},
                {"reference_scope", {{"asset_referencers", "registered_project_assets"},
                    {"world_references", "loaded_active_world"},
                    {"other_worlds_scanned", false},
                    {"indirect_references_included", false}}}});
        };
        bInitialized = RegisterTool(std::move(FindAssetReferences)) && bInitialized;

        FAgentToolDefinition GetSemanticMetadata;
        GetSemanticMetadata.Name = "editor.asset.semantic_metadata.get";
        GetSemanticMetadata.Description =
            "Read user-confirmed semantic metadata for one exact asset, including independent metadata and source-asset revisions";
        GetSemanticMetadata.Schema.Fields = {
            {"asset_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath}};
        GetSemanticMetadata.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FAssetPath Path;
            const std::string Text = FJson::parse(Call.ArgumentsJson)
                .at("asset_path").get<std::string>();
            if (!FAssetPath::TryParse(Text, Path) || !EngineLoop)
                return Failure(Call, "Asset path is invalid");
            const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(Path);
            if (Record == nullptr) return Failure(Call, "Asset was not found");
            FAssetSemanticMetadata Metadata;
            const FAssetSemanticMetadataResult Loaded =
                FAssetSemanticMetadataService::Load(Record->FilePath, Metadata);
            if (!Loaded.bSucceeded) return Failure(Call, Loaded.Message);
            const std::string AssetRevision = FileRevision(Record->FilePath);
            return Success(Call, {{"asset_path", Path.ToString()},
                {"exists", Loaded.bExists},
                {"metadata_revision", Loaded.Revision},
                {"asset_revision", AssetRevision},
                {"stale", Loaded.bExists
                    && !Metadata.SourceAssetRevision.empty()
                    && Metadata.SourceAssetRevision != AssetRevision},
                {"metadata", SemanticMetadataToJson(Metadata)}});
        };
        bInitialized = RegisterTool(std::move(GetSemanticMetadata)) && bInitialized;

        FAgentToolDefinition SetSemanticMetadata;
        SetSemanticMetadata.Name = "editor.asset.semantic_metadata.set";
        SetSemanticMetadata.Description =
            "Update only explicitly masked user-confirmed semantic metadata fields after checking both metadata and source-asset revisions; never writes model inference as formal fact";
        SetSemanticMetadata.Permission = EAgentToolPermission::WriteProject;
        SetSemanticMetadata.Schema.Fields = {
            {"asset_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"expected_metadata_revision", EAgentToolValueType::String,
                true, {}, {}, 32},
            {"expected_asset_revision", EAgentToolValueType::String,
                true, {}, {}, 32},
            {"update_mask", EAgentToolValueType::Array, true},
            {"values", EAgentToolValueType::Object, true}};
        SetSemanticMetadata.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath Path;
            if (!FAssetPath::TryParse(
                    Arguments.at("asset_path").get<std::string>(), Path)
                || !EngineLoop)
                return Failure(Call, "Asset path is invalid");
            const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(Path);
            if (Record == nullptr) return Failure(Call, "Asset was not found");
            const std::string AssetRevision = FileRevision(Record->FilePath);
            if (AssetRevision
                != Arguments.at("expected_asset_revision").get<std::string>())
                return Failure(Call,
                    "Source asset changed; describe it again before editing metadata");
            FAssetSemanticMetadata Before;
            const FAssetSemanticMetadataResult Loaded =
                FAssetSemanticMetadataService::Load(Record->FilePath, Before);
            if (!Loaded.bSucceeded) return Failure(Call, Loaded.Message);
            if (Loaded.Revision
                != Arguments.at("expected_metadata_revision").get<std::string>())
                return Failure(Call,
                    "Semantic metadata changed; read it again before updating");
            const FJson& Mask = Arguments.at("update_mask");
            const FJson& Values = Arguments.at("values");
            if (!Mask.is_array() || Mask.empty() || Mask.size() > 5
                || !Values.is_object() || Values.size() != Mask.size())
                return Failure(Call,
                    "update_mask and values must name the same 1-5 metadata fields");
            FAssetSemanticMetadata After = Before;
            After.Provenance = "user-confirmed";
            After.SourceAssetRevision = AssetRevision;
            std::unordered_set<std::string> Seen;
            for (const FJson& Entry : Mask)
            {
                if (!Entry.is_string())
                    return Failure(Call, "update_mask entries must be strings");
                const std::string Field = Entry.get<std::string>();
                if (!Seen.insert(Field).second || !Values.contains(Field))
                    return Failure(Call,
                        "update_mask contains duplicates or missing values");
                std::string Error;
                if (!SetSemanticMetadataField(
                        Field, Values.at(Field), After, Error))
                    return Failure(Call, Error);
            }
            for (auto It = Values.begin(); It != Values.end(); ++It)
                if (!Seen.contains(It.key()))
                    return Failure(Call,
                        "values contains a field outside update_mask");
            std::string ValidationError;
            if (!FAssetSemanticMetadataService::Validate(
                    After, &ValidationError))
                return Failure(Call, ValidationError);
            const FAssetSemanticMetadataResult Saved =
                FAssetSemanticMetadataService::Save(Record->FilePath, After);
            if (!Saved.bSucceeded) return Failure(Call, Saved.Message);
            FAssetSemanticMetadata ReadBack;
            const FAssetSemanticMetadataResult Verified =
                FAssetSemanticMetadataService::Load(Record->FilePath, ReadBack);
            if (!Verified.bSucceeded || !Verified.bExists || ReadBack != After)
            {
                if (Loaded.bExists)
                    FAssetSemanticMetadataService::Save(Record->FilePath, Before);
                else
                {
                    std::error_code RemoveError;
                    std::filesystem::remove(
                        FAssetSemanticMetadataService::GetSidecarPath(
                            Record->FilePath), RemoveError);
                }
                return Failure(Call,
                    "Semantic metadata failed read-back verification and was restored");
            }
            FJson Changed = FJson::object();
            for (const std::string& Field : Seen) Changed[Field] = Values.at(Field);
            return Success(Call, {{"asset_path", Path.ToString()},
                {"operation_id", Call.Id},
                {"before", SemanticMetadataToJson(Before)},
                {"after", SemanticMetadataToJson(ReadBack)},
                {"changed_fields", std::move(Changed)},
                {"asset_revision", AssetRevision},
                {"metadata_revision_before", Loaded.Revision},
                {"metadata_revision_after", Verified.Revision},
                {"provenance", "user-confirmed"}});
        };
        bInitialized = RegisterTool(std::move(SetSemanticMetadata)) && bInitialized;

        FAgentToolDefinition DescribeMaterial;
        DescribeMaterial.Name = "editor.material.describe";
        DescribeMaterial.Description =
            "Read one exact Material's editable PBR inputs, content revision, and reference impact before changing it";
        DescribeMaterial.Schema.Fields = {
            {"asset_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath}};
        DescribeMaterial.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FAssetPath Path;
            const std::string Text = FJson::parse(Call.ArgumentsJson)
                .at("asset_path").get<std::string>();
            if (!FAssetPath::TryParse(Text, Path) || !EngineLoop)
                return Failure(Call, "Material path is invalid");
            const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(Path);
            FMaterialData Material;
            EMaterialError Error = EMaterialError::None;
            if (Record == nullptr || Record->Type != EAssetType::Material
                || !LoadMaterialFromFile(Record->FilePath, Material, &Error))
                return Failure(Call, "Material could not be loaded: "
                    + std::string(ToString(Error)));
            const auto AssetRefs = FAssetDependencyService::FindAssetReferencers(
                Path, EngineLoop->GetAssetRegistry());
            const auto WorldRefs = FAssetDependencyService::FindWorldReferencers(
                EngineLoop->GetWorld(), Path);
            return Success(Call, {{"asset_path", Path.ToString()},
                {"revision", FileRevision(Record->FilePath)},
                {"values", MaterialToJson(Material)},
                {"asset_reference_count", AssetRefs.size()},
                {"world_reference_count", WorldRefs.size()},
                {"shared", AssetRefs.size() + WorldRefs.size() > 1}});
        };
        bInitialized = RegisterTool(std::move(DescribeMaterial)) && bInitialized;

        FAgentToolDefinition CreateMaterial;
        CreateMaterial.Name = "editor.material.create";
        CreateMaterial.Description =
            "Create one new Material at an unused explicit path. Never overwrites an existing asset and verifies the saved values";
        CreateMaterial.Permission = EAgentToolPermission::WriteProject;
        CreateMaterial.Schema.Fields = {
            {"asset_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"values", EAgentToolValueType::Object, true}};
        CreateMaterial.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath Path;
            if (!FAssetPath::TryParse(
                    Arguments.at("asset_path").get<std::string>(), Path)
                || Path.GetExtension() != ".pmat")
                return Failure(Call, "Material path must end in .pmat");
            FMaterialData Material;
            const FJson& Values = Arguments.at("values");
            if (Values.size() > 4)
                return Failure(Call, "Material values contain unsupported fields");
            for (auto It = Values.begin(); It != Values.end(); ++It)
            {
                std::string Error;
                if (!JsonToMaterialField(It.key(), It.value(), Material, Error))
                    return Failure(Call, Error);
            }
            if (!Material.BaseColorTexture.IsEmpty())
            {
                const FAssetRecord* Texture = EngineLoop
                    ? EngineLoop->GetAssetRegistry().Find(
                        Material.BaseColorTexture) : nullptr;
                if (Texture == nullptr || Texture->Type != EAssetType::Texture)
                    return Failure(Call,
                        "base_color_texture is not a registered Texture asset");
            }
            FEditorAssetService Service(EngineLoop);
            const FEditorAssetResult Created = Service.CreateMaterial(Path, Material);
            if (!Created.bSucceeded) return Failure(Call, Created.Message);
            const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(Path);
            FMaterialData ReadBack;
            EMaterialError Error = EMaterialError::None;
            if (Record == nullptr
                || !LoadMaterialFromFile(Record->FilePath, ReadBack, &Error)
                || MaterialToJson(ReadBack) != MaterialToJson(Material))
            {
                const std::filesystem::path CreatedFile = Record != nullptr
                    ? Record->FilePath
                    : FPaths::GetProjectContentDir()
                        / std::filesystem::path(
                            std::string(Path.GetGameRelativePath()));
                std::error_code RemoveError;
                std::filesystem::remove(CreatedFile, RemoveError);
                Service.RefreshRegistry();
                return Failure(Call, "Created Material failed read-back verification");
            }
            return Success(Call, {{"asset_path", Path.ToString()},
                {"operation_id", Call.Id}, {"before", nullptr},
                {"after", MaterialToJson(ReadBack)},
                {"changed_fields", Values.is_object()
                    ? FJson(Values) : FJson::object()},
                {"revision", FileRevision(Record->FilePath)}});
        };
        bInitialized = RegisterTool(std::move(CreateMaterial)) && bInitialized;

        FAgentToolDefinition DuplicateMaterial;
        DuplicateMaterial.Name = "editor.material.duplicate";
        DuplicateMaterial.Description =
            "Duplicate one exact Material to an unused explicit path after checking the source content revision";
        DuplicateMaterial.Permission = EAgentToolPermission::WriteProject;
        DuplicateMaterial.Schema.Fields = {
            {"source_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"destination_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"expected_revision", EAgentToolValueType::String, true, {}, {}, 32}};
        DuplicateMaterial.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath Source; FAssetPath Destination;
            if (!FAssetPath::TryParse(
                    Arguments.at("source_path").get<std::string>(), Source)
                || !FAssetPath::TryParse(
                    Arguments.at("destination_path").get<std::string>(), Destination)
                || Destination.GetExtension() != ".pmat" || !EngineLoop)
                return Failure(Call, "Material source or destination path is invalid");
            const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(Source);
            const std::string Revision = Record ? FileRevision(Record->FilePath) : "";
            if (Record == nullptr || Record->Type != EAssetType::Material)
                return Failure(Call, "Source Material was not found");
            if (Revision != Arguments.at("expected_revision").get<std::string>())
                return Failure(Call, "Source Material changed; describe it again before duplicating");
            const std::filesystem::path SourceFile = Record->FilePath;
            FMaterialData Material;
            EMaterialError Error = EMaterialError::None;
            if (!LoadMaterialFromFile(Record->FilePath, Material, &Error))
                return Failure(Call, "Source Material could not be loaded");
            FEditorAssetService Service(EngineLoop);
            const FEditorAssetResult Created = Service.CreateMaterial(
                Destination, Material);
            if (!Created.bSucceeded) return Failure(Call, Created.Message);
            const FAssetRecord* CreatedRecord =
                EngineLoop->GetAssetRegistry().Find(Destination);
            FMaterialData ReadBack;
            if (CreatedRecord == nullptr
                || !LoadMaterialFromFile(CreatedRecord->FilePath, ReadBack, &Error)
                || MaterialToJson(ReadBack) != MaterialToJson(Material))
            {
                std::error_code RemoveError;
                if (CreatedRecord != nullptr)
                    std::filesystem::remove(CreatedRecord->FilePath, RemoveError);
                Service.RefreshRegistry();
                return Failure(Call,
                    "Duplicated Material failed read-back verification and was removed");
            }
            const FAssetSemanticMetadataResult MetadataCopy =
                FAssetSemanticMetadataService::Copy(
                    SourceFile, CreatedRecord->FilePath);
            if (!MetadataCopy.bSucceeded)
            {
                std::error_code RemoveError;
                std::filesystem::remove(CreatedRecord->FilePath, RemoveError);
                std::filesystem::remove(
                    FAssetSemanticMetadataService::GetSidecarPath(
                        CreatedRecord->FilePath), RemoveError);
                Service.RefreshRegistry();
                return Failure(Call,
                    "Could not copy semantic metadata; duplicated Material was removed");
            }
            return Success(Call, {{"source_path", Source.ToString()},
                {"asset_path", Destination.ToString()}, {"operation_id", Call.Id},
                {"after", MaterialToJson(ReadBack)},
                {"revision", CreatedRecord
                    ? FileRevision(CreatedRecord->FilePath) : std::string {}}});
        };
        bInitialized = RegisterTool(std::move(DuplicateMaterial)) && bInitialized;

        FAgentToolDefinition UpdateMaterial;
        UpdateMaterial.Name = "editor.material.update";
        UpdateMaterial.Description =
            "Update only explicitly masked PBR fields on one exact Material after revision and shared-impact checks; saves atomically and verifies by reloading";
        UpdateMaterial.Permission = EAgentToolPermission::WriteProject;
        UpdateMaterial.Schema.Fields = {
            {"asset_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"expected_revision", EAgentToolValueType::String, true, {}, {}, 32},
            {"update_mask", EAgentToolValueType::Array, true},
            {"values", EAgentToolValueType::Object, true},
            {"allow_shared_update", EAgentToolValueType::Boolean, false}};
        UpdateMaterial.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath Path;
            if (!FAssetPath::TryParse(
                    Arguments.at("asset_path").get<std::string>(), Path) || !EngineLoop)
                return Failure(Call, "Material path is invalid");
            const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(Path);
            if (Record == nullptr || Record->Type != EAssetType::Material)
                return Failure(Call, "Material was not found");
            const std::string BeforeRevision = FileRevision(Record->FilePath);
            if (BeforeRevision != Arguments.at("expected_revision").get<std::string>())
                return Failure(Call, "Material changed; describe it again before updating");
            const auto AssetRefs = FAssetDependencyService::FindAssetReferencers(
                Path, EngineLoop->GetAssetRegistry());
            const auto WorldRefs = FAssetDependencyService::FindWorldReferencers(
                EngineLoop->GetWorld(), Path);
            if (AssetRefs.size() + WorldRefs.size() > 1
                && !Arguments.value("allow_shared_update", false))
                return Failure(Call,
                    "Material is shared by multiple references; duplicate it or explicitly allow a shared update");
            const FJson& Mask = Arguments.at("update_mask");
            const FJson& Values = Arguments.at("values");
            if (!Mask.is_array() || Mask.empty() || Mask.size() > 4
                || !Values.is_object() || Values.size() != Mask.size())
                return Failure(Call, "update_mask and values must name the same 1-4 fields");
            FMaterialData Before;
            EMaterialError MaterialError = EMaterialError::None;
            if (!LoadMaterialFromFile(Record->FilePath, Before, &MaterialError))
                return Failure(Call, "Material could not be loaded");
            FMaterialData After = Before;
            std::unordered_set<std::string> Seen;
            for (const FJson& Entry : Mask)
            {
                if (!Entry.is_string()) return Failure(Call, "update_mask entries must be strings");
                const std::string Field = Entry.get<std::string>();
                if (!Seen.insert(Field).second || !Values.contains(Field))
                    return Failure(Call, "update_mask contains duplicates or missing values");
                std::string Error;
                if (!JsonToMaterialField(Field, Values.at(Field), After, Error))
                    return Failure(Call, Error);
            }
            for (auto It = Values.begin(); It != Values.end(); ++It)
                if (!Seen.contains(It.key()))
                    return Failure(Call, "values contains a field outside update_mask");
            if (!After.BaseColorTexture.IsEmpty())
            {
                const FAssetRecord* Texture = EngineLoop->GetAssetRegistry().Find(
                    After.BaseColorTexture);
                if (Texture == nullptr || Texture->Type != EAssetType::Texture)
                    return Failure(Call,
                        "base_color_texture is not a registered Texture asset");
            }
            const std::filesystem::path File = Record->FilePath;
            FEditorAssetService Service(EngineLoop);
            const FEditorAssetResult Saved = Service.SaveMaterial(Path, After);
            if (!Saved.bSucceeded) return Failure(Call, Saved.Message);
            const FAssetRecord* SavedRecord = EngineLoop->GetAssetRegistry().Find(Path);
            FMaterialData ReadBack;
            if (SavedRecord == nullptr
                || !LoadMaterialFromFile(SavedRecord->FilePath, ReadBack, &MaterialError)
                || MaterialToJson(ReadBack) != MaterialToJson(After))
            {
                SaveMaterialToFile(File, Before);
                Service.RefreshRegistry();
                return Failure(Call,
                    "Material read-back verification failed; the previous file was restored");
            }
            FJson Changed = FJson::object();
            for (const std::string& Field : Seen)
                Changed[Field] = Values.at(Field);
            return Success(Call, {{"asset_path", Path.ToString()},
                {"operation_id", Call.Id}, {"before", MaterialToJson(Before)},
                {"after", MaterialToJson(ReadBack)},
                {"changed_fields", std::move(Changed)},
                {"revision_before", BeforeRevision},
                {"revision_after", FileRevision(SavedRecord->FilePath)}});
        };
        bInitialized = RegisterTool(std::move(UpdateMaterial)) && bInitialized;

        const auto ResolveGraphFile = [](std::string_view Text,
                                         FAssetPath& OutPath,
                                         std::filesystem::path& OutFile,
                                         std::string& OutError)
        {
            if (!FAssetPath::TryParse(Text, OutPath)
                || OutPath.GetExtension() != ".pgraph")
            {
                OutError = "Graph path must be a valid /Game/*.pgraph asset path";
                return false;
            }
            OutFile = FPaths::GetProjectContentDir()
                / std::filesystem::path(std::string(OutPath.GetGameRelativePath()));
            const std::filesystem::path Content = FPaths::GetProjectContentDir();
            const std::filesystem::path Relative = OutFile.lexically_relative(Content);
            if (Relative.empty()
                || (Relative.begin() != Relative.end() && *Relative.begin() == ".."))
            {
                OutError = "Graph path escapes project Content";
                return false;
            }
            return true;
        };
        const auto LoadGraph = [ResolveGraphFile](
            const FAgentToolCall& Call,
            FPicoGraphAsset& OutGraph,
            FAssetPath& OutPath,
            std::filesystem::path& OutFile,
            std::string& OutError)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            if (!ResolveGraphFile(Arguments.at("graph_path").get<std::string>(),
                    OutPath, OutFile, OutError)) return false;
            EGraphAssetError Error = EGraphAssetError::None;
            if (!LoadGraphAssetFromFile(OutFile, OutGraph, &Error))
            {
                OutError = "Could not load Graph: " + std::string(ToString(Error));
                return false;
            }
            return true;
        };
        const auto SaveGraph = [](const std::filesystem::path& File,
                                  const FPicoGraphAsset& Graph,
                                  std::string& OutError)
        {
            EGraphAssetError Error = EGraphAssetError::None;
            if (SaveGraphAssetToFile(File, Graph, &Error)) return true;
            OutError = "Could not save Graph: " + std::string(ToString(Error));
            return false;
        };

        FAgentToolDefinition CreateGraph;
        CreateGraph.Name = "editor.graph.create";
        CreateGraph.Description =
            "Create a new editable PicoGraph asset under /Game; never writes bytecode directly";
        CreateGraph.Permission = EAgentToolPermission::WriteProject;
        CreateGraph.Schema.Fields = {
            {"graph_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"entry_event", EAgentToolValueType::String, false, {}, {}, 64}
        };
        CreateGraph.Handler = [ResolveGraphFile](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath Path;
            std::filesystem::path File;
            std::string Error;
            if (!ResolveGraphFile(Arguments.at("graph_path").get<std::string>(),
                    Path, File, Error)) return Failure(Call, Error);
            if (std::filesystem::exists(File))
                return Failure(Call, "Graph asset already exists");
            FPicoGraphAsset Graph;
            Graph.GraphId = CreateGraphStableId();
            FGraphNode Entry;
            if (!MakeSchemaGraphNode("EntryEvent", 80.0f, 120.0f, Entry))
                return Failure(Call, "Entry Event Schema is unavailable");
            Entry.DisplayName = Arguments.value("entry_event", std::string("BeginPlay"));
            if (Entry.DisplayName.empty()) Entry.DisplayName = "BeginPlay";
            Graph.Nodes.push_back(std::move(Entry));
            EGraphAssetError AssetError = EGraphAssetError::None;
            if (!SaveGraphAssetToFile(File, Graph, &AssetError))
                return Failure(Call, "Could not create Graph: "
                    + std::string(ToString(AssetError)));
            FJson Pins = FJson::array();
            for (const FGraphPin& Pin : Graph.Nodes[0].Pins)
                Pins.push_back({{"id", Pin.Id}, {"name", Pin.Name},
                    {"direction", ToString(Pin.Direction)}, {"type", ToString(Pin.Type)}});
            return Success(Call, {{"graph_path", Path.ToString()},
                {"graph_id", Graph.GraphId}, {"entry_event", Graph.Nodes[0].DisplayName},
                {"entry_node_id", Graph.Nodes[0].Id}, {"entry_pins", std::move(Pins)}});
        };
        CreateGraph.Verifier = [LoadGraph](const FAgentToolCall& Call,
            const FAgentToolResult&, std::string& Error)
        {
            FPicoGraphAsset Graph; FAssetPath Path; std::filesystem::path File;
            return LoadGraph(Call, Graph, Path, File, Error);
        };
        bInitialized = RegisterTool(std::move(CreateGraph)) && bInitialized;

        FAgentToolDefinition DescribeGraph;
        DescribeGraph.Name = "editor.graph.describe";
        DescribeGraph.Description =
            "Read a PicoGraph's nodes, pins, links, variables, and stable identifiers before editing";
        DescribeGraph.Permission = EAgentToolPermission::ReadOnly;
        DescribeGraph.Schema.Fields = {
            {"graph_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath}
        };
        DescribeGraph.Handler = [LoadGraph](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FPicoGraphAsset Graph; FAssetPath Path; std::filesystem::path File;
            std::string Error;
            if (!LoadGraph(Call, Graph, Path, File, Error)) return Failure(Call, Error);
            FJson Nodes = FJson::array();
            for (const FGraphNode& Node : Graph.Nodes)
            {
                FJson Pins = FJson::array();
                for (const FGraphPin& Pin : Node.Pins)
                    Pins.push_back({{"id", Pin.Id}, {"name", Pin.Name},
                        {"direction", ToString(Pin.Direction)}, {"type", ToString(Pin.Type)},
                        {"default", Pin.DefaultValue}});
                Nodes.push_back({{"id", Node.Id}, {"type", Node.TypeName},
                    {"display_name", Node.DisplayName}, {"pins", std::move(Pins)}});
            }
            FJson Links = FJson::array();
            for (const FGraphLink& Link : Graph.Links)
                Links.push_back({{"id", Link.Id}, {"output_pin_id", Link.OutputPinId},
                    {"input_pin_id", Link.InputPinId}});
            FJson Variables = FJson::array();
            for (const FGraphVariable& Variable : Graph.Variables)
                Variables.push_back({{"id", Variable.Id}, {"name", Variable.Name},
                    {"type", ToString(Variable.Type)}, {"default", Variable.DefaultValue}});
            return Success(Call, {{"graph_path", Path.ToString()}, {"graph_id", Graph.GraphId},
                {"nodes", std::move(Nodes)}, {"links", std::move(Links)},
                {"variables", std::move(Variables)}});
        };
        bInitialized = RegisterTool(std::move(DescribeGraph)) && bInitialized;

        FAgentToolDefinition AddGraphNode;
        AddGraphNode.Name = "editor.graph.add_node";
        AddGraphNode.Description =
            "Add one node from the registered PicoGraph Schema to an existing Graph";
        AddGraphNode.Permission = EAgentToolPermission::WriteProject;
        AddGraphNode.Schema.Fields = {
            {"graph_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"node_type", EAgentToolValueType::String, true, {}, {}, 64},
            {"x", EAgentToolValueType::Number, true, -100000.0, 100000.0},
            {"y", EAgentToolValueType::Number, true, -100000.0, 100000.0}
        };
        AddGraphNode.Handler = [LoadGraph, SaveGraph](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FPicoGraphAsset Graph; FAssetPath Path; std::filesystem::path File;
            std::string Error;
            if (!LoadGraph(Call, Graph, Path, File, Error)) return Failure(Call, Error);
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FGraphNode Node;
            if (!MakeSchemaGraphNode(Arguments.at("node_type").get<std::string>(),
                    Arguments.at("x").get<float>(), Arguments.at("y").get<float>(), Node))
                return Failure(Call, "Unknown or unavailable Graph node type");
            const std::string NodeId = Node.Id;
            FJson Pins = FJson::array();
            for (const FGraphPin& Pin : Node.Pins)
                Pins.push_back({{"id", Pin.Id}, {"name", Pin.Name},
                    {"direction", ToString(Pin.Direction)}, {"type", ToString(Pin.Type)}});
            Graph.Nodes.push_back(std::move(Node));
            if (!SaveGraph(File, Graph, Error)) return Failure(Call, Error);
            return Success(Call, {{"graph_path", Path.ToString()},
                {"node_id", NodeId}, {"pins", std::move(Pins)}});
        };
        bInitialized = RegisterTool(std::move(AddGraphNode)) && bInitialized;

        FAgentToolDefinition ConnectGraphPins;
        ConnectGraphPins.Name = "editor.graph.connect_pins";
        ConnectGraphPins.Description = "Connect two compatible PicoGraph pins by stable ID";
        ConnectGraphPins.Permission = EAgentToolPermission::WriteProject;
        ConnectGraphPins.Schema.Fields = {
            {"graph_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"output_pin_id", EAgentToolValueType::String, true, {}, {}, 96},
            {"input_pin_id", EAgentToolValueType::String, true, {}, {}, 96}
        };
        ConnectGraphPins.Handler = [LoadGraph, SaveGraph](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FPicoGraphAsset Graph; FAssetPath Path; std::filesystem::path File;
            std::string Error;
            if (!LoadGraph(Call, Graph, Path, File, Error)) return Failure(Call, Error);
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            EGraphAssetError AssetError = EGraphAssetError::None;
            if (!AddGraphLink(Graph,
                    Arguments.at("output_pin_id").get<std::string>(),
                    Arguments.at("input_pin_id").get<std::string>(), &AssetError))
                return Failure(Call, "Could not connect Graph pins: "
                    + std::string(ToString(AssetError)));
            if (!SaveGraph(File, Graph, Error)) return Failure(Call, Error);
            return Success(Call, {{"graph_path", Path.ToString()},
                {"link_count", Graph.Links.size()}});
        };
        bInitialized = RegisterTool(std::move(ConnectGraphPins)) && bInitialized;

        FAgentToolDefinition SetGraphDefault;
        SetGraphDefault.Name = "editor.graph.set_default";
        SetGraphDefault.Description =
            "Set a non-Exec PicoGraph pin default by stable ID; compilation performs semantic validation";
        SetGraphDefault.Permission = EAgentToolPermission::WriteProject;
        SetGraphDefault.Schema.Fields = {
            {"graph_path", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"pin_id", EAgentToolValueType::String, true, {}, {}, 96},
            {"value", EAgentToolValueType::String, true, {}, {}, 1024}
        };
        SetGraphDefault.Handler = [LoadGraph, SaveGraph](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            FPicoGraphAsset Graph; FAssetPath Path; std::filesystem::path File;
            std::string Error;
            if (!LoadGraph(Call, Graph, Path, File, Error)) return Failure(Call, Error);
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FGraphPin* Pin = FindGraphPin(Graph, Arguments.at("pin_id").get<std::string>());
            if (Pin == nullptr || Pin->Type == EGraphValueType::Exec)
                return Failure(Call, "Graph pin is missing or cannot have a default value");
            Pin->DefaultValue = Arguments.at("value").get<std::string>();
            if (!SaveGraph(File, Graph, Error)) return Failure(Call, Error);
            return Success(Call, {{"graph_path", Path.ToString()},
                {"pin_id", Pin->Id}, {"value", Pin->DefaultValue}});
        };
        bInitialized = RegisterTool(std::move(SetGraphDefault)) && bInitialized;

        const auto MakeGraphAnalysisTool = [LoadGraph](bool bCompile)
        {
            FAgentToolDefinition Tool;
            Tool.Name = bCompile ? "editor.graph.compile" : "editor.graph.validate";
            Tool.Description = bCompile
                ? "Compile a PicoGraph through validated Typed IR into transient bytecode; does not write bytecode"
                : "Validate PicoGraph structure and semantics without modifying project files";
            Tool.Permission = EAgentToolPermission::ReadOnly;
            Tool.Schema.Fields = {{"graph_path", EAgentToolValueType::String, true,
                {}, {}, 512, EAgentToolStringFormat::AssetPath}};
            Tool.Handler = [LoadGraph, bCompile](
                const FAgentToolCall& Call, const FCancellationToken*)
            {
                FPicoGraphAsset Graph; FAssetPath Path; std::filesystem::path File;
                std::string Error;
                if (!LoadGraph(Call, Graph, Path, File, Error)) return Failure(Call, Error);
                const FGraphCompileResult Result = CompileGraph(Graph);
                FJson Diagnostics = FJson::array();
                bool bHasErrors = false;
                for (const FGraphDiagnostic& Diagnostic : Result.Diagnostics)
                {
                    bHasErrors = bHasErrors
                        || Diagnostic.Severity == EGraphDiagnosticSeverity::Error;
                    Diagnostics.push_back({{"severity", ToString(Diagnostic.Severity)},
                        {"code", ToString(Diagnostic.Code)}, {"message", Diagnostic.Message},
                        {"node_id", Diagnostic.NodeId}, {"pin_id", Diagnostic.PinId}});
                }
                FJson Output{{"graph_path", Path.ToString()},
                    {"valid", !bHasErrors}, {"diagnostics", std::move(Diagnostics)}};
                if (bCompile)
                {
                    Output["compiled"] = Result.bSucceeded;
                    Output["bytecode_version"] = Result.Bytecode.Version;
                    Output["bytecode_bytes"] = Result.Bytecode.Bytes.size();
                    Output["instruction_count"] = Result.IR.Instructions.size();
                }
                return Success(Call, std::move(Output));
            };
            return Tool;
        };
        bInitialized = RegisterTool(MakeGraphAnalysisTool(false)) && bInitialized;
        bInitialized = RegisterTool(MakeGraphAnalysisTool(true)) && bInitialized;

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
            FJson Description = DescribeObject(Object, true);
            if (Object->IsA(PActor::StaticClass())
                && Description.contains("components"))
            {
                PActor* Actor = static_cast<PActor*>(Object);
                std::size_t ComponentIndex = 0;
                for (PActorComponent* Component : Actor->GetComponents())
                {
                    if (Component == nullptr) continue;
                    Description["components"][ComponentIndex]["revision"] =
                        ObjectRevision(Component);
                    ++ComponentIndex;
                }
            }
            Description["revision"] = ObjectRevision(Object);
            return Success(Call, std::move(Description));
        };
        bInitialized = RegisterTool(std::move(DescribeObjectTool)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(GetProperty)) && bInitialized;

        FAgentToolDefinition SetProperties;
        SetProperties.Name = "editor.object.set_properties";
        SetProperties.Description =
            "Set up to 32 reflected Editable properties on one World object in one approved Undo transaction. Use editor.object.describe first and preserve unmodified fields of compound values";
        SetProperties.Permission = EAgentToolPermission::ModifyWorld;
        SetProperties.Schema.Fields = {
            {"object_path", EAgentToolValueType::String, true, {}, {}, 512},
            {"properties", EAgentToolValueType::Object, true},
            {"expected_revision", EAgentToolValueType::String, false, {}, {}, 32}
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
            const std::string RevisionBefore = ObjectRevision(Object);
            if (Arguments.contains("expected_revision")
                && Arguments.at("expected_revision").get<std::string>()
                    != RevisionBefore)
                return Failure(Call,
                    "Object changed; describe it again before setting properties");
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

            FJson Before = FJson::object();
            for (const FPendingValue& Entry : Pending)
            {
                FJson Value;
                if (!PropertyValueToJson(*Entry.Property, Object, Value))
                    return Failure(Call, "Could not capture property before-state");
                Before[Entry.Property->GetName().ToString()] = std::move(Value);
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
                {"properties", Applied}, {"before", std::move(Before)},
                {"after", Applied}, {"changed_fields", PropertyValues},
                {"operation_id", Call.Id}, {"revision_before", RevisionBefore},
                {"revision_after", ObjectRevision(Object)}});
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
        bInitialized = RegisterTool(std::move(SetProperties)) && bInitialized;

        FAgentToolDefinition DescribeBlueprintDefaults;
        DescribeBlueprintDefaults.Name = "editor.actor_blueprint.describe_defaults";
        DescribeBlueprintDefaults.Description =
            "Describe the reflected class defaults that future instances of one Actor Blueprint will receive; use this instead of a placed World instance for GameMode-spawned Pawns";
        DescribeBlueprintDefaults.Schema.Fields = {
            {"blueprint_asset", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath}};
        DescribeBlueprintDefaults.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath BlueprintPath;
            if (!FAssetPath::TryParse(
                    Arguments.at("blueprint_asset").get<std::string>(), BlueprintPath)
                || BlueprintPath.GetExtension() != ".pblueprint")
                return Failure(Call, "Actor Blueprint asset path is invalid");
            const FAssetRecord* Record = EngineLoop
                ? EngineLoop->GetAssetRegistry().Find(BlueprintPath) : nullptr;
            const PClass* GeneratedClass = FindActorBlueprintGeneratedClass(BlueprintPath);
            const PObject* Defaults = GeneratedClass != nullptr
                ? GeneratedClass->GetDefaultObject() : nullptr;
            if (Record == nullptr || Record->Type != EAssetType::ActorBlueprint
                || Defaults == nullptr)
                return Failure(Call, "Actor Blueprint is not compiled and registered");
            return Success(Call, {{"blueprint_asset", BlueprintPath.ToString()},
                {"generated_class", GeneratedClass->GetName().ToString()},
                {"defaults", DescribeObject(const_cast<PObject*>(Defaults), false)}});
        };
        bInitialized = RegisterTool(std::move(DescribeBlueprintDefaults))
            && bInitialized;

        FAgentToolDefinition SetBlueprintDefaults;
        SetBlueprintDefaults.Name = "editor.actor_blueprint.set_defaults";
        SetBlueprintDefaults.Description =
            "Persist up to 32 reflected Serializable defaults on an Actor Blueprint generated class so future GameMode-spawned and placed instances inherit them; never use runtime Replicated or Transient mirror properties";
        SetBlueprintDefaults.Permission = EAgentToolPermission::WriteProject;
        SetBlueprintDefaults.Schema.Fields = {
            {"blueprint_asset", EAgentToolValueType::String, true, {}, {}, 512,
                EAgentToolStringFormat::AssetPath},
            {"properties", EAgentToolValueType::Object, true}};
        SetBlueprintDefaults.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            FAssetPath BlueprintPath;
            if (!FAssetPath::TryParse(
                    Arguments.at("blueprint_asset").get<std::string>(), BlueprintPath)
                || BlueprintPath.GetExtension() != ".pblueprint")
                return Failure(Call, "Actor Blueprint asset path is invalid");
            const FAssetRecord* Record = EngineLoop
                ? EngineLoop->GetAssetRegistry().Find(BlueprintPath) : nullptr;
            const PClass* GeneratedClass = FindActorBlueprintGeneratedClass(BlueprintPath);
            const FJson& PropertyValues = Arguments.at("properties");
            if (Record == nullptr || Record->Type != EAssetType::ActorBlueprint
                || GeneratedClass == nullptr || !GeneratedClass->IsChildOf(PActor::StaticClass())
                || PropertyValues.empty() || PropertyValues.size() > 32)
                return Failure(Call, "Actor Blueprint or properties are invalid");

            struct FPendingValue
            {
                const PProperty* Property = nullptr;
                FEditorPropertyValue Value;
            };
            std::vector<FPendingValue> Pending;
            Pending.reserve(PropertyValues.size());
            for (auto It = PropertyValues.begin(); It != PropertyValues.end(); ++It)
            {
                const PProperty* Property = GeneratedClass->FindProperty(FName(It.key()));
                if (Property == nullptr
                    || !Property->HasAnyFlags(EPropertyFlags::Editable)
                    || !Property->HasAnyFlags(EPropertyFlags::Serializable)
                    || Property->HasAnyFlags(
                        EPropertyFlags::ReadOnly | EPropertyFlags::Transient))
                    return Failure(Call,
                        "Blueprint default is not persistently editable: " + It.key());
                FEditorPropertyValue Value;
                std::string Error;
                if (!JsonToPropertyValue(*Property, It.value(), Value, Error))
                    return Failure(Call, It.key() + ": " + Error);
                Pending.push_back({Property, std::move(Value)});
            }

            PWorld* PreviewWorld = NewObject<PWorld>(
                nullptr, "AgentBlueprintDefaultsWorld", EObjectFlags::Transient);
            const auto Cleanup = [&PreviewWorld]()
            {
                if (PreviewWorld == nullptr) return;
                RemoveFromRoot(PreviewWorld);
                DestroyObjectTree(PreviewWorld);
                PreviewWorld = nullptr;
            };
            if (PreviewWorld == nullptr || !AddToRoot(PreviewWorld)
                || !PreviewWorld->Initialize())
            {
                Cleanup();
                return Failure(Call, "Could not initialize Blueprint defaults workspace");
            }
            PreviewWorld->SetAssetServices(
                &EngineLoop->GetAssetRegistry(), &EngineLoop->GetAssetManager());
            FActorSpawnParameters Spawn;
            Spawn.Name = FName("AgentBlueprintDefaultsActor");
            Spawn.ObjectFlags = EObjectFlags::Transient;
            PActor* PreviewActor = PreviewWorld->SpawnActor(GeneratedClass, Spawn);
            if (PreviewActor == nullptr)
            {
                Cleanup();
                return Failure(Call, "Could not construct Actor Blueprint defaults");
            }
            for (FPendingValue& Entry : Pending)
            {
                const FEditorPropertyResult Applied = ApplyEditorPropertyValue(
                    EngineLoop, PreviewActor, Entry.Property, Entry.Value);
                if (!Applied.bSucceeded)
                {
                    Cleanup();
                    return Failure(Call, Applied.Message);
                }
            }
            EActorBlueprintError BlueprintError = EActorBlueprintError::None;
            if (!SaveActorBlueprintDefaults(
                    Record->FilePath, BlueprintPath, PreviewActor, &BlueprintError))
            {
                Cleanup();
                return Failure(Call, "Could not save Actor Blueprint defaults: "
                    + std::string(ToString(BlueprintError)));
            }
            Cleanup();

            const PObject* SavedDefaults = GeneratedClass->GetDefaultObject();
            FJson Saved = FJson::object();
            for (const FPendingValue& Entry : Pending)
            {
                FJson Value;
                if (SavedDefaults == nullptr
                    || !PropertyValueToJson(*Entry.Property, SavedDefaults, Value))
                    return Failure(Call, "Could not read back saved Blueprint default");
                Saved[Entry.Property->GetName().ToString()] = std::move(Value);
            }
            return Success(Call, {{"blueprint_asset", BlueprintPath.ToString()},
                {"generated_class", GeneratedClass->GetName().ToString()},
                {"properties", std::move(Saved)}});
        };
        SetBlueprintDefaults.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Output = FJson::parse(Result.OutputJson);
            FAssetPath BlueprintPath;
            if (!FAssetPath::TryParse(
                    Output.at("blueprint_asset").get<std::string>(), BlueprintPath))
            {
                Error = "Saved Blueprint path is invalid";
                return false;
            }
            const PClass* GeneratedClass = FindActorBlueprintGeneratedClass(BlueprintPath);
            const PObject* Defaults = GeneratedClass != nullptr
                ? GeneratedClass->GetDefaultObject() : nullptr;
            for (auto It = Output.at("properties").begin();
                Defaults != nullptr && It != Output.at("properties").end(); ++It)
            {
                const PProperty* Property = GeneratedClass->FindProperty(FName(It.key()));
                FJson Actual;
                if (Property == nullptr
                    || !PropertyValueToJson(*Property, Defaults, Actual)
                    || !JsonEquivalent(Actual, It.value()))
                {
                    Error = "Blueprint default postcondition failed: " + It.key();
                    return false;
                }
            }
            if (Defaults == nullptr)
            {
                Error = "Blueprint generated defaults disappeared";
                return false;
            }
            return true;
        };
        bInitialized = RegisterTool(std::move(SetBlueprintDefaults)) && bInitialized;

        FAgentToolDefinition BatchSetProperties;
        BatchSetProperties.Name = "editor.object.batch_set_properties";
        BatchSetProperties.Description =
            "Set reflected Editable properties on up to 32 explicit World objects in one approved all-or-nothing Undo transaction. Each expected_revision must come from the description entry with the same object_path";
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
                if (!Edit.is_object() || (Edit.size() != 2 && Edit.size() != 3)
                    || !Edit.contains("object_path") || !Edit.at("object_path").is_string()
                    || !Edit.contains("properties") || !Edit.at("properties").is_object())
                    return Failure(Call,
                        "Each edit requires object_path, properties, and optional expected_revision only");
                const std::string Path = Edit.at("object_path").get<std::string>();
                if (Path.empty() || Path.size() > 512 || !SeenPaths.insert(Path).second)
                    return Failure(Call, "Edit object paths must be unique and valid");
                PObject* Object = FindEditorWorldObjectByPath(
                    EngineLoop ? EngineLoop->GetWorld() : nullptr, Path);
                const FJson& Values = Edit.at("properties");
                if (!Object || Values.empty() || Values.size() > 32
                    || TotalProperties + Values.size() > 128)
                    return Failure(Call, "Batch property target or property count is invalid");
                if (Edit.contains("expected_revision")
                    && (!Edit.at("expected_revision").is_string()
                        || Edit.at("expected_revision").get<std::string>()
                            != ObjectRevision(Object)))
                    return Failure(Call, Path
                        + ": object changed; describe it again before batch editing");
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
        bInitialized = RegisterTool(std::move(BatchSetProperties)) && bInitialized;

        FAgentToolDefinition ListComponentTypes;
        ListComponentTypes.Name = "editor.component.list_types";
        ListComponentTypes.Description =
            "Discover constructible Actor Component classes from the live reflection registry, including editable defaults; does not use a hard-coded component list";
        ListComponentTypes.Handler = [](const FAgentToolCall& Call,
            const FCancellationToken*)
        {
            FJson Types = FJson::array();
            for (const PClass* Class : FClassRegistry::GetClasses())
            {
                if (Class == nullptr || !Class->CanConstruct()
                    || !Class->IsChildOf(PActorComponent::StaticClass()))
                    continue;
                FJson Properties = FJson::object();
                const PObject* Defaults = Class->GetDefaultObject();
                std::vector<const PProperty*> Reflected;
                GatherProperties(Class, Reflected);
                for (const PProperty* Property : Reflected)
                    if (Property != nullptr && Defaults != nullptr
                        && Property->HasAnyFlags(EPropertyFlags::Editable))
                        Properties[Property->GetName().ToString()] =
                            DescribeProperty(*Property, Defaults);
                Types.push_back({{"class", Class->GetName().ToString()},
                    {"scene_component", Class->IsChildOf(
                        PSceneComponent::StaticClass())},
                    {"editable_defaults", std::move(Properties)}});
            }
            std::sort(Types.begin(), Types.end(),
                [](const FJson& Left, const FJson& Right)
                {
                    return Left.at("class").get<std::string>()
                        < Right.at("class").get<std::string>();
                });
            return Success(Call, {{"component_types", std::move(Types)}});
        };
        bInitialized = RegisterTool(std::move(ListComponentTypes)) && bInitialized;

        FAgentToolDefinition AddComponent;
        AddComponent.Name = "editor.component.add";
        AddComponent.Description =
            "Add one reflected constructible component to one exact Actor after checking its revision; scene components attach to an explicit parent or the current root";
        AddComponent.Permission = EAgentToolPermission::ModifyWorld;
        AddComponent.Schema.Fields = {
            {"actor_path", EAgentToolValueType::String, true, {}, {}, 512},
            {"expected_revision", EAgentToolValueType::String, true, {}, {}, 32},
            {"component_class", EAgentToolValueType::String, true, {}, {}, 128},
            {"component_name", EAgentToolValueType::String, true, {}, {}, 64},
            {"parent_component_path", EAgentToolValueType::String, false, {}, {}, 512},
            {"socket_name", EAgentToolValueType::String, false, {}, {}, 64}};
        AddComponent.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Arguments.at("actor_path").get<std::string>());
            PActor* Actor = Object && Object->IsA(PActor::StaticClass())
                ? static_cast<PActor*>(Object) : nullptr;
            if (Actor == nullptr) return Failure(Call, "Actor path was not found");
            const std::string RevisionBefore = ObjectRevision(Actor);
            if (RevisionBefore
                != Arguments.at("expected_revision").get<std::string>())
                return Failure(Call, "Actor changed; describe it again before adding a component");
            const std::string Name = Arguments.at("component_name").get<std::string>();
            const PClass* ComponentClass = FClassRegistry::FindClass(
                FName(Arguments.at("component_class").get<std::string>()));
            if (!IsSafeObjectName(Name) || ComponentClass == nullptr
                || !ComponentClass->CanConstruct()
                || !ComponentClass->IsChildOf(PActorComponent::StaticClass()))
                return Failure(Call, "Component name or reflected class is invalid");
            for (PActorComponent* Existing : Actor->GetComponents())
                if (Existing != nullptr && Existing->GetName().ToString() == Name)
                    return Failure(Call, "Actor already has a component with that name");

            PSceneComponent* Parent = nullptr;
            const std::string ParentPath =
                Arguments.value("parent_component_path", "");
            if (!ParentPath.empty())
            {
                PObject* ParentObject = FindEditorWorldObjectByPath(
                    EngineLoop->GetWorld(), ParentPath);
                Parent = ParentObject && ParentObject->IsA(PSceneComponent::StaticClass())
                    ? static_cast<PSceneComponent*>(ParentObject) : nullptr;
                if (Parent == nullptr || Parent->GetOwner() != Actor)
                    return Failure(Call,
                        "Parent component must be an explicit scene component owned by the Actor");
            }

            const FJson Before = DescribeObject(Actor, true);
            PActorComponent* Component = Actor->CreateComponent(ComponentClass, Name);
            if (Component == nullptr) return Failure(Call, "Could not create component");
            if (Component->IsA(PSceneComponent::StaticClass()))
            {
                auto* Scene = static_cast<PSceneComponent*>(Component);
                if (Actor->GetRootComponent() == nullptr)
                {
                    if (!Actor->SetRootComponent(Scene))
                        return Failure(Call, "Could not set the new scene component as root");
                }
                else
                {
                    if (Parent == nullptr) Parent = Actor->GetRootComponent();
                    const std::string Socket = Arguments.value("socket_name", "");
                    if (!Scene->AttachToComponent(Parent,
                            EAttachmentTransformRule::KeepRelative, FName(Socket)))
                        return Failure(Call, "Could not attach the new scene component");
                }
            }
            if (Selection) Selection->Set(Component);
            return Success(Call, {{"actor_path", Actor->GetPathName()},
                {"component_path", Component->GetPathName()},
                {"component_class", ComponentClass->GetName().ToString()},
                {"operation_id", Call.Id}, {"before", Before},
                {"after", DescribeObject(Actor, true)},
                {"changed_fields", FJson::array({"components"})},
                {"revision_before", RevisionBefore},
                {"revision_after", ObjectRevision(Actor)}});
        };
        AddComponent.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Output = FJson::parse(Result.OutputJson);
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr,
                Output.at("component_path").get<std::string>());
            if (Object == nullptr || !Object->IsA(PActorComponent::StaticClass())
                || Object->GetClass()->GetName().ToString()
                    != Output.at("component_class").get<std::string>())
            {
                Error = "Added component failed read-back verification";
                return false;
            }
            return true;
        };
        bInitialized = RegisterTool(std::move(AddComponent)) && bInitialized;

        FAgentToolDefinition RemoveComponent;
        RemoveComponent.Name = "editor.component.remove";
        RemoveComponent.Description =
            "Remove one exact non-root component after checking its owner revision; attached descendants require explicit subtree consent and the whole change is Undoable";
        RemoveComponent.Permission = EAgentToolPermission::ModifyWorld;
        RemoveComponent.Schema.Fields = {
            {"component_path", EAgentToolValueType::String, true, {}, {}, 512},
            {"expected_actor_revision", EAgentToolValueType::String, true, {}, {}, 32},
            {"allow_remove_subtree", EAgentToolValueType::Boolean, false}};
        RemoveComponent.Handler = [this](
            const FAgentToolCall& Call, const FCancellationToken*)
        {
            const FJson Arguments = FJson::parse(Call.ArgumentsJson);
            const std::string ComponentPath =
                Arguments.at("component_path").get<std::string>();
            PObject* Object = FindEditorWorldObjectByPath(
                EngineLoop ? EngineLoop->GetWorld() : nullptr, ComponentPath);
            PActorComponent* Component =
                Object && Object->IsA(PActorComponent::StaticClass())
                    ? static_cast<PActorComponent*>(Object) : nullptr;
            PActor* Actor = Component ? Component->GetOwner() : nullptr;
            if (Actor == nullptr) return Failure(Call, "Component path was not found");
            const std::string RevisionBefore = ObjectRevision(Actor);
            if (RevisionBefore
                != Arguments.at("expected_actor_revision").get<std::string>())
                return Failure(Call, "Actor changed; describe it again before removing a component");
            if (Component == Actor->GetRootComponent())
                return Failure(Call,
                    "Removing the root component is not allowed; replace or reorganize the Actor first");
            FJson Removed = FJson::array({ComponentPath});
            if (Component->IsA(PSceneComponent::StaticClass()))
            {
                const auto Children = static_cast<PSceneComponent*>(Component)
                    ->GetAttachChildren();
                if (!Children.empty()
                    && !Arguments.value("allow_remove_subtree", false))
                    return Failure(Call,
                        "Component has attached descendants; inspect impact and explicitly allow subtree removal");
                std::function<void(PSceneComponent*)> Gather =
                    [&Removed, &Gather](PSceneComponent* Parent)
                    {
                        for (PSceneComponent* Child : Parent->GetAttachChildren())
                        {
                            Removed.push_back(Child->GetPathName());
                            Gather(Child);
                        }
                    };
                Gather(static_cast<PSceneComponent*>(Component));
            }
            const FJson Before = DescribeObject(Actor, true);
            const std::string ActorPath = Actor->GetPathName();
            if (!Actor->DestroyComponent(Component))
                return Failure(Call,
                    "Component cannot be removed, usually because it is an inherited default component");
            if (Selection) Selection->Set(Actor);
            return Success(Call, {{"actor_path", ActorPath},
                {"removed_component_paths", std::move(Removed)},
                {"operation_id", Call.Id}, {"before", Before},
                {"after", DescribeObject(Actor, true)},
                {"changed_fields", FJson::array({"components"})},
                {"revision_before", RevisionBefore},
                {"revision_after", ObjectRevision(Actor)}});
        };
        RemoveComponent.Verifier = [this](const FAgentToolCall&,
            const FAgentToolResult& Result, std::string& Error)
        {
            const FJson Output = FJson::parse(Result.OutputJson);
            for (const FJson& Path : Output.at("removed_component_paths"))
            {
                if (FindEditorWorldObjectByPath(
                        EngineLoop ? EngineLoop->GetWorld() : nullptr,
                        Path.get<std::string>()) != nullptr)
                {
                    Error = "Removed component still exists after the operation";
                    return false;
                }
            }
            return true;
        };
        bInitialized = RegisterTool(std::move(RemoveComponent)) && bInitialized;

        FAgentToolDefinition SpawnActor;
        SpawnActor.Name = "editor.actor.spawn";
        SpawnActor.Description =
            "Create an Empty or Cube Actor in the active World. Empty creates only a "
            "PSceneComponent root: it is not a light, camera, mesh, or other requested "
            "component. Use editor.component.add and then set and read back exact "
            "component properties to assemble those Actor types";
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
                {"root_component_path", Root->GetPathName()},
                {"assembly_state", Kind == "Empty"
                    ? "scene_root_only" : "cube_geometry_created"},
                {"created_component_class", Root->GetClass()->GetName().ToString()},
                {"requires_component_add_for_specialized_actor", Kind == "Empty"}});
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
        bInitialized = RegisterTool(std::move(SpawnActor)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(SpawnBlueprint)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(DeleteActor)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(DeleteActors)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(CreateRoom)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(CreateThirdPerson)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(SetLocation)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(ValidateGameplay)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(StartPlay)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(StopPlay)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(SaveWorld)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(CreateProject)) && bInitialized;

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
        bInitialized = RegisterTool(std::move(PackageProject)) && bInitialized;
    }

    FEngineLoop* EngineLoop = nullptr;
    FEditorSelection* Selection = nullptr;
    FEditorAgentHostServices HostServices;
    FTransaction Transaction;
    FWorldToolProvider WorldTools;
    FObjectToolProvider ObjectTools;
    FAssetToolProvider AssetTools;
    FBlueprintGraphToolProvider BlueprintGraphTools;
    FGameplayToolProvider GameplayTools;
    FProjectProcessToolProvider ProjectProcessTools;
    FAgentToolRegistry Registry;
    mutable std::unordered_map<std::string, FCachedTypedSummary> TypedSummaryCache;
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
std::vector<std::string> FEditorAgentToolExecutor::GetRevisionReadSet(
    const FAgentToolCall& Call) const
{
    return Impl ? Impl->Registry.GetRevisionReadSet(Call)
        : std::vector<std::string>{"State.Revision"};
}
std::vector<std::string> FEditorAgentToolExecutor::GetRevisionWriteSet(
    const FAgentToolCall& Call) const
{
    return Impl ? Impl->Registry.GetRevisionWriteSet(Call)
        : std::vector<std::string>{"State.Revision"};
}
void FEditorAgentToolExecutor::PrepareApproval(const FAgentToolCall& Call)
{
    if (Impl) Impl->Registry.PrepareApproval(Call);
}

bool FEditorAgentToolExecutor::PrepareApprovalDecision(
    const FAgentToolCall& Call, bool bApproved)
{
    return Impl && Impl->Registry.PrepareApprovalDecision(Call, bApproved);
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
