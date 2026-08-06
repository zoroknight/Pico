#include "Pico/AssetImport/StaticMeshImporter.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/DirectionalLightComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/LightComponent.h"
#include "Pico/Engine/PointLightComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Object/ObjectSystem.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

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
}

int main(int Argc, char** Argv)
{
    if (Argc >= 2
        && std::string_view(Argv[1]) == "rewrite-world-asset-path")
    {
        return RewriteWorldAssetPath(Argc, Argv);
    }
    if (Argc != 4 || std::string_view(Argv[1]) != "import-obj")
    {
        std::cerr << "Usage:\n"
            "  PicoAssetTool import-obj <source.obj> <destination.pmesh>\n"
            "  PicoAssetTool rewrite-world-asset-path "
            "<world.pworld> <old-path> <new-path>\n";
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
