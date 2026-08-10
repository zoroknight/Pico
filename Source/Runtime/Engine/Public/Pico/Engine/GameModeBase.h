#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ObjectPtr.h"

namespace Pico
{
class PGameStateBase;
class PPawn;
class PPlayer;
class PPlayerController;
class PPlayerStart;
class PPlayerState;
class PController;

class PGameModeBase : public PActor
{
    PICO_DECLARE_CLASS(PGameModeBase, PActor)

public:
    const PClass* GetDefaultPawnClass() const;
    const PClass* GetPlayerControllerClass() const;
    const PClass* GetPlayerStateClass() const;
    const PClass* GetGameStateClass() const;
    PGameStateBase* GetGameState() const;

    PPlayerController* Login(PPlayer* NewPlayer);
    virtual void PostLogin(PPlayerController* NewPlayer);
    virtual bool HandleStartingNewPlayer(PPlayerController* NewPlayer);
    bool RestartPlayer(PController* NewPlayer);
    void Logout(PController* Exiting);

    bool SetDefaultPawnClass(const PClass* Class);
    bool SetPlayerControllerClass(const PClass* Class);
    bool SetPlayerStateClass(const PClass* Class);
    bool SetGameStateClass(const PClass* Class);

protected:
    explicit PGameModeBase(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    virtual PPlayerStart* ChoosePlayerStart(PController* Player);
    virtual PPawn* SpawnDefaultPawnFor(
        PController* Controller,
        PPlayerStart* StartSpot);
    virtual void OnLogout(PController* Exiting);

private:
    friend class PWorld;
    PGameStateBase* CreateGameState();
    const PGameModeBase* GetClassDefaults() const;
    bool CanEditClassDefaults() const;

    const PClass* DefaultPawnClass = nullptr;
    const PClass* PlayerControllerClass = nullptr;
    const PClass* PlayerStateClass = nullptr;
    const PClass* GameStateClass = nullptr;
    TObjectPtr<PGameStateBase> GameState;
    int32 NextPlayerId = 0;
};
}
