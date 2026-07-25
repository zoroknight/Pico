#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSerialization.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/Property.h"
#include "Pico/Samples/DemoCharacter.h"

#include <filesystem>
#include <iostream>

int main()
{
    using namespace Pico;

    std::cout << "[1] Initialize object system\n";
    if (!PObjectSystem::Init())
    {
        return 1;
    }

    std::cout << "[2] Register PDemoCharacter\n";
    if (!PDemoCharacter::RegisterClass())
    {
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "\n" << DumpClass(PDemoCharacter::StaticClass()) << '\n';
    std::cout << "[3] Create Player through NewObject\n";
    PDemoCharacter* Player = NewObject<PDemoCharacter>(nullptr, "Player");
    if (Player == nullptr)
    {
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "\n" << DumpObject(Player) << '\n';
    std::cout << "[4] Change Health through PProperty: 100 -> 75\n";
    const PProperty* HealthProperty = Player->GetClass()->FindProperty(FName("Health"));
    if (HealthProperty == nullptr || !HealthProperty->SetValue(Player, int32 { 75 }))
    {
        DestroyObject(Player);
        PObjectSystem::Shutdown();
        return 1;
    }

    const std::filesystem::path SaveDirectory = std::filesystem::current_path() / "Saved" / "ReflectionDemo";
    const std::filesystem::path SavePath = SaveDirectory / "Player.pobj";
    std::error_code FileError;
    std::filesystem::create_directories(SaveDirectory, FileError);

    std::cout << "[5] Save Player to " << SavePath.string() << '\n';
    EObjectSerializationError SerializationError = EObjectSerializationError::None;
    if (FileError || !SaveObjectToFile(SavePath, Player, &SerializationError))
    {
        std::cerr << "Save failed: " << ToString(SerializationError) << '\n';
        DestroyObject(Player);
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "[6] Destroy Player\n";
    DestroyObject(Player);

    std::cout << "[7] Load Player from its .pobj file\n";
    PObject* LoadedObject = LoadObjectFromFile(SavePath, nullptr, &SerializationError);
    auto* LoadedPlayer = static_cast<PDemoCharacter*>(LoadedObject);
    if (LoadedPlayer == nullptr)
    {
        std::cerr << "Load failed: " << ToString(SerializationError) << '\n';
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "[8] PostLoad observed Health=" << LoadedPlayer->GetHealthSeenInPostLoad() << '\n';
    std::cout << "\n" << DumpObject(LoadedPlayer) << '\n';

    const bool bSucceeded =
        LoadedPlayer->GetHealth() == 75
        && LoadedPlayer->GetHealthSeenInPostLoad() == 75;
    std::cout << "[9] Round trip " << (bSucceeded ? "succeeded" : "failed") << '\n';

    DestroyObject(LoadedPlayer);
    PObjectSystem::Shutdown();
    return bSucceeded ? 0 : 1;
}
