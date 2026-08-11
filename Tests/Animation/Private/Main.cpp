#include "TestRunner.h"

#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

namespace
{
struct FAnimationFixture
{
    Pico::FSkeletonData Skeleton;
    Pico::FSkeletalMeshData Mesh;
    Pico::FAnimationClipData Idle;
    Pico::FAnimationClipData Walk;
    Pico::FAnimationClipData Jump;
};

FAnimationFixture MakeFixture()
{
    FAnimationFixture Fixture;
    Pico::FAssetPath SkeletonPath;
    Pico::FAssetPath::TryParse("/Game/Characters/Test.pskeleton", SkeletonPath);
    Fixture.Skeleton.Bones = {
        {"Root", -1, Pico::FTransform::Identity, Pico::FMatrix4::Identity},
        {"Upper", 0, Pico::FTransform(Pico::FVector3(0.0f, 0.0f, 50.0f)),
            Pico::FTransform(Pico::FVector3(0.0f, 0.0f, -50.0f)).ToMatrix()}
    };
    Fixture.Mesh.Vertices = {
        {{-10.0f, 0.0f, 0.0f}, Pico::FVector3::ForwardVector, {}, {0, 0, 0, 0}, {1, 0, 0, 0}},
        {{10.0f, 0.0f, 0.0f}, Pico::FVector3::ForwardVector, {}, {0, 0, 0, 0}, {1, 0, 0, 0}},
        {{0.0f, 0.0f, 80.0f}, Pico::FVector3::ForwardVector, {}, {1, 0, 0, 0}, {1, 0, 0, 0}}
    };
    Fixture.Mesh.Indices = {0, 1, 2};
    Fixture.Mesh.Sections = {{0, 3, "Default"}};
    Fixture.Mesh.Bounds = {{-10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 80.0f}};
    Fixture.Mesh.SkeletonAsset = SkeletonPath;

    const auto MakeClip = [](const char* Name, bool bRootMotion)
    {
        Pico::FAnimationClipData Clip;
        Clip.Name = Name;
        Pico::FAssetPath::TryParse(
            "/Game/Characters/Test.pskeleton", Clip.SkeletonAsset);
        Clip.Duration = 1.0f;
        Clip.bLooping = true;
        Clip.RootBoneIndex = bRootMotion ? 0 : -1;
        Pico::FBoneAnimationTrack Root;
        Root.BoneIndex = 0;
        Root.TranslationKeys = {
            {0.0f, Pico::FVector3::ZeroVector},
            {1.0f, bRootMotion ? Pico::FVector3(100.0f, 0.0f, 0.0f) : Pico::FVector3::ZeroVector}};
        Pico::FBoneAnimationTrack Upper;
        Upper.BoneIndex = 1;
        Upper.TranslationKeys = {
            {0.0f, Pico::FVector3(0.0f, 0.0f, 50.0f)},
            {1.0f, Pico::FVector3(0.0f, 0.0f, 50.0f)}};
        Upper.RotationKeys = {
            {0.0f, Pico::FQuat::Identity},
            {0.5f, Pico::FQuat::FromAxisAngle(Pico::FVector3::RightVector, 0.5f)},
            {1.0f, Pico::FQuat::Identity}};
        Clip.Tracks = {Root, Upper};
        return Clip;
    };
    Fixture.Idle = MakeClip("Idle", false);
    Fixture.Walk = MakeClip("Walk", true);
    Fixture.Jump = MakeClip("Jump", false);
    Fixture.Jump.bLooping = false;
    return Fixture;
}
}

int main()
{
    FTestRunner Runner;
    const FAnimationFixture Fixture = MakeFixture();
    Runner.Expect(
        Pico::ValidateSkeleton(Fixture.Skeleton)
            && Pico::ValidateSkeletalMesh(Fixture.Mesh, &Fixture.Skeleton)
            && Pico::ValidateAnimationClip(Fixture.Walk, &Fixture.Skeleton),
        "Skeleton, weighted mesh and animation clip validate as one compatible set");

    Pico::FSkeletonPose ReferencePose;
    Pico::FSkeletonPose AnimatedPose;
    Pico::FSkinnedMeshRenderData RenderData;
    Runner.Expect(
        Pico::BuildReferencePose(Fixture.Skeleton, ReferencePose)
            && Pico::SampleAnimationClip(Fixture.Skeleton, Fixture.Walk, 0.5f, AnimatedPose)
            && Pico::SkinSkeletalMesh(Fixture.Mesh, AnimatedPose, RenderData)
            && RenderData.Vertices.size() == Fixture.Mesh.Vertices.size()
            && !RenderData.Vertices[2].Position.Equals(Fixture.Mesh.Vertices[2].Position),
        "Pose sampling drives weighted CPU skinning into renderer-neutral vertices");

    const Pico::FTransform RootDelta =
        Pico::ExtractRootMotion(Fixture.Skeleton, Fixture.Walk, 0.0f, 0.25f);
    Runner.Expect(
        RootDelta.Translation.Equals(Pico::FVector3(25.0f, 0.0f, 0.0f), 0.01f),
        "Root motion extraction returns the root-bone delta for the frame");

    const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("PicoAnimationTests_" + std::to_string(Unique));
    std::filesystem::create_directories(Root / "Content/Characters");
    Pico::FSkeletonData LoadedSkeleton;
    Pico::FSkeletalMeshData LoadedMesh;
    Pico::FAnimationClipData LoadedClip;
    Runner.Expect(
        Pico::SaveSkeletonToFile(Root / "Content/Characters/Test.pskeleton", Fixture.Skeleton)
            && Pico::SaveSkeletalMeshToFile(Root / "Content/Characters/Test.pskeletalmesh", Fixture.Mesh)
            && Pico::SaveAnimationClipToFile(Root / "Content/Characters/Idle.panimation", Fixture.Idle)
            && Pico::SaveAnimationClipToFile(Root / "Content/Characters/Walk.panimation", Fixture.Walk)
            && Pico::SaveAnimationClipToFile(Root / "Content/Characters/Jump.panimation", Fixture.Jump)
            && Pico::LoadSkeletonFromFile(Root / "Content/Characters/Test.pskeleton", LoadedSkeleton)
            && Pico::LoadSkeletalMeshFromFile(Root / "Content/Characters/Test.pskeletalmesh", LoadedMesh)
            && Pico::LoadAnimationClipFromFile(Root / "Content/Characters/Walk.panimation", LoadedClip)
            && LoadedSkeleton.Bones.size() == 2
            && LoadedMesh.Vertices.size() == 3
            && LoadedClip.Name == "Walk",
        "Native skeleton, skeletal mesh and animation files round trip from disk");
    {
        std::ofstream ProjectFile(Root / "AnimationTests.pico");
        ProjectFile << "[Project]\nName=AnimationTests\nFileVersion=1\nEngineVersion=0.1.0\n";
    }

    Pico::FEngineLoop EngineLoop;
    char Program[] = "PicoAnimationTests";
    char Frames[] = "-frames=-1";
    char* Arguments[] = {Program, Frames};
    const bool bEngineReady = EngineLoop.PreInit(2, Arguments, Root / "AnimationTests.pico") == 0
        && EngineLoop.Init() == 0;
    Runner.Expect(bEngineReady, "Animation tests initialize the reflected object runtime");
    if (bEngineReady)
    {
        Pico::PAnimInstance* Instance = Pico::NewObject<Pico::PAnimInstance>(
            EngineLoop.GetWorld(), "TestAnimInstance", Pico::EObjectFlags::Transient);
        Runner.Expect(Instance != nullptr, "AnimInstance is created through the unified object path");
        if (Instance == nullptr)
        {
            EngineLoop.Exit();
            return Runner.Finish();
        }
        Instance->SetAnimationSet(
            std::make_shared<Pico::FSkeletonData>(Fixture.Skeleton),
            std::make_shared<Pico::FAnimationClipData>(Fixture.Idle),
            std::make_shared<Pico::FAnimationClipData>(Fixture.Walk),
            std::make_shared<Pico::FAnimationClipData>(Fixture.Jump));
        Instance->Update(1.0f / 60.0f, 0.0f, false);
        const bool bIdle = Instance->GetAnimationState() == Pico::EAnimationState::Idle;
        Instance->Update(1.0f / 60.0f, 100.0f, false);
        const bool bWalk = Instance->GetAnimationState() == Pico::EAnimationState::Walk;
        Instance->SetExtractRootMotion(true);
        Instance->Update(0.25f, 100.0f, false);
        const bool bRootLocked = Instance->GetPose().LocalTransforms[0].Translation.IsNearlyZero()
            && !Instance->ConsumeExtractedRootMotion().Translation.IsNearlyZero();
        Instance->Update(1.0f / 60.0f, 100.0f, true);
        const bool bSeeked = Instance->SetPlaybackTime(0.5f)
            && std::abs(Instance->GetPlaybackTime() - 0.5f) < 0.0001f;
        Runner.Expect(
            bIdle && bWalk && bRootLocked && bSeeked
                && Instance->GetAnimationState() == Pico::EAnimationState::Jump,
            "AnimInstance selects states, supports preview seeking, and removes extracted Root Motion from the visual pose");

        Pico::PActor* Actor = EngineLoop.GetWorld()->SpawnActor<Pico::PActor>("AssetDrivenActor");
        Pico::PSkeletalMeshComponent* Component = Actor != nullptr
            ? Actor->CreateComponent<Pico::PSkeletalMeshComponent>("SkeletalMesh")
            : nullptr;
        Pico::FAssetPath MeshPath;
        Pico::FAssetPath IdlePath;
        Pico::FAssetPath WalkPath;
        Pico::FAssetPath JumpPath;
        Pico::FAssetPath::TryParse("/Game/Characters/Test.pskeletalmesh", MeshPath);
        Pico::FAssetPath::TryParse("/Game/Characters/Idle.panimation", IdlePath);
        Pico::FAssetPath::TryParse("/Game/Characters/Walk.panimation", WalkPath);
        Pico::FAssetPath::TryParse("/Game/Characters/Jump.panimation", JumpPath);
        const auto LoadedAssetMesh = EngineLoop.GetAssetManager().LoadSkeletalMesh(
            MeshPath, EngineLoop.GetAssetRegistry());
        const auto LoadedAssetSkeleton = LoadedAssetMesh != nullptr
            ? EngineLoop.GetAssetManager().LoadSkeleton(
                LoadedAssetMesh->SkeletonAsset, EngineLoop.GetAssetRegistry())
            : nullptr;
        Runner.Expect(
            LoadedAssetMesh != nullptr && LoadedAssetSkeleton != nullptr,
            "AssetManager resolves native skeletal mesh and referenced skeleton assets");
        if (Component != nullptr)
        {
            Component->RegisterComponent();
            Component->SetSkeletalMeshAsset(MeshPath);
            Component->SetIdleAnimationAsset(IdlePath);
            Component->SetWalkAnimationAsset(WalkPath);
            Component->SetJumpAnimationAsset(JumpPath);
            Component->TickComponent(1.0f / 60.0f);
            Component->SetAnimationPlaybackTime(0.5f);
        }
        Runner.Expect(
            Component != nullptr
                && !Component->GetRenderData().Vertices.empty()
                && Component->GetAnimInstance() != nullptr
                && std::abs(Component->GetAnimInstance()->GetPlaybackTime() - 0.5f) < 0.0001f,
            "SkeletalMeshComponent resolves asset paths and rebuilds skinned data after preview seeking");
        EngineLoop.Exit();
    }
    std::error_code ErrorCode;
    std::filesystem::remove_all(Root, ErrorCode);
    return Runner.Finish();
}
