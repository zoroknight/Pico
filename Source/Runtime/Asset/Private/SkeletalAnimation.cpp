#include "Pico/Asset/SkeletalAnimation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>
#include <utility>

namespace Pico
{
namespace
{
constexpr uint32 SkeletonMagic = 0x4c4b5350;
constexpr uint32 SkeletalMeshMagic = 0x4d4b5350;
constexpr uint32 AnimationMagic = 0x4d4e4150;
constexpr uint32 AnimationSetMagic = 0x54455350;
constexpr uint32 AnimationMontageMagic = 0x474d4150;
constexpr uint32 FormatVersion = 1;
constexpr uint32 SkeletalMeshFormatVersion = 2;
constexpr uint32 MaxBones = 1024;
constexpr uint32 MaxVertices = 16 * 1024 * 1024;
constexpr uint32 MaxIndices = 64 * 1024 * 1024;
constexpr uint32 MaxTracks = 4096;
constexpr uint32 MaxKeys = 16 * 1024 * 1024;
constexpr uint32 MaxNotifies = 64 * 1024;
constexpr uint32 MaxMontageSegments = 4096;
constexpr uint32 MaxMontageSections = 4096;
constexpr uint32 MaxString = 4096;
constexpr std::size_t MaxFileSize = 512ull * 1024ull * 1024ull;

void Report(ESkeletalAssetError* OutError, ESkeletalAssetError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

bool Finite(float Value) { return std::isfinite(Value); }
bool Finite(const FVector2& Value) { return Finite(Value.X) && Finite(Value.Y); }
bool Finite(const FVector3& Value)
{
    return Finite(Value.X) && Finite(Value.Y) && Finite(Value.Z);
}
bool Finite(const FQuat& Value)
{
    return Finite(Value.X) && Finite(Value.Y) && Finite(Value.Z) && Finite(Value.W);
}
bool Finite(const FTransform& Value)
{
    return Finite(Value.Translation) && Finite(Value.Rotation) && Finite(Value.Scale);
}

class FWriter
{
public:
    void U32(uint32 Value)
    {
        for (std::size_t Index = 0; Index < sizeof(Value); ++Index)
            Data.push_back(static_cast<uint8>((Value >> (Index * 8)) & 0xffu));
    }
    void I32(int32 Value) { U32(std::bit_cast<uint32>(Value)); }
    void U8(uint8 Value) { Data.push_back(Value); }
    void Float(float Value) { U32(std::bit_cast<uint32>(Value)); }
    bool String(std::string_view Value)
    {
        if (Value.size() > MaxString) return false;
        U32(static_cast<uint32>(Value.size()));
        Data.insert(Data.end(), Value.begin(), Value.end());
        return true;
    }
    void Vector2(const FVector2& Value) { Float(Value.X); Float(Value.Y); }
    void Vector3(const FVector3& Value) { Float(Value.X); Float(Value.Y); Float(Value.Z); }
    void Quat(const FQuat& Value) { Float(Value.X); Float(Value.Y); Float(Value.Z); Float(Value.W); }
    void Transform(const FTransform& Value)
    {
        Quat(Value.Rotation); Vector3(Value.Translation); Vector3(Value.Scale);
    }
    void Matrix(const FMatrix4& Value)
    {
        for (std::size_t Row = 0; Row < 4; ++Row)
            for (std::size_t Column = 0; Column < 4; ++Column) Float(Value.M[Row][Column]);
    }
    std::vector<uint8> Take() { return std::move(Data); }

private:
    std::vector<uint8> Data;
};

class FReader
{
public:
    explicit FReader(std::span<const uint8> InData) : Data(InData) {}
    bool U32(uint32& Out)
    {
        if (Remaining() < sizeof(uint32)) return false;
        Out = 0;
        for (std::size_t Index = 0; Index < sizeof(uint32); ++Index)
            Out |= static_cast<uint32>(Data[Offset++]) << (Index * 8);
        return true;
    }
    bool I32(int32& Out)
    {
        uint32 Bits = 0;
        if (!U32(Bits)) return false;
        Out = std::bit_cast<int32>(Bits);
        return true;
    }
    bool U8(uint8& Out)
    {
        if (Remaining() < 1) return false;
        Out = Data[Offset++];
        return true;
    }
    bool Float(float& Out)
    {
        uint32 Bits = 0;
        if (!U32(Bits)) return false;
        Out = std::bit_cast<float>(Bits);
        return true;
    }
    bool String(std::string& Out)
    {
        uint32 Size = 0;
        if (!U32(Size) || Size > MaxString || Remaining() < Size) return false;
        Out.assign(reinterpret_cast<const char*>(Data.data() + Offset), Size);
        Offset += Size;
        return true;
    }
    bool Vector2(FVector2& Value) { return Float(Value.X) && Float(Value.Y); }
    bool Vector3(FVector3& Value) { return Float(Value.X) && Float(Value.Y) && Float(Value.Z); }
    bool Quat(FQuat& Value)
    {
        return Float(Value.X) && Float(Value.Y) && Float(Value.Z) && Float(Value.W);
    }
    bool Transform(FTransform& Value)
    {
        return Quat(Value.Rotation) && Vector3(Value.Translation) && Vector3(Value.Scale);
    }
    bool Matrix(FMatrix4& Value)
    {
        for (std::size_t Row = 0; Row < 4; ++Row)
            for (std::size_t Column = 0; Column < 4; ++Column)
                if (!Float(Value.M[Row][Column])) return false;
        return true;
    }
    std::size_t Remaining() const { return Data.size() - Offset; }

private:
    std::span<const uint8> Data;
    std::size_t Offset = 0;
};

bool ReplaceFile(const std::filesystem::path& Temporary, const std::filesystem::path& Target)
{
    std::error_code Error;
    std::filesystem::rename(Temporary, Target, Error);
    if (!Error) return true;
    Error.clear();
    if (!std::filesystem::is_regular_file(Target, Error)) return false;
    std::filesystem::path Backup = Target;
    Backup += ".bak";
    std::filesystem::remove(Backup, Error);
    Error.clear();
    std::filesystem::rename(Target, Backup, Error);
    if (Error) return false;
    std::filesystem::rename(Temporary, Target, Error);
    if (Error)
    {
        std::error_code RestoreError;
        std::filesystem::rename(Backup, Target, RestoreError);
        return false;
    }
    std::filesystem::remove(Backup, Error);
    return true;
}

bool SaveBytes(const std::filesystem::path& FilePath, std::vector<uint8> Data,
    ESkeletalAssetError* OutError)
{
    if (FilePath.empty()) { Report(OutError, ESkeletalAssetError::InvalidArgument); return false; }
    std::error_code Error;
    std::filesystem::create_directories(FilePath.parent_path(), Error);
    if (Error) { Report(OutError, ESkeletalAssetError::FileWriteFailed); return false; }
    std::filesystem::path Temporary = FilePath;
    Temporary += ".tmp";
    std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
    File.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
    File.close();
    if (!File || !ReplaceFile(Temporary, FilePath))
    {
        Report(OutError, ESkeletalAssetError::FileWriteFailed);
        return false;
    }
    Report(OutError, ESkeletalAssetError::None);
    return true;
}

bool LoadBytes(const std::filesystem::path& FilePath, std::vector<uint8>& OutData,
    ESkeletalAssetError* OutError)
{
    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File) { Report(OutError, ESkeletalAssetError::FileOpenFailed); return false; }
    const std::streampos End = File.tellg();
    if (End < 0) { Report(OutError, ESkeletalAssetError::FileReadFailed); return false; }
    const std::size_t Size = static_cast<std::size_t>(End);
    if (Size > MaxFileSize) { Report(OutError, ESkeletalAssetError::FileTooLarge); return false; }
    OutData.resize(Size);
    File.seekg(0, std::ios::beg);
    if (Size > 0) File.read(reinterpret_cast<char*>(OutData.data()), static_cast<std::streamsize>(Size));
    if (!File) { Report(OutError, ESkeletalAssetError::FileReadFailed); return false; }
    return true;
}

bool WriteHeader(FWriter& Writer, uint32 Magic, uint32 Version = FormatVersion)
{
    Writer.U32(Magic); Writer.U32(Version); return true;
}

bool ReadHeader(FReader& Reader, uint32 Expected, ESkeletalAssetError* OutError,
    uint32 MaximumVersion = FormatVersion, uint32* OutVersion = nullptr)
{
    uint32 Magic = 0;
    uint32 Version = 0;
    if (!Reader.U32(Magic) || !Reader.U32(Version) || Magic != Expected)
    {
        Report(OutError, ESkeletalAssetError::InvalidArchive); return false;
    }
    if (Version == 0 || Version > MaximumVersion)
    {
        Report(OutError, ESkeletalAssetError::UnsupportedVersion); return false;
    }
    if (OutVersion != nullptr) *OutVersion = Version;
    return true;
}

FVector3 Lerp(const FVector3& A, const FVector3& B, float Alpha)
{
    return A + (B - A) * Alpha;
}

FQuat Nlerp(FQuat A, FQuat B, float Alpha)
{
    const float Dot = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
    if (Dot < 0.0f) B = FQuat(-B.X, -B.Y, -B.Z, -B.W);
    return FQuat(
        A.X + (B.X - A.X) * Alpha,
        A.Y + (B.Y - A.Y) * Alpha,
        A.Z + (B.Z - A.Z) * Alpha,
        A.W + (B.W - A.W) * Alpha).GetNormalized();
}

template<typename Key, typename Value, typename Interpolator>
Value SampleKeys(const std::vector<Key>& Keys, float Time, const Value& Fallback, Interpolator Interpolate)
{
    if (Keys.empty()) return Fallback;
    if (Time <= Keys.front().Time) return Keys.front().Value;
    if (Time >= Keys.back().Time) return Keys.back().Value;
    const auto Upper = std::upper_bound(Keys.begin(), Keys.end(), Time,
        [](float ValueTime, const Key& Item) { return ValueTime < Item.Time; });
    const Key& Right = *Upper;
    const Key& Left = *std::prev(Upper);
    const float Span = Right.Time - Left.Time;
    const float Alpha = Span > SmallNumber ? (Time - Left.Time) / Span : 0.0f;
    return Interpolate(Left.Value, Right.Value, Alpha);
}

bool ValidKeyTimes(const auto& Keys, float Duration)
{
    float Previous = -1.0f;
    for (const auto& Key : Keys)
    {
        if (!Finite(Key.Time) || Key.Time < 0.0f || Key.Time > Duration || Key.Time < Previous)
            return false;
        Previous = Key.Time;
    }
    return true;
}

void BuildComponentAndSkinning(const FSkeletonData& Skeleton, FSkeletonPose& Pose)
{
    const std::size_t Count = Skeleton.Bones.size();
    Pose.ComponentTransforms.resize(Count);
    Pose.SkinningMatrices.resize(Count);
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        const int32 Parent = Skeleton.Bones[Index].ParentIndex;
        Pose.ComponentTransforms[Index] = Parent >= 0
            ? Pose.LocalTransforms[Index] * Pose.ComponentTransforms[static_cast<std::size_t>(Parent)]
            : Pose.LocalTransforms[Index];
        Pose.SkinningMatrices[Index] =
            Pose.ComponentTransforms[Index].ToMatrix() * Skeleton.Bones[Index].InverseBindMatrix;
    }
}

FTransform SampleRootTransform(const FSkeletonData& Skeleton, const FAnimationClipData& Clip, float Time)
{
    FSkeletonPose Pose;
    if (!SampleAnimationClip(Skeleton, Clip, Time, Pose)
        || Clip.RootBoneIndex < 0
        || static_cast<std::size_t>(Clip.RootBoneIndex) >= Pose.ComponentTransforms.size())
        return FTransform::Identity;
    return Pose.ComponentTransforms[static_cast<std::size_t>(Clip.RootBoneIndex)];
}
}

bool ValidateSkeleton(const FSkeletonData& Skeleton, ESkeletalAssetError* OutError)
{
    Report(OutError, ESkeletalAssetError::None);
    if (Skeleton.Bones.empty() || Skeleton.Bones.size() > MaxBones)
    {
        Report(OutError, ESkeletalAssetError::LimitExceeded); return false;
    }
    for (std::size_t Index = 0; Index < Skeleton.Bones.size(); ++Index)
    {
        const FSkeletonBone& Bone = Skeleton.Bones[Index];
        if (Bone.Name.empty() || Bone.Name.size() > MaxString || !Finite(Bone.ReferenceLocalPose)
            || Bone.ParentIndex >= static_cast<int32>(Index) || Bone.ParentIndex < -1)
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
        for (std::size_t Row = 0; Row < 4; ++Row)
            for (std::size_t Column = 0; Column < 4; ++Column)
                if (!Finite(Bone.InverseBindMatrix.M[Row][Column]))
                {
                    Report(OutError, ESkeletalAssetError::InvalidData); return false;
                }
        for (std::size_t Other = 0; Other < Index; ++Other)
            if (Skeleton.Bones[Other].Name == Bone.Name)
            {
                Report(OutError, ESkeletalAssetError::InvalidData); return false;
            }
    }
    return true;
}

bool ValidateSkeletalMesh(const FSkeletalMeshData& Mesh, const FSkeletonData* Skeleton,
    ESkeletalAssetError* OutError)
{
    Report(OutError, ESkeletalAssetError::None);
    if (!Mesh.SkeletonAsset.IsValid() || Mesh.Vertices.empty() || Mesh.Indices.empty()
          || Mesh.Sections.empty() || Mesh.Indices.size() % 3 != 0)
    {
        Report(OutError, ESkeletalAssetError::InvalidData); return false;
    }
    if (Mesh.Vertices.size() > MaxVertices || Mesh.Indices.size() > MaxIndices)
    {
        Report(OutError, ESkeletalAssetError::LimitExceeded); return false;
    }
    for (const FSkeletalMeshVertex& Vertex : Mesh.Vertices)
    {
        float WeightSum = 0.0f;
        if (!Finite(Vertex.Position) || !Finite(Vertex.Normal) || !Finite(Vertex.TexCoord))
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
        for (std::size_t Influence = 0; Influence < 4; ++Influence)
        {
            if (!Finite(Vertex.BoneWeights[Influence]) || Vertex.BoneWeights[Influence] < 0.0f
                || (Skeleton != nullptr && Vertex.BoneIndices[Influence] >= Skeleton->Bones.size()))
            {
                Report(OutError, ESkeletalAssetError::InvalidData); return false;
            }
            WeightSum += Vertex.BoneWeights[Influence];
        }
        if (std::abs(WeightSum - 1.0f) > 0.01f)
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
    }
    for (uint32 Index : Mesh.Indices) if (Index >= Mesh.Vertices.size())
    {
        Report(OutError, ESkeletalAssetError::InvalidData); return false;
    }
    uint32 Expected = 0;
    for (const FStaticMeshSection& Section : Mesh.Sections)
    {
        if (Section.FirstIndex != Expected || Section.IndexCount == 0 || Section.IndexCount % 3 != 0
            || Section.IndexCount > Mesh.Indices.size() - std::min<std::size_t>(Section.FirstIndex, Mesh.Indices.size()))
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
        Expected += Section.IndexCount;
    }
    if (Expected != Mesh.Indices.size() || !Finite(Mesh.Bounds.Min) || !Finite(Mesh.Bounds.Max)
        || Mesh.Bounds.Min.X > Mesh.Bounds.Max.X || Mesh.Bounds.Min.Y > Mesh.Bounds.Max.Y
        || Mesh.Bounds.Min.Z > Mesh.Bounds.Max.Z)
    {
        Report(OutError, ESkeletalAssetError::InvalidData); return false;
    }
    if (!Mesh.DefaultMaterials.empty() && Mesh.DefaultMaterials.size() != Mesh.Sections.size())
    {
        Report(OutError, ESkeletalAssetError::InvalidData); return false;
    }
    for (const FAssetPath& Material : Mesh.DefaultMaterials)
        if (Material.IsValid() && Material.GetExtension() != ".pmat")
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
    return true;
}

bool ValidateAnimationClip(const FAnimationClipData& Clip, const FSkeletonData* Skeleton,
    ESkeletalAssetError* OutError)
{
    Report(OutError, ESkeletalAssetError::None);
    if (!Clip.SkeletonAsset.IsValid() || Clip.Name.empty() || !Finite(Clip.Duration)
        || Clip.Duration <= 0.0f || Clip.Tracks.size() > MaxTracks
        || Clip.Notifies.size() > MaxNotifies)
    {
        Report(OutError, ESkeletalAssetError::InvalidData); return false;
    }
    if (Clip.RootBoneIndex < -1
        || (Skeleton != nullptr && Clip.RootBoneIndex >= static_cast<int32>(Skeleton->Bones.size())))
    {
        Report(OutError, ESkeletalAssetError::InvalidData); return false;
    }
    uint64 KeyCount = 0;
    std::vector<bool> Seen(Skeleton != nullptr ? Skeleton->Bones.size() : 0, false);
    for (const FBoneAnimationTrack& Track : Clip.Tracks)
    {
        if (Skeleton != nullptr && Track.BoneIndex >= Skeleton->Bones.size())
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
        if (!Seen.empty() && Seen[Track.BoneIndex])
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
        if (!Seen.empty()) Seen[Track.BoneIndex] = true;
        KeyCount += Track.TranslationKeys.size() + Track.RotationKeys.size() + Track.ScaleKeys.size();
        if (KeyCount > MaxKeys || !ValidKeyTimes(Track.TranslationKeys, Clip.Duration)
            || !ValidKeyTimes(Track.RotationKeys, Clip.Duration)
            || !ValidKeyTimes(Track.ScaleKeys, Clip.Duration))
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
        for (const FVectorKey& Key : Track.TranslationKeys) if (!Finite(Key.Value))
        { Report(OutError, ESkeletalAssetError::InvalidData); return false; }
        for (const FVectorKey& Key : Track.ScaleKeys) if (!Finite(Key.Value))
        { Report(OutError, ESkeletalAssetError::InvalidData); return false; }
        for (const FQuatKey& Key : Track.RotationKeys) if (!Finite(Key.Value))
        { Report(OutError, ESkeletalAssetError::InvalidData); return false; }
    }
    for (const FAnimationNotifyData& Notify : Clip.Notifies)
    {
        if (Notify.Name.empty() || Notify.Name.size() > MaxString || !Finite(Notify.BeginTime)
            || !Finite(Notify.EndTime) || Notify.BeginTime < 0.0f
            || Notify.EndTime < Notify.BeginTime || Notify.EndTime > Clip.Duration)
        {
            Report(OutError, ESkeletalAssetError::InvalidData); return false;
        }
    }
    return true;
}

bool ValidateAnimationSet(const FAnimationSetData& AnimationSet, ESkeletalAssetError* OutError)
{
    Report(OutError, ESkeletalAssetError::None);
    if (!AnimationSet.SkeletonAsset.IsValid()
        || !AnimationSet.IdleAnimation.IsValid()
        || !AnimationSet.WalkAnimation.IsValid()
        || !AnimationSet.JumpAnimation.IsValid())
    {
        Report(OutError, ESkeletalAssetError::InvalidData);
        return false;
    }
    return true;
}

bool ValidateAnimationMontage(const FAnimationMontageData& Montage, ESkeletalAssetError* OutError)
{
    Report(OutError, ESkeletalAssetError::None);
    if (!Montage.SkeletonAsset.IsValid() || Montage.SlotName.empty()
        || Montage.SlotName.size() > MaxString || !Finite(Montage.BlendInTime)
        || !Finite(Montage.BlendOutTime) || Montage.BlendInTime < 0.0f
        || Montage.BlendOutTime < 0.0f || Montage.Segments.empty()
        || Montage.Segments.size() > MaxMontageSegments
        || Montage.Sections.empty() || Montage.Sections.size() > MaxMontageSections
        || Montage.Notifies.size() > MaxNotifies)
    {
        Report(OutError, ESkeletalAssetError::InvalidData);
        return false;
    }
    float Duration = 0.0f;
    float PreviousEnd = 0.0f;
    for (const FAnimationMontageSegment& Segment : Montage.Segments)
    {
        if (!Segment.AnimationAsset.IsValid() || !Finite(Segment.StartTime)
            || !Finite(Segment.EndTime) || !Finite(Segment.PlayRate)
            || Segment.StartTime < 0.0f || Segment.EndTime <= Segment.StartTime
            || Segment.PlayRate <= 0.0f || Segment.StartTime < PreviousEnd)
        {
            Report(OutError, ESkeletalAssetError::InvalidData);
            return false;
        }
        PreviousEnd = Segment.EndTime;
        Duration = std::max(Duration, Segment.EndTime);
    }
    std::set<std::string> Names;
    float PreviousStart = -1.0f;
    for (const FAnimationMontageSection& Section : Montage.Sections)
    {
        if (Section.Name.empty() || Section.Name.size() > MaxString
            || !Finite(Section.StartTime) || Section.StartTime < 0.0f
            || Section.StartTime >= Duration || Section.StartTime <= PreviousStart
            || !Names.insert(Section.Name).second)
        {
            Report(OutError, ESkeletalAssetError::InvalidData);
            return false;
        }
        PreviousStart = Section.StartTime;
    }
    for (const FAnimationMontageSection& Section : Montage.Sections)
    {
        if (!Section.NextSection.empty() && !Names.contains(Section.NextSection))
        {
            Report(OutError, ESkeletalAssetError::InvalidData);
            return false;
        }
    }
    for (const FAnimationNotifyData& Notify : Montage.Notifies)
    {
        if (Notify.Name.empty() || Notify.Name.size() > MaxString
            || !Finite(Notify.BeginTime) || !Finite(Notify.EndTime)
            || Notify.BeginTime < 0.0f || Notify.EndTime < Notify.BeginTime
            || Notify.EndTime > Duration)
        {
            Report(OutError, ESkeletalAssetError::InvalidData);
            return false;
        }
    }
    return true;
}

bool SaveSkeletonToFile(const std::filesystem::path& FilePath, const FSkeletonData& Skeleton,
    ESkeletalAssetError* OutError)
{
    if (!ValidateSkeleton(Skeleton, OutError)) return false;
    FWriter Writer; WriteHeader(Writer, SkeletonMagic); Writer.U32(static_cast<uint32>(Skeleton.Bones.size()));
    for (const FSkeletonBone& Bone : Skeleton.Bones)
    {
        if (!Writer.String(Bone.Name)) { Report(OutError, ESkeletalAssetError::InvalidData); return false; }
        Writer.I32(Bone.ParentIndex); Writer.Transform(Bone.ReferenceLocalPose); Writer.Matrix(Bone.InverseBindMatrix);
    }
    return SaveBytes(FilePath, Writer.Take(), OutError);
}

bool LoadSkeletonFromFile(const std::filesystem::path& FilePath, FSkeletonData& OutSkeleton,
    ESkeletalAssetError* OutError)
{
    std::vector<uint8> Data; if (!LoadBytes(FilePath, Data, OutError)) return false;
    FReader Reader(Data); if (!ReadHeader(Reader, SkeletonMagic, OutError)) return false;
    uint32 Count = 0; if (!Reader.U32(Count) || Count == 0 || Count > MaxBones)
    { Report(OutError, ESkeletalAssetError::LimitExceeded); return false; }
    FSkeletonData Value; Value.Bones.resize(Count);
    for (FSkeletonBone& Bone : Value.Bones)
        if (!Reader.String(Bone.Name) || !Reader.I32(Bone.ParentIndex)
            || !Reader.Transform(Bone.ReferenceLocalPose) || !Reader.Matrix(Bone.InverseBindMatrix))
        { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
    if (Reader.Remaining() != 0) { Report(OutError, ESkeletalAssetError::TrailingData); return false; }
    if (!ValidateSkeleton(Value, OutError)) return false;
    OutSkeleton = std::move(Value); return true;
}

bool SaveSkeletalMeshToFile(const std::filesystem::path& FilePath, const FSkeletalMeshData& Mesh,
    ESkeletalAssetError* OutError)
{
    if (!ValidateSkeletalMesh(Mesh, nullptr, OutError)) return false;
    FWriter Writer; WriteHeader(Writer, SkeletalMeshMagic, SkeletalMeshFormatVersion);
    if (!Writer.String(Mesh.SkeletonAsset.ToString())) return false;
    Writer.U32(static_cast<uint32>(Mesh.Vertices.size())); Writer.U32(static_cast<uint32>(Mesh.Indices.size()));
    Writer.U32(static_cast<uint32>(Mesh.Sections.size())); Writer.Vector3(Mesh.Bounds.Min); Writer.Vector3(Mesh.Bounds.Max);
    for (const FSkeletalMeshVertex& Vertex : Mesh.Vertices)
    {
        Writer.Vector3(Vertex.Position); Writer.Vector3(Vertex.Normal); Writer.Vector2(Vertex.TexCoord);
        for (uint32 Bone : Vertex.BoneIndices) Writer.U32(Bone);
        for (float Weight : Vertex.BoneWeights) Writer.Float(Weight);
    }
    for (uint32 Index : Mesh.Indices) Writer.U32(Index);
    for (const FStaticMeshSection& Section : Mesh.Sections)
    {
        Writer.U32(Section.FirstIndex); Writer.U32(Section.IndexCount);
        if (!Writer.String(Section.MaterialSlotName)) return false;
    }
    Writer.U32(static_cast<uint32>(Mesh.DefaultMaterials.size()));
    for (const FAssetPath& Material : Mesh.DefaultMaterials)
        if (!Writer.String(Material.ToString())) return false;
    return SaveBytes(FilePath, Writer.Take(), OutError);
}

bool LoadSkeletalMeshFromFile(const std::filesystem::path& FilePath, FSkeletalMeshData& OutMesh,
    ESkeletalAssetError* OutError)
{
    std::vector<uint8> Data; if (!LoadBytes(FilePath, Data, OutError)) return false;
    FReader Reader(Data); uint32 Version = 0;
    if (!ReadHeader(Reader, SkeletalMeshMagic, OutError, SkeletalMeshFormatVersion, &Version)) return false;
    std::string SkeletonPath; uint32 Vertices = 0, Indices = 0, Sections = 0;
    if (!Reader.String(SkeletonPath) || !Reader.U32(Vertices) || !Reader.U32(Indices) || !Reader.U32(Sections)
        || Vertices == 0 || Vertices > MaxVertices || Indices == 0 || Indices > MaxIndices || Sections == 0)
    { Report(OutError, ESkeletalAssetError::LimitExceeded); return false; }
    FSkeletalMeshData Value;
    if (!FAssetPath::TryParse(SkeletonPath, Value.SkeletonAsset)
        || !Reader.Vector3(Value.Bounds.Min) || !Reader.Vector3(Value.Bounds.Max))
    { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
    Value.Vertices.resize(Vertices);
    for (FSkeletalMeshVertex& Vertex : Value.Vertices)
    {
        if (!Reader.Vector3(Vertex.Position) || !Reader.Vector3(Vertex.Normal) || !Reader.Vector2(Vertex.TexCoord))
        { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
        for (uint32& Bone : Vertex.BoneIndices) if (!Reader.U32(Bone)) return false;
        for (float& Weight : Vertex.BoneWeights) if (!Reader.Float(Weight)) return false;
    }
    Value.Indices.resize(Indices); for (uint32& Index : Value.Indices) if (!Reader.U32(Index)) return false;
    Value.Sections.resize(Sections);
    for (FStaticMeshSection& Section : Value.Sections)
        if (!Reader.U32(Section.FirstIndex) || !Reader.U32(Section.IndexCount) || !Reader.String(Section.MaterialSlotName))
        { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
    if (Version >= 2)
    {
        uint32 MaterialCount = 0;
        if (!Reader.U32(MaterialCount) || (MaterialCount != 0 && MaterialCount != Sections))
        { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
        Value.DefaultMaterials.resize(MaterialCount);
        for (FAssetPath& Material : Value.DefaultMaterials)
        {
            std::string Path;
            if (!Reader.String(Path) || (!Path.empty() && !FAssetPath::TryParse(Path, Material)))
            { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
        }
    }
    if (Reader.Remaining() != 0) { Report(OutError, ESkeletalAssetError::TrailingData); return false; }
    if (!ValidateSkeletalMesh(Value, nullptr, OutError)) return false;
    OutMesh = std::move(Value); return true;
}

bool SaveAnimationClipToFile(const std::filesystem::path& FilePath, const FAnimationClipData& Clip,
    ESkeletalAssetError* OutError)
{
    if (!ValidateAnimationClip(Clip, nullptr, OutError)) return false;
    FWriter Writer; WriteHeader(Writer, AnimationMagic);
    if (!Writer.String(Clip.SkeletonAsset.ToString()) || !Writer.String(Clip.Name)) return false;
    Writer.Float(Clip.Duration); Writer.U8(Clip.bLooping ? 1 : 0); Writer.I32(Clip.RootBoneIndex);
    Writer.U32(static_cast<uint32>(Clip.Tracks.size())); Writer.U32(static_cast<uint32>(Clip.Notifies.size()));
    for (const FBoneAnimationTrack& Track : Clip.Tracks)
    {
        Writer.U32(Track.BoneIndex); Writer.U32(static_cast<uint32>(Track.TranslationKeys.size()));
        Writer.U32(static_cast<uint32>(Track.RotationKeys.size())); Writer.U32(static_cast<uint32>(Track.ScaleKeys.size()));
        for (const FVectorKey& Key : Track.TranslationKeys) { Writer.Float(Key.Time); Writer.Vector3(Key.Value); }
        for (const FQuatKey& Key : Track.RotationKeys) { Writer.Float(Key.Time); Writer.Quat(Key.Value); }
        for (const FVectorKey& Key : Track.ScaleKeys) { Writer.Float(Key.Time); Writer.Vector3(Key.Value); }
    }
    for (const FAnimationNotifyData& Notify : Clip.Notifies)
    {
        if (!Writer.String(Notify.Name)) return false;
        Writer.Float(Notify.BeginTime); Writer.Float(Notify.EndTime);
    }
    return SaveBytes(FilePath, Writer.Take(), OutError);
}

bool LoadAnimationClipFromFile(const std::filesystem::path& FilePath, FAnimationClipData& OutClip,
    ESkeletalAssetError* OutError)
{
    std::vector<uint8> Data; if (!LoadBytes(FilePath, Data, OutError)) return false;
    FReader Reader(Data); if (!ReadHeader(Reader, AnimationMagic, OutError)) return false;
    std::string SkeletonPath; uint8 Looping = 0; uint32 Tracks = 0, Notifies = 0;
    FAnimationClipData Value;
    if (!Reader.String(SkeletonPath) || !Reader.String(Value.Name) || !Reader.Float(Value.Duration)
        || !Reader.U8(Looping) || Looping > 1 || !Reader.I32(Value.RootBoneIndex)
        || !Reader.U32(Tracks) || !Reader.U32(Notifies) || Tracks > MaxTracks || Notifies > MaxNotifies
        || !FAssetPath::TryParse(SkeletonPath, Value.SkeletonAsset))
    { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
    Value.bLooping = Looping != 0; Value.Tracks.resize(Tracks);
    uint64 TotalKeys = 0;
    for (FBoneAnimationTrack& Track : Value.Tracks)
    {
        uint32 Translation = 0, Rotation = 0, Scale = 0;
        if (!Reader.U32(Track.BoneIndex) || !Reader.U32(Translation) || !Reader.U32(Rotation) || !Reader.U32(Scale))
        { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
        TotalKeys += static_cast<uint64>(Translation) + Rotation + Scale;
        if (TotalKeys > MaxKeys) { Report(OutError, ESkeletalAssetError::LimitExceeded); return false; }
        Track.TranslationKeys.resize(Translation); Track.RotationKeys.resize(Rotation); Track.ScaleKeys.resize(Scale);
        for (FVectorKey& Key : Track.TranslationKeys) if (!Reader.Float(Key.Time) || !Reader.Vector3(Key.Value)) return false;
        for (FQuatKey& Key : Track.RotationKeys) if (!Reader.Float(Key.Time) || !Reader.Quat(Key.Value)) return false;
        for (FVectorKey& Key : Track.ScaleKeys) if (!Reader.Float(Key.Time) || !Reader.Vector3(Key.Value)) return false;
    }
    Value.Notifies.resize(Notifies);
    for (FAnimationNotifyData& Notify : Value.Notifies)
        if (!Reader.String(Notify.Name) || !Reader.Float(Notify.BeginTime) || !Reader.Float(Notify.EndTime)) return false;
    if (Reader.Remaining() != 0) { Report(OutError, ESkeletalAssetError::TrailingData); return false; }
    if (!ValidateAnimationClip(Value, nullptr, OutError)) return false;
    OutClip = std::move(Value); return true;
}

bool SaveAnimationSetToFile(const std::filesystem::path& FilePath,
    const FAnimationSetData& AnimationSet, ESkeletalAssetError* OutError)
{
    if (!ValidateAnimationSet(AnimationSet, OutError)) return false;
    FWriter Writer; WriteHeader(Writer, AnimationSetMagic);
    if (!Writer.String(AnimationSet.SkeletonAsset.ToString())
        || !Writer.String(AnimationSet.IdleAnimation.ToString())
        || !Writer.String(AnimationSet.WalkAnimation.ToString())
        || !Writer.String(AnimationSet.JumpAnimation.ToString())) return false;
    return SaveBytes(FilePath, Writer.Take(), OutError);
}

bool LoadAnimationSetFromFile(const std::filesystem::path& FilePath,
    FAnimationSetData& OutAnimationSet, ESkeletalAssetError* OutError)
{
    std::vector<uint8> Data; if (!LoadBytes(FilePath, Data, OutError)) return false;
    FReader Reader(Data); if (!ReadHeader(Reader, AnimationSetMagic, OutError)) return false;
    std::string Skeleton, Idle, Walk, Jump;
    FAnimationSetData Value;
    if (!Reader.String(Skeleton) || !Reader.String(Idle) || !Reader.String(Walk)
        || !Reader.String(Jump) || Reader.Remaining() != 0
        || !FAssetPath::TryParse(Skeleton, Value.SkeletonAsset)
        || !FAssetPath::TryParse(Idle, Value.IdleAnimation)
        || !FAssetPath::TryParse(Walk, Value.WalkAnimation)
        || !FAssetPath::TryParse(Jump, Value.JumpAnimation))
    { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
    if (!ValidateAnimationSet(Value, OutError)) return false;
    OutAnimationSet = std::move(Value); return true;
}

bool SaveAnimationMontageToFile(const std::filesystem::path& FilePath,
    const FAnimationMontageData& Montage, ESkeletalAssetError* OutError)
{
    if (!ValidateAnimationMontage(Montage, OutError)) return false;
    FWriter Writer; WriteHeader(Writer, AnimationMontageMagic);
    if (!Writer.String(Montage.SkeletonAsset.ToString()) || !Writer.String(Montage.SlotName)) return false;
    Writer.Float(Montage.BlendInTime); Writer.Float(Montage.BlendOutTime);
    Writer.U32(static_cast<uint32>(Montage.Segments.size()));
    Writer.U32(static_cast<uint32>(Montage.Sections.size()));
    Writer.U32(static_cast<uint32>(Montage.Notifies.size()));
    for (const FAnimationMontageSegment& Segment : Montage.Segments)
    {
        if (!Writer.String(Segment.AnimationAsset.ToString())) return false;
        Writer.Float(Segment.StartTime); Writer.Float(Segment.EndTime); Writer.Float(Segment.PlayRate);
    }
    for (const FAnimationMontageSection& Section : Montage.Sections)
    {
        if (!Writer.String(Section.Name) || !Writer.String(Section.NextSection)) return false;
        Writer.Float(Section.StartTime);
    }
    for (const FAnimationNotifyData& Notify : Montage.Notifies)
    {
        if (!Writer.String(Notify.Name)) return false;
        Writer.Float(Notify.BeginTime); Writer.Float(Notify.EndTime);
    }
    return SaveBytes(FilePath, Writer.Take(), OutError);
}

bool LoadAnimationMontageFromFile(const std::filesystem::path& FilePath,
    FAnimationMontageData& OutMontage, ESkeletalAssetError* OutError)
{
    std::vector<uint8> Data; if (!LoadBytes(FilePath, Data, OutError)) return false;
    FReader Reader(Data); if (!ReadHeader(Reader, AnimationMontageMagic, OutError)) return false;
    std::string Skeleton; uint32 Segments = 0, Sections = 0, Notifies = 0;
    FAnimationMontageData Value;
    if (!Reader.String(Skeleton) || !Reader.String(Value.SlotName)
        || !Reader.Float(Value.BlendInTime) || !Reader.Float(Value.BlendOutTime)
        || !Reader.U32(Segments) || !Reader.U32(Sections) || !Reader.U32(Notifies)
        || Segments == 0 || Segments > MaxMontageSegments
        || Sections == 0 || Sections > MaxMontageSections || Notifies > MaxNotifies
        || !FAssetPath::TryParse(Skeleton, Value.SkeletonAsset))
    { Report(OutError, ESkeletalAssetError::InvalidArchive); return false; }
    Value.Segments.resize(Segments); Value.Sections.resize(Sections); Value.Notifies.resize(Notifies);
    for (FAnimationMontageSegment& Segment : Value.Segments)
    {
        std::string Animation;
        if (!Reader.String(Animation) || !Reader.Float(Segment.StartTime)
            || !Reader.Float(Segment.EndTime) || !Reader.Float(Segment.PlayRate)
            || !FAssetPath::TryParse(Animation, Segment.AnimationAsset)) return false;
    }
    for (FAnimationMontageSection& Section : Value.Sections)
        if (!Reader.String(Section.Name) || !Reader.String(Section.NextSection)
            || !Reader.Float(Section.StartTime)) return false;
    for (FAnimationNotifyData& Notify : Value.Notifies)
        if (!Reader.String(Notify.Name) || !Reader.Float(Notify.BeginTime)
            || !Reader.Float(Notify.EndTime)) return false;
    if (Reader.Remaining() != 0) { Report(OutError, ESkeletalAssetError::TrailingData); return false; }
    if (!ValidateAnimationMontage(Value, OutError)) return false;
    OutMontage = std::move(Value); return true;
}

bool BuildReferencePose(const FSkeletonData& Skeleton, FSkeletonPose& OutPose)
{
    if (!ValidateSkeleton(Skeleton)) return false;
    OutPose.LocalTransforms.clear(); OutPose.LocalTransforms.reserve(Skeleton.Bones.size());
    for (const FSkeletonBone& Bone : Skeleton.Bones) OutPose.LocalTransforms.push_back(Bone.ReferenceLocalPose);
    BuildComponentAndSkinning(Skeleton, OutPose); return true;
}

bool RebuildPoseFromLocalTransforms(const FSkeletonData& Skeleton, FSkeletonPose& InOutPose)
{
    if (!ValidateSkeleton(Skeleton)
        || InOutPose.LocalTransforms.size() != Skeleton.Bones.size())
    {
        return false;
    }
    for (const FTransform& Transform : InOutPose.LocalTransforms)
    {
        if (!Finite(Transform)) return false;
    }
    BuildComponentAndSkinning(Skeleton, InOutPose);
    return true;
}

bool SampleAnimationClip(const FSkeletonData& Skeleton, const FAnimationClipData& Clip,
    float Time, FSkeletonPose& OutPose)
{
    if (!ValidateSkeleton(Skeleton) || !ValidateAnimationClip(Clip, &Skeleton) || !Finite(Time)) return false;
    float SampleTime = Time;
    if (Clip.bLooping)
    {
        SampleTime = std::fmod(Time, Clip.Duration);
        if (SampleTime < 0.0f) SampleTime += Clip.Duration;
    }
    else SampleTime = std::clamp(Time, 0.0f, Clip.Duration);
    OutPose.LocalTransforms.clear(); OutPose.LocalTransforms.reserve(Skeleton.Bones.size());
    for (const FSkeletonBone& Bone : Skeleton.Bones) OutPose.LocalTransforms.push_back(Bone.ReferenceLocalPose);
    for (const FBoneAnimationTrack& Track : Clip.Tracks)
    {
        FTransform& Transform = OutPose.LocalTransforms[Track.BoneIndex];
        Transform.Translation = SampleKeys(Track.TranslationKeys, SampleTime, Transform.Translation, Lerp);
        Transform.Rotation = SampleKeys(Track.RotationKeys, SampleTime, Transform.Rotation, Nlerp);
        Transform.Scale = SampleKeys(Track.ScaleKeys, SampleTime, Transform.Scale, Lerp);
    }
    BuildComponentAndSkinning(Skeleton, OutPose); return true;
}

bool SkinSkeletalMesh(const FSkeletalMeshData& Mesh, const FSkeletonPose& Pose,
    FSkinnedMeshRenderData& OutRenderData)
{
    if (!ValidateSkeletalMesh(Mesh) || Pose.SkinningMatrices.empty()) return false;
    FSkinnedMeshRenderData Result;
    Result.Vertices.resize(Mesh.Vertices.size()); Result.Indices = Mesh.Indices;
    Result.Sections = Mesh.Sections;
    bool bFirst = true;
    for (std::size_t Index = 0; Index < Mesh.Vertices.size(); ++Index)
    {
        const FSkeletalMeshVertex& Source = Mesh.Vertices[Index];
        FVector3 Position = FVector3::ZeroVector;
        FVector3 Normal = FVector3::ZeroVector;
        for (std::size_t Influence = 0; Influence < 4; ++Influence)
        {
            const float Weight = Source.BoneWeights[Influence];
            const uint32 Bone = Source.BoneIndices[Influence];
            if (Weight <= 0.0f) continue;
            if (Bone >= Pose.SkinningMatrices.size()) return false;
            Position += Pose.SkinningMatrices[Bone].TransformPosition(Source.Position) * Weight;
            Normal += Pose.SkinningMatrices[Bone].TransformVector(Source.Normal) * Weight;
        }
        Result.Vertices[Index] = {Position, Normal.GetSafeNormal(), Source.TexCoord};
        if (bFirst) { Result.Bounds.Min = Position; Result.Bounds.Max = Position; bFirst = false; }
        else
        {
            Result.Bounds.Min.X = std::min(Result.Bounds.Min.X, Position.X);
            Result.Bounds.Min.Y = std::min(Result.Bounds.Min.Y, Position.Y);
            Result.Bounds.Min.Z = std::min(Result.Bounds.Min.Z, Position.Z);
            Result.Bounds.Max.X = std::max(Result.Bounds.Max.X, Position.X);
            Result.Bounds.Max.Y = std::max(Result.Bounds.Max.Y, Position.Y);
            Result.Bounds.Max.Z = std::max(Result.Bounds.Max.Z, Position.Z);
        }
    }
    Result.Revision = OutRenderData.Revision + 1;
    OutRenderData = std::move(Result); return true;
}

FTransform ExtractRootMotion(const FSkeletonData& Skeleton, const FAnimationClipData& Clip,
    float PreviousTime, float CurrentTime)
{
    if (Clip.RootBoneIndex < 0 || CurrentTime <= PreviousTime || !Finite(PreviousTime) || !Finite(CurrentTime))
        return FTransform::Identity;
    const FTransform Previous = SampleRootTransform(Skeleton, Clip, PreviousTime);
    const FTransform Current = SampleRootTransform(Skeleton, Clip, CurrentTime);
    if (!Clip.bLooping || static_cast<int64>(PreviousTime / Clip.Duration) == static_cast<int64>(CurrentTime / Clip.Duration))
        return Current.GetRelativeTransform(Previous);
    const FTransform End = SampleRootTransform(Skeleton, Clip, Clip.Duration);
    const FTransform Start = SampleRootTransform(Skeleton, Clip, 0.0f);
    const FTransform First = End.GetRelativeTransform(Previous);
    const FTransform Second = Current.GetRelativeTransform(Start);
    return FTransform(Second.Rotation * First.Rotation,
        First.Translation + Second.Translation, FVector3::OneVector);
}

std::string_view ToString(ESkeletalAssetError Error)
{
    switch (Error)
    {
    case ESkeletalAssetError::None: return "None";
    case ESkeletalAssetError::InvalidArgument: return "InvalidArgument";
    case ESkeletalAssetError::InvalidData: return "InvalidData";
    case ESkeletalAssetError::InvalidArchive: return "InvalidArchive";
    case ESkeletalAssetError::UnsupportedVersion: return "UnsupportedVersion";
    case ESkeletalAssetError::LimitExceeded: return "LimitExceeded";
    case ESkeletalAssetError::FileOpenFailed: return "FileOpenFailed";
    case ESkeletalAssetError::FileReadFailed: return "FileReadFailed";
    case ESkeletalAssetError::FileWriteFailed: return "FileWriteFailed";
    case ESkeletalAssetError::FileTooLarge: return "FileTooLarge";
    case ESkeletalAssetError::TrailingData: return "TrailingData";
    }
    return "Unknown";
}
}
