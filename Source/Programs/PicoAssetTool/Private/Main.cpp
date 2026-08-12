#include "Pico/AssetImport/StaticMeshImporter.h"
#include "Pico/AssetImport/SkeletalAnimationImporter.h"
#include "Pico/Asset/Material.h"
#include "Pico/Asset/StaticMesh.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/DirectionalLightComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/LightComponent.h"
#include "Pico/Engine/PointLightComponent.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/ObjectGlobals.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <array>

namespace
{
bool RegisterEngineClasses()
{
    return Pico::PObjectSystem::Init()
        && Pico::PActorComponent::RegisterClass()
        && Pico::PSceneComponent::RegisterClass()
        && Pico::PCameraComponent::RegisterClass()
        && Pico::PLightComponent::RegisterClass()
        && Pico::PDirectionalLightComponent::RegisterClass()
        && Pico::PPointLightComponent::RegisterClass()
        && Pico::PSpringArmComponent::RegisterClass()
        && Pico::PPrimitiveComponent::RegisterClass()
        && Pico::PCubeComponent::RegisterClass()
        && Pico::PStaticMeshComponent::RegisterClass()
        && Pico::PActor::RegisterClass()
        && Pico::PPlayerStart::RegisterClass()
        && Pico::PLevel::RegisterClass()
        && Pico::PWorld::RegisterClass();
}

int RewriteWorldAssetPath(int Argc, char** Argv)
{
    if (Argc != 5)
    {
        std::cerr << "Usage: PicoAssetTool rewrite-world-asset-path "
            "<world.pworld> <old-path> <new-path>\n";
        return 1;
    }
    Pico::FAssetPath OldPath;
    Pico::FAssetPath NewPath;
    if (!Pico::FAssetPath::TryParse(Argv[3], OldPath)
        || !Pico::FAssetPath::TryParse(Argv[4], NewPath))
    {
        std::cerr << "Old and new paths must be valid /Game asset paths\n";
        return 1;
    }
    if (!RegisterEngineClasses())
    {
        std::cerr << "Could not initialize engine reflection\n";
        Pico::PObjectSystem::Shutdown();
        return 1;
    }

    Pico::FWorldAssetData Data;
    Pico::EWorldSerializationError Error = Pico::EWorldSerializationError::None;
    if (!Pico::LoadWorldAssetDataFromFile(Argv[2], Data, &Error))
    {
        std::cerr << "Could not load World: " << Pico::ToString(Error) << '\n';
        Pico::PObjectSystem::Shutdown();
        return 1;
    }
    std::size_t ReplacedCount = 0;
    for (Pico::FSceneObjectRecord& Object : Data.Objects)
    {
        for (Pico::FSerializedPropertyRecord& Property : Object.Properties)
        {
            if (Property.Type == Pico::EPropertyType::AssetPath
                && Property.AssetPathValue == OldPath)
            {
                Property.AssetPathValue = NewPath;
                ++ReplacedCount;
            }
        }
    }
    if (!Pico::SaveWorldAssetDataToFile(Argv[2], Data, &Error))
    {
        std::cerr << "Could not save World: " << Pico::ToString(Error) << '\n';
        Pico::PObjectSystem::Shutdown();
        return 1;
    }
    Pico::PObjectSystem::Shutdown();
    std::cout << "Replaced " << ReplacedCount << " World asset reference(s)\n";
    return 0;
}

int ImportSkeletal(int Argc, char** Argv)
{
    if (Argc != 7)
    {
        std::cerr << "Usage: PicoAssetTool import-skeletal <source.gltf|glb|fbx> "
            "</Game/path.pskeleton> <destination.pskeleton> "
            "<destination.pskeletalmesh> <animation-directory>\n";
        return 1;
    }
    Pico::FAssetPath SkeletonAssetPath;
    if (!Pico::FAssetPath::TryParse(Argv[3], SkeletonAssetPath))
    {
        std::cerr << "Skeleton asset path must be a valid /Game path\n";
        return 1;
    }
    Pico::FSkeletalImportResult Result;
    Pico::ESkeletalImportError Error = Pico::ESkeletalImportError::None;
    if (!Pico::ImportSkeletalAnimation(Argv[2], SkeletonAssetPath, {}, Result, &Error))
    {
        std::cerr << "Skeletal import failed: " << Pico::ToString(Error) << '\n';
        return 1;
    }
    if (!Pico::SaveSkeletalImportResult(Result, Argv[4], Argv[5], Argv[6], &Error))
    {
        std::cerr << "Could not save skeletal assets: " << Pico::ToString(Error) << '\n';
        return 1;
    }
    std::cout << "Imported skeleton, mesh, " << Result.Animations.size()
        << " animation clip(s), " << Result.Materials.size()
        << " material(s), and " << Result.Textures.size() << " texture(s)\n";
    for (const std::string& Warning : Result.Warnings)
    {
        std::cout << "Warning: " << Warning << '\n';
    }
    return 0;
}

Pico::FStaticMeshData MakeCubeMesh()
{
    Pico::FStaticMeshData Mesh;
    struct FFace
    {
        Pico::FVector3 Normal;
        std::array<Pico::FVector3, 4> Positions;
    };
    const std::array<FFace, 6> Faces {{
        {{ 1, 0, 0}, {{{50,-50,-50}, {50,50,-50}, {50,50,50}, {50,-50,50}}}},
        {{-1, 0, 0}, {{{-50,50,-50}, {-50,-50,-50}, {-50,-50,50}, {-50,50,50}}}},
        {{0,  1, 0}, {{{50,50,-50}, {-50,50,-50}, {-50,50,50}, {50,50,50}}}},
        {{0, -1, 0}, {{{-50,-50,-50}, {50,-50,-50}, {50,-50,50}, {-50,-50,50}}}},
        {{0, 0,  1}, {{{-50,-50,50}, {50,-50,50}, {50,50,50}, {-50,50,50}}}},
        {{0, 0, -1}, {{{-50,50,-50}, {50,50,-50}, {50,-50,-50}, {-50,-50,-50}}}}
    }};
    const std::array<Pico::FVector2, 4> UVs {{{0,0}, {1,0}, {1,1}, {0,1}}};
    for (const FFace& Face : Faces)
    {
        const Pico::uint32 Base = static_cast<Pico::uint32>(Mesh.Vertices.size());
        for (std::size_t Index = 0; Index < 4; ++Index)
            Mesh.Vertices.push_back({Face.Positions[Index], Face.Normal, UVs[Index]});
        Mesh.Indices.insert(Mesh.Indices.end(),
            {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }
    Mesh.Sections = {{0, static_cast<Pico::uint32>(Mesh.Indices.size()), "Default"}};
    Mesh.Bounds = {{-50,-50,-50}, {50,50,50}};
    return Mesh;
}

int CreateStarterContent(int Argc, char** Argv)
{
    if (Argc != 3)
    {
        std::cerr << "Usage: PicoAssetTool create-starter-content <project-root>\n";
        return 1;
    }
    const std::filesystem::path ProjectRoot = std::filesystem::absolute(Argv[2]);
    const std::filesystem::path Content = ProjectRoot / "Content";
    const std::filesystem::path MeshPath = Content / "StarterContent/Meshes/SM_Cube.pmesh";
    const std::filesystem::path GroundMaterialPath =
        Content / "StarterContent/Materials/M_Ground.pmat";
    const std::filesystem::path WallMaterialPath =
        Content / "StarterContent/Materials/M_Wall.pmat";
    const std::filesystem::path AccentMaterialPath =
        Content / "StarterContent/Materials/M_Accent.pmat";
    Pico::FMaterialData GroundMaterial;
    GroundMaterial.BaseColor = {0.22f, 0.28f, 0.24f};
    GroundMaterial.Roughness = 0.9f;
    Pico::FMaterialData WallMaterial;
    WallMaterial.BaseColor = {0.32f, 0.38f, 0.44f};
    WallMaterial.Roughness = 0.72f;
    Pico::FMaterialData AccentMaterial;
    AccentMaterial.BaseColor = {0.78f, 0.24f, 0.12f};
    AccentMaterial.Metallic = 0.08f;
    AccentMaterial.Roughness = 0.42f;
    if (!Pico::SaveStaticMeshToFile(MeshPath, MakeCubeMesh())
        || !Pico::SaveMaterialToFile(GroundMaterialPath, GroundMaterial)
        || !Pico::SaveMaterialToFile(WallMaterialPath, WallMaterial)
        || !Pico::SaveMaterialToFile(AccentMaterialPath, AccentMaterial)
        || !RegisterEngineClasses())
    {
        std::cerr << "Could not create Starter Content assets\n";
        Pico::PObjectSystem::Shutdown();
        return 1;
    }

    Pico::PWorld* World = Pico::NewObject<Pico::PWorld>(nullptr, "StarterWorld");
    if (World == nullptr || !World->Initialize())
    {
        std::cerr << "Could not initialize Starter World\n";
        Pico::PObjectSystem::Shutdown();
        return 1;
    }
    Pico::FAssetPath CubeAsset;
    Pico::FAssetPath GroundMaterialAsset;
    Pico::FAssetPath WallMaterialAsset;
    Pico::FAssetPath AccentMaterialAsset;
    Pico::FAssetPath::TryParse("/Game/StarterContent/Meshes/SM_Cube.pmesh", CubeAsset);
    Pico::FAssetPath::TryParse("/Game/StarterContent/Materials/M_Ground.pmat", GroundMaterialAsset);
    Pico::FAssetPath::TryParse("/Game/StarterContent/Materials/M_Wall.pmat", WallMaterialAsset);
    Pico::FAssetPath::TryParse("/Game/StarterContent/Materials/M_Accent.pmat", AccentMaterialAsset);
    const auto AddBlock = [World, &CubeAsset](
        const char* Name, const Pico::FVector3& Location, const Pico::FVector3& Scale,
        const Pico::FAssetPath& Material, Pico::EPhysicsBodyType BodyType)
    {
        Pico::PActor* Actor = World->SpawnActor<Pico::PActor>(Name);
        auto* Collision = Actor != nullptr
            ? Actor->CreateComponent<Pico::PCubeComponent>("Collision") : nullptr;
        auto* Mesh = Actor != nullptr
            ? Actor->CreateComponent<Pico::PStaticMeshComponent>("StaticMeshComponent") : nullptr;
        if (Actor == nullptr || Collision == nullptr || Mesh == nullptr
            || !Actor->SetRootComponent(Collision)
            || !Mesh->AttachToComponent(
                Collision, Pico::EAttachmentTransformRule::KeepRelative)) return false;
        Collision->SetVisible(false);
        Collision->SetExtent({50,50,50});
        Collision->SetCollisionEnabled(Pico::ECollisionEnabled::QueryAndPhysics);
        Collision->SetPhysicsBodyType(BodyType);
        Collision->SetSimulatePhysics(BodyType == Pico::EPhysicsBodyType::Dynamic);
        Mesh->SetStaticMeshAsset(CubeAsset);
        Mesh->SetMaterialAsset(Material);
        return Actor->SetActorTransform(Pico::FTransform(Pico::FRotator {}, Location, Scale));
    };
    bool bSuccess =
        AddBlock("Floor", {0,0,-25}, {12,12,0.5f}, GroundMaterialAsset, Pico::EPhysicsBodyType::Static)
        && AddBlock("WallNorth", {0,575,125}, {12,0.5f,3}, WallMaterialAsset, Pico::EPhysicsBodyType::Static)
        && AddBlock("WallEast", {575,0,125}, {0.5f,12,3}, WallMaterialAsset, Pico::EPhysicsBodyType::Static)
        && AddBlock("WallWest", {-575,0,125}, {0.5f,12,3}, WallMaterialAsset, Pico::EPhysicsBodyType::Static)
        && AddBlock("PhysicsCrate", {180,0,160}, {0.9f,0.9f,0.9f}, AccentMaterialAsset, Pico::EPhysicsBodyType::Dynamic);
    Pico::PPlayerStart* Start = World->SpawnActor<Pico::PPlayerStart>("PlayerStart");
    if (Start != nullptr) bSuccess = Start->SetActorLocation({0,0,110}) && bSuccess;
    Pico::PActor* SunActor = World->SpawnActor<Pico::PActor>("DirectionalLight");
    auto* Sun = SunActor != nullptr
        ? SunActor->CreateComponent<Pico::PDirectionalLightComponent>("DirectionalLightComponent") : nullptr;
    if (Sun != nullptr && SunActor->SetRootComponent(Sun))
    {
        Sun->SetIntensity(2.5f);
        SunActor->SetActorRotation({-45, -35, 0});
    }
    else bSuccess = false;
    Pico::PActor* FillActor = World->SpawnActor<Pico::PActor>("PointLight");
    auto* Fill = FillActor != nullptr
        ? FillActor->CreateComponent<Pico::PPointLightComponent>("PointLightComponent") : nullptr;
    if (Fill != nullptr && FillActor->SetRootComponent(Fill))
    {
        Fill->SetIntensity(8.0f);
        Fill->SetAttenuationRadius(900.0f);
        FillActor->SetActorLocation({-250,-200,350});
    }
    else bSuccess = false;
    Pico::EWorldSerializationError Error = Pico::EWorldSerializationError::None;
    const std::filesystem::path WorldPath = Content / "Maps/StarterWorld.pworld";
    bSuccess = bSuccess && Pico::SaveWorldToFile(WorldPath, *World, &Error);
    Pico::DestroyObjectTree(World);
    Pico::PObjectSystem::Shutdown();
    if (!bSuccess)
    {
        std::cerr << "Could not save Starter World: " << Pico::ToString(Error) << '\n';
        return 1;
    }
    std::cout << "Created Starter Content and " << WorldPath.string() << '\n';
    return 0;
}
}

int main(int Argc, char** Argv)
{
    if (Argc >= 2
        && std::string_view(Argv[1]) == "rewrite-world-asset-path")
    {
        return RewriteWorldAssetPath(Argc, Argv);
    }
    if (Argc >= 2 && std::string_view(Argv[1]) == "import-skeletal")
    {
        return ImportSkeletal(Argc, Argv);
    }
    if (Argc >= 2 && std::string_view(Argv[1]) == "create-starter-content")
    {
        return CreateStarterContent(Argc, Argv);
    }
    if (Argc != 4 || std::string_view(Argv[1]) != "import-obj")
    {
        std::cerr << "Usage:\n"
            "  PicoAssetTool import-obj <source.obj> <destination.pmesh>\n"
            "  PicoAssetTool import-skeletal <source.gltf|glb|fbx> "
            "</Game/path.pskeleton> <destination.pskeleton> "
            "<destination.pskeletalmesh> <animation-directory>\n"
            "  PicoAssetTool rewrite-world-asset-path "
            "<world.pworld> <old-path> <new-path>\n"
            "  PicoAssetTool create-starter-content <project-root>\n";
        return 1;
    }
    const std::filesystem::path Destination(Argv[3]);
    if (Destination.extension() != ".pmesh")
    {
        std::cerr << "Destination must use the .pmesh extension\n";
        return 1;
    }
    Pico::EStaticMeshImportError Error = Pico::EStaticMeshImportError::None;
    if (!Pico::ImportObjStaticMeshToFile(Argv[2], Destination, {}, &Error))
    {
        std::cerr << "Import failed: " << Pico::ToString(Error) << '\n';
        return 1;
    }
    std::cout << "Imported " << Argv[2] << " -> " << Destination.string() << '\n';
    return 0;
}
