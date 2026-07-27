#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/ObjectSerialization.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace Pico
{
class PObject;
}

namespace PicoSandbox
{
class PSandboxCharacter;

enum class ESandboxStep
{
    Initialized,
    Registered,
    Created,
    Modified,
    Saved,
    Destroyed,
    Loaded,
    Verified,
    Count
};

enum class ESandboxStepState
{
    NotStarted,
    Succeeded,
    Failed
};

struct FSandboxSnapshot
{
    Pico::int32 EntityId = 0;
    bool bEnabled = false;
    Pico::int32 Health = 0;
    float MoveSpeed = 0.0f;
    bool bAlive = false;
    Pico::FVector3 Velocity;
    Pico::FRotator ViewRotation;
    Pico::FTransform Transform;
    Pico::int32 HealthSeenInPostLoad = 0;
    Pico::FTransform TransformSeenInPostLoad;
};

class FSandboxSession
{
public:
    explicit FSandboxSession(std::filesystem::path InObjectPath = GetDefaultObjectPath());
    ~FSandboxSession();

    FSandboxSession(const FSandboxSession&) = delete;
    FSandboxSession& operator=(const FSandboxSession&) = delete;

    bool Initialize();
    bool Create();
    bool ApplyDemoChanges();
    bool Save();
    bool Destroy();
    bool Load();
    bool VerifyLoaded();
    bool RunFullWorkflow();
    void Shutdown();

    void MarkObjectModified(std::string Message);

    bool IsInitialized() const;
    PSandboxCharacter* GetObject() const;
    FSandboxSnapshot GetSnapshot() const;
    ESandboxStepState GetStepState(ESandboxStep Step) const;
    const std::filesystem::path& GetObjectPath() const;
    const std::string& GetLastMessage() const;
    Pico::EObjectSerializationError GetLastSerializationError() const;

    static std::filesystem::path GetSandboxRootDirectory();
    static std::filesystem::path GetDefaultObjectPath();
    static std::string_view GetStepName(ESandboxStep Step);

private:
    void ResetWorkflowStates();
    void SetStep(ESandboxStep Step, ESandboxStepState State, std::string Message);

    std::filesystem::path ObjectPath;
    PSandboxCharacter* Object = nullptr;
    std::array<ESandboxStepState, static_cast<std::size_t>(ESandboxStep::Count)> StepStates {};
    std::string LastMessage;
    Pico::EObjectSerializationError LastSerializationError = Pico::EObjectSerializationError::None;
    bool bInitialized = false;
    bool bOwnsObjectSystem = false;
};
}
