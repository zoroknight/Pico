#include "Pico/AssetImport/SkeletalAnimationImporter.h"
#include "Pico/AssetImport/TextureImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Pico
{
namespace
{
void Report(ESkeletalImportError* OutError, ESkeletalImportError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

FVector3 ConvertVector(const aiVector3D& Value, const FSkeletalImportOptions& Options,
    bool bScaleTranslation = false)
{
    const float Scale = bScaleTranslation ? Options.SourceUnitToCentimeters : 1.0f;
    return Options.bConvertYUpToZUp
        ? FVector3(Value.z, Value.x, Value.y) * Scale
        : FVector3(Value.x, Value.y, Value.z) * Scale;
}

FQuat ConvertQuat(const aiQuaternion& Value, const FSkeletalImportOptions& Options)
{
    return Options.bConvertYUpToZUp
        ? FQuat(Value.z, Value.x, Value.y, Value.w).GetNormalized()
        : FQuat(Value.x, Value.y, Value.z, Value.w).GetNormalized();
}

FTransform ConvertTransform(const aiMatrix4x4& Matrix, const FSkeletalImportOptions& Options)
{
    aiVector3D Scale;
    aiVector3D Position;
    aiQuaternion Rotation;
    Matrix.Decompose(Scale, Rotation, Position);
    return FTransform(
        ConvertQuat(Rotation, Options),
        ConvertVector(Position, Options, true),
        ConvertVector(Scale, Options));
}

FMatrix4 ConvertMatrix(const aiMatrix4x4& Matrix, const FSkeletalImportOptions& Options)
{
    const float Source[4][4] = {
        {Matrix.a1, Matrix.a2, Matrix.a3, Matrix.a4},
        {Matrix.b1, Matrix.b2, Matrix.b3, Matrix.b4},
        {Matrix.c1, Matrix.c2, Matrix.c3, Matrix.c4},
        {Matrix.d1, Matrix.d2, Matrix.d3, Matrix.d4}
    };
    FMatrix4 Result = FMatrix4::Identity;
    const int Axis[3] = {2, 0, 1};
    for (int Row = 0; Row < 3; ++Row)
    {
        const int SourceRow = Options.bConvertYUpToZUp ? Axis[Row] : Row;
        for (int Column = 0; Column < 3; ++Column)
        {
            const int SourceColumn = Options.bConvertYUpToZUp ? Axis[Column] : Column;
            Result.M[Row][Column] = Source[SourceRow][SourceColumn];
        }
        Result.M[Row][3] = Source[SourceRow][3] * Options.SourceUnitToCentimeters;
    }
    return Result;
}

void CollectRequiredNodes(
    const aiScene& Scene,
    std::unordered_set<const aiNode*>& OutRequiredNodes,
    std::unordered_map<std::string, const aiNode*>& OutNodesByName)
{
    const auto IndexNodes = [&](const auto& Self, const aiNode* Node) -> void
    {
        if (Node == nullptr) return;
        OutNodesByName.emplace(Node->mName.C_Str(), Node);
        for (unsigned Index = 0; Index < Node->mNumChildren; ++Index)
            Self(Self, Node->mChildren[Index]);
    };
    IndexNodes(IndexNodes, Scene.mRootNode);
    for (unsigned MeshIndex = 0; MeshIndex < Scene.mNumMeshes; ++MeshIndex)
    {
        const aiMesh* Mesh = Scene.mMeshes[MeshIndex];
        for (unsigned BoneIndex = 0; BoneIndex < Mesh->mNumBones; ++BoneIndex)
        {
            const aiBone* Bone = Mesh->mBones[BoneIndex];
            const std::string Name = Bone->mName.C_Str();
            const auto Found = OutNodesByName.find(Name);
            const aiNode* Node = Bone->mNode != nullptr
                ? Bone->mNode
                : (Found != OutNodesByName.end() ? Found->second : nullptr);
            while (Node != nullptr)
            {
                OutRequiredNodes.insert(Node);
                Node = Node->mParent;
            }
        }
    }
}

void BuildSkeletonRecursive(
    const aiNode* Node,
    int32 ParentIndex,
    const std::unordered_set<const aiNode*>& Required,
    const FSkeletalImportOptions& Options,
    FSkeletonData& OutSkeleton,
    std::unordered_map<std::string, uint32>& OutBoneMap)
{
    if (Node == nullptr) return;
    const std::string Name = Node->mName.C_Str();
    int32 CurrentParent = ParentIndex;
    if (Required.contains(Node))
    {
        FSkeletonBone Bone;
        Bone.Name = Name;
        Bone.ParentIndex = ParentIndex;
        Bone.ReferenceLocalPose = ConvertTransform(Node->mTransformation, Options);
        CurrentParent = static_cast<int32>(OutSkeleton.Bones.size());
        OutBoneMap.emplace(Name, static_cast<uint32>(CurrentParent));
        OutSkeleton.Bones.push_back(std::move(Bone));
    }
    for (unsigned Index = 0; Index < Node->mNumChildren; ++Index)
        BuildSkeletonRecursive(Node->mChildren[Index], CurrentParent, Required, Options,
            OutSkeleton, OutBoneMap);
}

std::string SanitizeFileName(std::string Name, std::size_t Index)
{
    if (Name.empty()) Name = "Animation_" + std::to_string(Index);
    for (char& Character : Name)
        if (!std::isalnum(static_cast<unsigned char>(Character)) && Character != '_' && Character != '-')
            Character = '_';
    return Name;
}

int32 ImportMaterialTexture(
    const aiScene& Scene,
    const aiString& TexturePath,
    const std::filesystem::path& SourceFile,
    std::string Name,
    FSkeletalImportResult& Result)
{
    FTextureData Texture;
    bool bImported = false;
    if (const aiTexture* Embedded = Scene.GetEmbeddedTexture(TexturePath.C_Str()))
    {
        if (Embedded->mHeight == 0)
        {
            bImported = ImportTextureMemory(
                std::span<const uint8>(
                    reinterpret_cast<const uint8*>(Embedded->pcData), Embedded->mWidth),
                Texture);
        }
        else
        {
            Texture.Width = Embedded->mWidth;
            Texture.Height = Embedded->mHeight;
            Texture.Pixels.reserve(static_cast<std::size_t>(Texture.Width) * Texture.Height * 4);
            for (std::size_t Index = 0;
                Index < static_cast<std::size_t>(Texture.Width) * Texture.Height; ++Index)
            {
                const aiTexel& Texel = Embedded->pcData[Index];
                Texture.Pixels.insert(Texture.Pixels.end(), {Texel.r, Texel.g, Texel.b, Texel.a});
            }
            bImported = ValidateTexture(Texture);
        }
    }
    else
    {
        bImported = ImportTexture(SourceFile.parent_path() / TexturePath.C_Str(), Texture);
    }
    if (!bImported) return -1;
    FSkeletalImportResult::FImportedTexture Imported;
    Imported.Name = SanitizeFileName(std::move(Name), Result.Textures.size());
    Imported.Texture = std::move(Texture);
    Result.Textures.push_back(std::move(Imported));
    return static_cast<int32>(Result.Textures.size() - 1);
}
}

bool ImportSkeletalAnimation(
    const std::filesystem::path& SourceFile,
    const FAssetPath& SkeletonAssetPath,
    const FSkeletalImportOptions& Options,
    FSkeletalImportResult& OutResult,
    ESkeletalImportError* OutError)
{
    Report(OutError, ESkeletalImportError::None);
    if (SourceFile.empty() || !SkeletonAssetPath.IsValid()
        || !std::isfinite(Options.SourceUnitToCentimeters)
        || Options.SourceUnitToCentimeters <= 0.0f
        || Options.MaxBoneInfluences == 0 || Options.MaxBoneInfluences > 4)
    {
        Report(OutError, ESkeletalImportError::InvalidArgument);
        return false;
    }

    Assimp::Importer Importer;
    const aiScene* Scene = Importer.ReadFile(
        SourceFile.string(),
        aiProcess_Triangulate
            | aiProcess_GenSmoothNormals
              | aiProcess_JoinIdenticalVertices
              | aiProcess_ImproveCacheLocality
              | aiProcess_LimitBoneWeights
              | aiProcess_PopulateArmatureData
              | aiProcess_ValidateDataStructure);
    if (Scene == nullptr || Scene->mRootNode == nullptr)
    {
        Report(OutError, ESkeletalImportError::ImportFailed);
        return false;
    }
    if (Scene->mNumMeshes == 0)
    {
        Report(OutError, ESkeletalImportError::MissingMesh);
        return false;
    }

    FSkeletalImportResult Result;
    Result.Materials.reserve(Scene->mNumMaterials);
    for (unsigned MaterialIndex = 0; MaterialIndex < Scene->mNumMaterials; ++MaterialIndex)
    {
        const aiMaterial* SourceMaterial = Scene->mMaterials[MaterialIndex];
        FSkeletalImportResult::FImportedMaterial Imported;
        aiString MaterialName;
        SourceMaterial->Get(AI_MATKEY_NAME, MaterialName);
        Imported.Name = SanitizeFileName(MaterialName.C_Str(), MaterialIndex);
        aiColor4D BaseColor(0.8f, 0.8f, 0.8f, 1.0f);
        if (SourceMaterial->Get(AI_MATKEY_BASE_COLOR, BaseColor) != AI_SUCCESS)
            SourceMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, BaseColor);
        Imported.Material.BaseColor = {BaseColor.r, BaseColor.g, BaseColor.b};
        SourceMaterial->Get(AI_MATKEY_METALLIC_FACTOR, Imported.Material.Metallic);
        SourceMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, Imported.Material.Roughness);
        aiString TexturePath;
        if (SourceMaterial->GetTexture(aiTextureType_BASE_COLOR, 0, &TexturePath) == AI_SUCCESS
            || SourceMaterial->GetTexture(aiTextureType_DIFFUSE, 0, &TexturePath) == AI_SUCCESS)
        {
            Imported.BaseColorTextureIndex = ImportMaterialTexture(
                *Scene, TexturePath, SourceFile, Imported.Name + "_BaseColor", Result);
            if (Imported.BaseColorTextureIndex < 0)
                Result.Warnings.push_back("Could not decode BaseColor texture for " + Imported.Name);
        }
        Result.Materials.push_back(std::move(Imported));
    }
    std::unordered_set<const aiNode*> RequiredNodes;
    std::unordered_map<std::string, const aiNode*> Nodes;
    CollectRequiredNodes(*Scene, RequiredNodes, Nodes);
    if (RequiredNodes.empty())
    {
        Report(OutError, ESkeletalImportError::MissingSkeleton);
        return false;
    }
    std::unordered_map<std::string, uint32> BoneMap;
    BuildSkeletonRecursive(Scene->mRootNode, -1, RequiredNodes, Options, Result.Skeleton, BoneMap);

    std::unordered_set<uint32> WeightedBones;
    for (unsigned MeshIndex = 0; MeshIndex < Scene->mNumMeshes; ++MeshIndex)
    {
        const aiMesh* SourceMesh = Scene->mMeshes[MeshIndex];
        for (unsigned BoneIndex = 0; BoneIndex < SourceMesh->mNumBones; ++BoneIndex)
        {
            const aiBone* SourceBone = SourceMesh->mBones[BoneIndex];
            const auto Found = BoneMap.find(SourceBone->mName.C_Str());
            if (Found == BoneMap.end()) continue;
            Result.Skeleton.Bones[Found->second].InverseBindMatrix =
                ConvertMatrix(SourceBone->mOffsetMatrix, Options);
            WeightedBones.insert(Found->second);
        }
    }
    if (!ValidateSkeleton(Result.Skeleton))
    {
        Report(OutError, ESkeletalImportError::InvalidSkeleton);
        return false;
    }

    Result.Mesh.SkeletonAsset = SkeletonAssetPath;
    bool bFirstBounds = true;
    for (unsigned MeshIndex = 0; MeshIndex < Scene->mNumMeshes; ++MeshIndex)
    {
        const aiMesh* SourceMesh = Scene->mMeshes[MeshIndex];
        const uint32 BaseVertex = static_cast<uint32>(Result.Mesh.Vertices.size());
        const uint32 FirstIndex = static_cast<uint32>(Result.Mesh.Indices.size());
        std::vector<std::vector<std::pair<uint32, float>>> Influences(SourceMesh->mNumVertices);
        for (unsigned BoneIndex = 0; BoneIndex < SourceMesh->mNumBones; ++BoneIndex)
        {
            const aiBone* SourceBone = SourceMesh->mBones[BoneIndex];
            const auto Found = BoneMap.find(SourceBone->mName.C_Str());
            if (Found == BoneMap.end()) continue;
            for (unsigned WeightIndex = 0; WeightIndex < SourceBone->mNumWeights; ++WeightIndex)
            {
                const aiVertexWeight& Weight = SourceBone->mWeights[WeightIndex];
                if (Weight.mVertexId < Influences.size() && Weight.mWeight > 0.0f)
                    Influences[Weight.mVertexId].push_back({Found->second, Weight.mWeight});
            }
        }
        for (unsigned VertexIndex = 0; VertexIndex < SourceMesh->mNumVertices; ++VertexIndex)
        {
            FSkeletalMeshVertex Vertex;
            Vertex.Position = ConvertVector(SourceMesh->mVertices[VertexIndex], Options, true);
            Vertex.Normal = SourceMesh->HasNormals()
                ? ConvertVector(SourceMesh->mNormals[VertexIndex], Options).GetSafeNormal()
                : FVector3::UpVector;
            if (SourceMesh->HasTextureCoords(0))
                Vertex.TexCoord = {SourceMesh->mTextureCoords[0][VertexIndex].x,
                    1.0f - SourceMesh->mTextureCoords[0][VertexIndex].y};
            auto& VertexInfluences = Influences[VertexIndex];
            std::sort(VertexInfluences.begin(), VertexInfluences.end(),
                [](const auto& Left, const auto& Right) { return Left.second > Right.second; });
            if (VertexInfluences.empty())
            {
                Vertex.BoneIndices[0] = 0;
                Vertex.BoneWeights[0] = 1.0f;
                Result.Warnings.push_back("A vertex had no bone weight and was assigned to bone 0");
            }
            else
            {
                Vertex.BoneWeights.fill(0.0f);
                float Sum = 0.0f;
                const std::size_t Count = std::min<std::size_t>(
                    VertexInfluences.size(), Options.MaxBoneInfluences);
                for (std::size_t Influence = 0; Influence < Count; ++Influence)
                {
                    Vertex.BoneIndices[Influence] = VertexInfluences[Influence].first;
                    Vertex.BoneWeights[Influence] = VertexInfluences[Influence].second;
                    Sum += VertexInfluences[Influence].second;
                }
                if (Sum <= SmallNumber)
                {
                    Report(OutError, ESkeletalImportError::InvalidWeights);
                    return false;
                }
                for (float& Weight : Vertex.BoneWeights) Weight /= Sum;
            }
            Result.Mesh.Vertices.push_back(Vertex);
            if (bFirstBounds)
            {
                Result.Mesh.Bounds.Min = Vertex.Position;
                Result.Mesh.Bounds.Max = Vertex.Position;
                bFirstBounds = false;
            }
            else
            {
                Result.Mesh.Bounds.Min.X = std::min(Result.Mesh.Bounds.Min.X, Vertex.Position.X);
                Result.Mesh.Bounds.Min.Y = std::min(Result.Mesh.Bounds.Min.Y, Vertex.Position.Y);
                Result.Mesh.Bounds.Min.Z = std::min(Result.Mesh.Bounds.Min.Z, Vertex.Position.Z);
                Result.Mesh.Bounds.Max.X = std::max(Result.Mesh.Bounds.Max.X, Vertex.Position.X);
                Result.Mesh.Bounds.Max.Y = std::max(Result.Mesh.Bounds.Max.Y, Vertex.Position.Y);
                Result.Mesh.Bounds.Max.Z = std::max(Result.Mesh.Bounds.Max.Z, Vertex.Position.Z);
            }
        }
        for (unsigned FaceIndex = 0; FaceIndex < SourceMesh->mNumFaces; ++FaceIndex)
        {
            const aiFace& Face = SourceMesh->mFaces[FaceIndex];
            if (Face.mNumIndices != 3) continue;
            for (unsigned Index = 0; Index < 3; ++Index)
                Result.Mesh.Indices.push_back(BaseVertex + Face.mIndices[Index]);
        }
        FStaticMeshSection Section;
        Section.FirstIndex = FirstIndex;
        Section.IndexCount = static_cast<uint32>(Result.Mesh.Indices.size()) - FirstIndex;
        Section.MaterialSlotName = SourceMesh->mMaterialIndex < Result.Materials.size()
            ? Result.Materials[SourceMesh->mMaterialIndex].Name
            : "Material_" + std::to_string(SourceMesh->mMaterialIndex);
        if (Section.IndexCount > 0) Result.Mesh.Sections.push_back(std::move(Section));
    }
    if (!ValidateSkeletalMesh(Result.Mesh, &Result.Skeleton))
    {
        Report(OutError, ESkeletalImportError::InvalidWeights);
        return false;
    }

    int32 RootBone = -1;
    for (uint32 Bone : WeightedBones)
        if (RootBone < 0 || Result.Skeleton.Bones[Bone].ParentIndex <
                Result.Skeleton.Bones[static_cast<std::size_t>(RootBone)].ParentIndex)
            RootBone = static_cast<int32>(Bone);

    for (unsigned AnimationIndex = 0; AnimationIndex < Scene->mNumAnimations; ++AnimationIndex)
    {
        const aiAnimation* SourceAnimation = Scene->mAnimations[AnimationIndex];
        const double TicksPerSecond = SourceAnimation->mTicksPerSecond > 0.0
            ? SourceAnimation->mTicksPerSecond : 30.0;
        FAnimationClipData Clip;
        Clip.SkeletonAsset = SkeletonAssetPath;
        Clip.Name = SanitizeFileName(SourceAnimation->mName.C_Str(), AnimationIndex);
        Clip.Duration = static_cast<float>(SourceAnimation->mDuration / TicksPerSecond);
        Clip.bLooping = true;
        Clip.RootBoneIndex = RootBone;
        for (unsigned ChannelIndex = 0; ChannelIndex < SourceAnimation->mNumChannels; ++ChannelIndex)
        {
            const aiNodeAnim* Channel = SourceAnimation->mChannels[ChannelIndex];
            const auto Found = BoneMap.find(Channel->mNodeName.C_Str());
            if (Found == BoneMap.end()) continue;
            FBoneAnimationTrack Track;
            Track.BoneIndex = Found->second;
            for (unsigned Index = 0; Index < Channel->mNumPositionKeys; ++Index)
                Track.TranslationKeys.push_back({
                    static_cast<float>(Channel->mPositionKeys[Index].mTime / TicksPerSecond),
                    ConvertVector(Channel->mPositionKeys[Index].mValue, Options, true)});
            for (unsigned Index = 0; Index < Channel->mNumRotationKeys; ++Index)
                Track.RotationKeys.push_back({
                    static_cast<float>(Channel->mRotationKeys[Index].mTime / TicksPerSecond),
                    ConvertQuat(Channel->mRotationKeys[Index].mValue, Options)});
            for (unsigned Index = 0; Index < Channel->mNumScalingKeys; ++Index)
                Track.ScaleKeys.push_back({
                    static_cast<float>(Channel->mScalingKeys[Index].mTime / TicksPerSecond),
                    ConvertVector(Channel->mScalingKeys[Index].mValue, Options)});
            Clip.Tracks.push_back(std::move(Track));
        }
        if (!ValidateAnimationClip(Clip, &Result.Skeleton))
        {
            Result.Warnings.push_back("Skipped invalid animation " + Clip.Name);
            continue;
        }
        Result.Animations.push_back(std::move(Clip));
    }
    OutResult = std::move(Result);
    return true;
}

bool SaveSkeletalImportResult(
    const FSkeletalImportResult& Result,
    const std::filesystem::path& SkeletonFile,
    const std::filesystem::path& MeshFile,
    const std::filesystem::path& AnimationDirectory,
    ESkeletalImportError* OutError)
{
    Report(OutError, ESkeletalImportError::None);
    ESkeletalAssetError AssetError = ESkeletalAssetError::None;
    if (!SaveSkeletonToFile(SkeletonFile, Result.Skeleton, &AssetError)
        || !SaveSkeletalMeshToFile(MeshFile, Result.Mesh, &AssetError))
    {
        Report(OutError, ESkeletalImportError::SaveFailed);
        return false;
    }
    for (std::size_t Index = 0; Index < Result.Animations.size(); ++Index)
    {
        const std::string Name = SanitizeFileName(Result.Animations[Index].Name, Index);
        if (!SaveAnimationClipToFile(
                AnimationDirectory / (Name + ".panimation"), Result.Animations[Index], &AssetError))
        {
            Report(OutError, ESkeletalImportError::SaveFailed);
            return false;
        }
    }
    return true;
}

std::string_view ToString(ESkeletalImportError Error)
{
    switch (Error)
    {
    case ESkeletalImportError::None: return "None";
    case ESkeletalImportError::AssimpUnavailable: return "AssimpUnavailable";
    case ESkeletalImportError::InvalidArgument: return "InvalidArgument";
    case ESkeletalImportError::ImportFailed: return "ImportFailed";
    case ESkeletalImportError::MissingMesh: return "MissingMesh";
    case ESkeletalImportError::MissingSkeleton: return "MissingSkeleton";
    case ESkeletalImportError::InvalidSkeleton: return "InvalidSkeleton";
    case ESkeletalImportError::InvalidWeights: return "InvalidWeights";
    case ESkeletalImportError::InvalidAnimation: return "InvalidAnimation";
    case ESkeletalImportError::SaveFailed: return "SaveFailed";
    }
    return "Unknown";
}
}
