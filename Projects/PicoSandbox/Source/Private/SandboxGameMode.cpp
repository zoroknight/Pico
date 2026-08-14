#include "PicoSandbox/SandboxGameMode.h"

#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Paths.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/World.h"

#include <algorithm>
#include <array>
#include <memory>

namespace PicoSandbox
{
namespace
{
struct FSampleAnimationSet
{
    std::shared_ptr<Pico::FSkeletonData> Skeleton;
    std::shared_ptr<Pico::FSkeletalMeshData> Mesh;
    std::shared_ptr<Pico::FAnimationClipData> Idle;
    std::shared_ptr<Pico::FAnimationClipData> Walk;
    std::shared_ptr<Pico::FAnimationClipData> Jump;
};

void AddBox(
    Pico::FSkeletalMeshData& Mesh,
    const Pico::FVector3& Min,
    const Pico::FVector3& Max,
    Pico::uint32 Bone)
{
    const std::array<Pico::FVector3, 8> Corners = {{
        {Min.X, Min.Y, Min.Z}, {Max.X, Min.Y, Min.Z},
        {Max.X, Max.Y, Min.Z}, {Min.X, Max.Y, Min.Z},
        {Min.X, Min.Y, Max.Z}, {Max.X, Min.Y, Max.Z},
        {Max.X, Max.Y, Max.Z}, {Min.X, Max.Y, Max.Z}}};
    struct FFace { int A; int B; int C; int D; Pico::FVector3 Normal; };
    const std::array<FFace, 6> Faces = {{
        {0, 3, 2, 1, {0, 0, -1}}, {4, 5, 6, 7, {0, 0, 1}},
        {0, 1, 5, 4, {0, -1, 0}}, {1, 2, 6, 5, {1, 0, 0}},
        {2, 3, 7, 6, {0, 1, 0}}, {3, 0, 4, 7, {-1, 0, 0}}}};
    for (const FFace& Face : Faces)
    {
        const Pico::uint32 Base = static_cast<Pico::uint32>(Mesh.Vertices.size());
        for (int Corner : {Face.A, Face.B, Face.C, Face.D})
        {
            Pico::FSkeletalMeshVertex Vertex;
            Vertex.Position = Corners[static_cast<std::size_t>(Corner)];
            Vertex.Normal = Face.Normal;
            Vertex.BoneIndices[0] = Bone;
            Mesh.Vertices.push_back(Vertex);
        }
        Mesh.Indices.insert(Mesh.Indices.end(),
            {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }
}

std::shared_ptr<Pico::FAnimationClipData> MakeClip(
    const char* Name,
    float Angle,
    bool bLooping)
{
    auto Clip = std::make_shared<Pico::FAnimationClipData>();
    Pico::FAssetPath::TryParse("/Game/Characters/Sample.pskeleton", Clip->SkeletonAsset);
    Clip->Name = Name;
    Clip->Duration = 1.0f;
    Clip->bLooping = bLooping;
    Pico::FBoneAnimationTrack Upper;
    Upper.BoneIndex = 1;
    Upper.TranslationKeys = {{0.0f, {0, 0, 70}}, {1.0f, {0, 0, 70}}};
    Upper.RotationKeys = {
        {0.0f, Pico::FQuat::FromAxisAngle(Pico::FVector3::RightVector, -Angle)},
        {0.5f, Pico::FQuat::FromAxisAngle(Pico::FVector3::RightVector, Angle)},
        {1.0f, Pico::FQuat::FromAxisAngle(Pico::FVector3::RightVector, -Angle)}};
    Clip->Tracks.push_back(std::move(Upper));
    return Clip;
}

FSampleAnimationSet MakeSampleAnimationSet()
{
    FSampleAnimationSet Set;
    Set.Skeleton = std::make_shared<Pico::FSkeletonData>();
    Set.Skeleton->Bones = {
        {"Root", -1, Pico::FTransform::Identity, Pico::FMatrix4::Identity},
        {"Upper", 0, Pico::FTransform(Pico::FVector3(0, 0, 70)),
            Pico::FTransform(Pico::FVector3(0, 0, -70)).ToMatrix()}};
    Set.Mesh = std::make_shared<Pico::FSkeletalMeshData>();
    Pico::FAssetPath::TryParse("/Game/Characters/Sample.pskeleton", Set.Mesh->SkeletonAsset);
    AddBox(*Set.Mesh, {-28, -22, 0}, {28, 22, 70}, 0);
    AddBox(*Set.Mesh, {-22, -18, 70}, {22, 18, 135}, 1);
    Set.Mesh->Sections = {{0, static_cast<Pico::uint32>(Set.Mesh->Indices.size()), "Default"}};
    Set.Mesh->Bounds = {{-50, -50, 0}, {50, 50, 150}};
    Set.Idle = MakeClip("Idle", 0.08f, true);
    Set.Walk = MakeClip("Walk", 0.55f, true);
    Set.Jump = MakeClip("Jump", 0.9f, false);
    return Set;
}
}

PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxGameMode)

PSandboxGameMode::PSandboxGameMode(
    const Pico::FObjectConstructionParams& Params)
    : PGameModeBase(Params)
{
}

Pico::PPawn* PSandboxGameMode::SpawnDefaultPawnFor(
    Pico::PController* Controller,
    Pico::PPlayerStart* StartSpot)
{
    Pico::PPawn* Pawn = PGameModeBase::SpawnDefaultPawnFor(Controller, StartSpot);
    if (Pawn != nullptr)
    {
        Pico::FConfigFile Config;
        Config.Load(Pico::FPaths::GetProjectConfigFile("Pico.ini"));
        Pico::FAssetPath ProfilePath;
        const std::string ProfileText = Config.GetString("Game", "DefaultPawnProfile", "");
        const bool bHasProfile = !ProfileText.empty()
            && Pico::FAssetPath::TryParse(ProfileText, ProfilePath);
        const FSampleAnimationSet AnimationSet = bHasProfile
            ? FSampleAnimationSet {} : MakeSampleAnimationSet();
        for (Pico::PActorComponent* Component : Pawn->GetComponents())
        {
            if (bHasProfile && Component != nullptr
                && Component->IsA(Pico::PStaticMeshComponent::StaticClass()))
            {
                static_cast<Pico::PStaticMeshComponent*>(Component)->SetStaticMeshAsset({});
            }
            if (Component != nullptr
                && Component->IsA(Pico::PSkeletalMeshComponent::StaticClass()))
            {
                auto* SkeletalMesh = static_cast<Pico::PSkeletalMeshComponent*>(Component);
                if (bHasProfile && !SkeletalMesh->GetCharacterProfileAsset().IsValid())
                    SkeletalMesh->SetCharacterProfileAsset(ProfilePath);
                else if (!SkeletalMesh->GetCharacterProfileAsset().IsValid())
                    SkeletalMesh->SetRuntimeAnimationSet(
                        AnimationSet.Skeleton,
                        AnimationSet.Mesh,
                        AnimationSet.Idle,
                        AnimationSet.Walk,
                        AnimationSet.Jump);
            }
        }
    }
    Pico::PSceneComponent* Root = Pawn != nullptr ? Pawn->GetRootComponent() : nullptr;
    if (Root == nullptr || !Root->IsA(Pico::PCapsuleComponent::StaticClass()))
    {
        return Pawn;
    }

    const auto* Capsule = static_cast<Pico::PCapsuleComponent*>(Root);
    Pico::FTransform SpawnTransform = Pawn->GetActorTransform();
    SpawnTransform.Translation.Z = std::max(
        SpawnTransform.Translation.Z,
        Capsule->GetHalfHeight());
    Pawn->SetActorTransform(SpawnTransform);
    return Pawn;
}

void PSandboxGameMode::BeginPlay()
{
    PGameModeBase::BeginPlay();
}
}
