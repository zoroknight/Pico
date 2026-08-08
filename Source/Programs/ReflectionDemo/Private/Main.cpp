#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/ObjectSerialization.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/Property.h"
#include "Pico/Samples/DemoCharacter.h"

#include <array>
#include <filesystem>
#include <iostream>

int main()
{
    using namespace Pico;

    std::cout << "[Setup] Initialize object system\n";
    if (!PObjectSystem::Init())
    {
        return 1;
    }

    std::cout << "[Setup] Register demo classes\n";
    if (!PDemoCharacter::RegisterClass() || !PDemoHealthObserver::RegisterClass())
    {
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "\n=== Part 1: reflected serialization round trip ===\n";
    std::cout << "\n" << DumpClass(PDemoCharacter::StaticClass()) << '\n';
    std::cout << "[Serialization 1] Create Player through NewObject\n";
    PDemoCharacter* Player = NewObject<PDemoCharacter>(nullptr, "Player");
    if (Player == nullptr)
    {
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "\n" << DumpObject(Player) << '\n';
    std::cout << "[Serialization 2] Change Health through PProperty: 100 -> 75\n";
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

    std::cout << "[Serialization 3] Save Player to " << SavePath.string() << '\n';
    EObjectSerializationError SerializationError = EObjectSerializationError::None;
    if (FileError || !SaveObjectToFile(SavePath, Player, &SerializationError))
    {
        std::cerr << "Save failed: " << ToString(SerializationError) << '\n';
        DestroyObject(Player);
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "[Serialization 4] Destroy Player\n";
    DestroyObject(Player);

    std::cout << "[Serialization 5] Load Player from its .pobj file\n";
    PObject* LoadedObject = LoadObjectFromFile(SavePath, nullptr, &SerializationError);
    auto* LoadedPlayer = static_cast<PDemoCharacter*>(LoadedObject);
    if (LoadedPlayer == nullptr)
    {
        std::cerr << "Load failed: " << ToString(SerializationError) << '\n';
        PObjectSystem::Shutdown();
        return 1;
    }

    std::cout << "[Serialization 6] PostLoad observed Health="
              << LoadedPlayer->GetHealthSeenInPostLoad() << '\n';
    std::cout << "\n" << DumpObject(LoadedPlayer) << '\n';

    const bool bSerializationSucceeded =
        LoadedPlayer->GetHealth() == 75
        && LoadedPlayer->GetHealthSeenInPostLoad() == 75;
    std::cout << "[Serialization 7] Round trip "
              << (bSerializationSucceeded ? "succeeded" : "failed") << '\n';

    std::cout << "\n=== Part 2: PFunction invokes a Native Delegate event ===\n";
    const PFunction* ApplyDamageFunction =
        LoadedPlayer->GetClass()->FindFunction(FName("ApplyDamage"));
    std::cout << "[Events 1] FindFunction(ApplyDamage): "
              << (ApplyDamageFunction != nullptr ? "found" : "missing") << '\n';

    PDemoHealthObserver* Observer =
        NewObject<PDemoHealthObserver>(nullptr, "HealthObserver");
    int32 LambdaNotificationCount = 0;
    const FDelegateHandle LambdaHandle = LoadedPlayer->OnHealthChanged.AddLambda(
        [&LambdaNotificationCount](int32 OldHealth, int32 NewHealth)
        {
            ++LambdaNotificationCount;
            std::cout << "  Lambda listener: Health " << OldHealth
                      << " -> " << NewHealth << '\n';
        });
    const FDelegateHandle ObjectHandle = Observer != nullptr
        ? LoadedPlayer->OnHealthChanged.AddObject(
            Observer,
            &PDemoHealthObserver::HandleHealthChanged)
        : FDelegateHandle {};
    std::cout << "[Events 2] Bind lambda and weak object listeners: "
              << (LambdaHandle.IsValid() && ObjectHandle.IsValid() ? "succeeded" : "failed")
              << '\n';

    FFunctionValue ReturnValue;
    const std::array<FFunctionValue, 1> DamageArguments { int32 { 20 } };
    const EFunctionInvokeResult DamageResult =
        LoadedPlayer->ProcessEvent(ApplyDamageFunction, DamageArguments, &ReturnValue);
    const bool bFirstBroadcastSucceeded =
        DamageResult == EFunctionInvokeResult::Success
        && std::holds_alternative<int32>(ReturnValue)
        && std::get<int32>(ReturnValue) == 55
        && Observer != nullptr
        && Observer->GetNotificationCount() == 1
        && Observer->GetLastOldHealth() == 75
        && Observer->GetLastNewHealth() == 55
        && LambdaNotificationCount == 1;
    std::cout << "[Events 3] ProcessEvent ApplyDamage(20): Health="
              << LoadedPlayer->GetHealth() << ", object listener notifications="
              << (Observer != nullptr ? Observer->GetNotificationCount() : 0) << '\n';

    const std::array<FFunctionValue, 1> WrongArguments { 20.0f };
    const EFunctionInvokeResult WrongTypeResult =
        LoadedPlayer->ProcessEvent(ApplyDamageFunction, WrongArguments, &ReturnValue);
    const bool bWrongTypeRejected =
        WrongTypeResult == EFunctionInvokeResult::ArgumentTypeMismatch
        && LoadedPlayer->GetHealth() == 55
        && LambdaNotificationCount == 1;
    std::cout << "[Events 4] ProcessEvent ApplyDamage(Float): "
              << (bWrongTypeRejected ? "rejected with ArgumentTypeMismatch" : "unexpected result")
              << '\n';

    std::cout << "[Events 5] Destroy weak object listener, then damage again\n";
    if (Observer != nullptr)
    {
        DestroyObject(Observer);
        Observer = nullptr;
    }
    const std::array<FFunctionValue, 1> DamageAfterDestroyArguments { int32 { 5 } };
    const EFunctionInvokeResult AfterDestroyResult = LoadedPlayer->ProcessEvent(
        ApplyDamageFunction,
        DamageAfterDestroyArguments,
        &ReturnValue);
    const bool bWeakBindingExpired =
        AfterDestroyResult == EFunctionInvokeResult::Success
        && LoadedPlayer->GetHealth() == 50
        && LambdaNotificationCount == 2
        && LoadedPlayer->OnHealthChanged.Num() == 1;
    std::cout << "  Remaining live listeners=" << LoadedPlayer->OnHealthChanged.Num()
              << " (the destroyed object listener was skipped)\n";

    const bool bLambdaRemoved =
        LoadedPlayer->OnHealthChanged.Remove(LambdaHandle)
        && LoadedPlayer->OnHealthChanged.Num() == 0;
    const bool bEventDemoSucceeded =
        ApplyDamageFunction != nullptr
        && LambdaHandle.IsValid()
        && ObjectHandle.IsValid()
        && bFirstBroadcastSucceeded
        && bWrongTypeRejected
        && bWeakBindingExpired
        && bLambdaRemoved;
    std::cout << "[Events 6] PFunction + Delegate demo "
              << (bEventDemoSucceeded ? "succeeded" : "failed") << '\n';

    DestroyObject(LoadedPlayer);
    PObjectSystem::Shutdown();
    const bool bSucceeded = bSerializationSucceeded && bEventDemoSucceeded;
    std::cout << "\n[Result] All reflection demonstrations "
              << (bSucceeded ? "succeeded" : "failed") << '\n';
    return bSucceeded ? 0 : 1;
}
