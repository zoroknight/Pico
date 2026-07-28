#include "PicoSandbox/SandboxSession.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/Property.h"
#include "Pico/Core/Paths.h"
#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxModule.h"

#include <cmath>
#include <system_error>
#include <utility>

namespace PicoSandbox
{
namespace
{
#ifndef PICO_SANDBOX_PROJECT_FILE
#define PICO_SANDBOX_PROJECT_FILE ""
#endif

template <typename TValue>
bool SetReflectedValue(Pico::PObject* Object, const char* PropertyName, TValue Value)
{
    const Pico::PProperty* Property =
        Object != nullptr
            ? Object->GetClass()->FindProperty(Pico::FName(PropertyName))
            : nullptr;
    return Property != nullptr && Property->SetValue(Object, Value);
}
}

FSandboxSession::FSandboxSession(std::filesystem::path InObjectPath)
    : ObjectPath(std::move(InObjectPath))
{
    StepStates.fill(ESandboxStepState::NotStarted);
}

FSandboxSession::~FSandboxSession()
{
    Shutdown();
}

bool FSandboxSession::Initialize()
{
    if (!bInitialized)
    {
        bOwnsObjectSystem = !Pico::PObjectSystem::IsInitialized();
        if (bOwnsObjectSystem && !Pico::PObjectSystem::Init())
        {
            SetStep(ESandboxStep::Initialized, ESandboxStepState::Failed, "Object system initialization failed");
            return false;
        }

        bInitialized = Pico::PObjectSystem::IsInitialized();
        if (!bInitialized)
        {
            SetStep(ESandboxStep::Initialized, ESandboxStepState::Failed, "Object system is unavailable");
            return false;
        }
    }

    SetStep(ESandboxStep::Initialized, ESandboxStepState::Succeeded, "Object system initialized");
    if (!RegisterSandboxClasses())
    {
        SetStep(ESandboxStep::Registered, ESandboxStepState::Failed, "Sandbox class registration failed");
        return false;
    }

    SetStep(ESandboxStep::Registered, ESandboxStepState::Succeeded, "Sandbox classes registered");
    return true;
}

bool FSandboxSession::Create()
{
    if (!Initialize())
    {
        return false;
    }

    if (Object != nullptr)
    {
        SetStep(ESandboxStep::Created, ESandboxStepState::Failed, "Destroy the current object before creating another");
        return false;
    }

    Object = Pico::NewObject<PSandboxCharacter>(nullptr, "SandboxHero");
    if (Object == nullptr)
    {
        SetStep(ESandboxStep::Created, ESandboxStepState::Failed, "SandboxHero creation failed");
        return false;
    }

    SetStep(ESandboxStep::Created, ESandboxStepState::Succeeded, "Created SandboxHero");
    return true;
}

bool FSandboxSession::ApplyDemoChanges()
{
    const bool bChanged =
        SetReflectedValue(Object, "EntityId", Pico::int32 { 2002 })
        && SetReflectedValue(Object, "bEnabled", true)
        && SetReflectedValue(Object, "Health", Pico::int32 { 75 })
        && SetReflectedValue(Object, "MoveSpeed", 720.0f)
        && SetReflectedValue(Object, "bAlive", false)
        && SetReflectedValue(Object, "Velocity", Pico::FVector3(100.0f, 0.0f, 25.0f))
        && SetReflectedValue(Object, "ViewRotation", Pico::FRotator(5.0f, 90.0f, 0.0f))
        && SetReflectedValue(
            Object,
            "Transform",
            Pico::FTransform(
                Pico::FRotator(10.0f, 45.0f, 0.0f),
                Pico::FVector3(120.0f, 30.0f, 10.0f),
                Pico::FVector3(1.5f, 1.0f, 1.0f)));

    SetStep(
        ESandboxStep::Modified,
        bChanged ? ESandboxStepState::Succeeded : ESandboxStepState::Failed,
        bChanged ? "Applied reflected property changes" : "Reflected property changes failed");
    return bChanged;
}

bool FSandboxSession::Save()
{
    if (Object == nullptr)
    {
        SetStep(ESandboxStep::Saved, ESandboxStepState::Failed, "Create an object before saving");
        return false;
    }

    if (!Pico::FPaths::IsProjectWritePath(ObjectPath))
    {
        SetStep(
            ESandboxStep::Saved,
            ESandboxStepState::Failed,
            "Save path is outside project Content, Intermediate, and Saved directories");
        return false;
    }

    std::error_code FileError;
    if (!ObjectPath.parent_path().empty())
    {
        std::filesystem::create_directories(ObjectPath.parent_path(), FileError);
    }

    LastSerializationError = Pico::EObjectSerializationError::None;
    const bool bSaved =
        !FileError
        && Pico::SaveObjectToFile(ObjectPath, Object, &LastSerializationError);
    SetStep(
        ESandboxStep::Saved,
        bSaved ? ESandboxStepState::Succeeded : ESandboxStepState::Failed,
        bSaved
            ? "Saved " + ObjectPath.string()
            : "Save failed: " + std::string(Pico::ToString(LastSerializationError)));
    return bSaved;
}

bool FSandboxSession::Destroy()
{
    if (Object == nullptr)
    {
        SetStep(ESandboxStep::Destroyed, ESandboxStepState::Failed, "There is no live object to destroy");
        return false;
    }

    const bool bDestroyed = Pico::DestroyObject(Object);
    if (bDestroyed)
    {
        Object = nullptr;
    }

    SetStep(
        ESandboxStep::Destroyed,
        bDestroyed ? ESandboxStepState::Succeeded : ESandboxStepState::Failed,
        bDestroyed ? "Destroyed the in-memory object" : "Object destruction failed");
    return bDestroyed;
}

bool FSandboxSession::Load()
{
    if (!Initialize())
    {
        return false;
    }

    if (Object != nullptr)
    {
        SetStep(ESandboxStep::Loaded, ESandboxStepState::Failed, "Destroy the current object before loading");
        return false;
    }

    LastSerializationError = Pico::EObjectSerializationError::None;
    Pico::PObject* LoadedObject =
        Pico::LoadObjectFromFile(ObjectPath, nullptr, &LastSerializationError);
    if (LoadedObject == nullptr || LoadedObject->GetClass() != PSandboxCharacter::StaticClass())
    {
        if (LoadedObject != nullptr)
        {
            Pico::DestroyObject(LoadedObject);
        }
        SetStep(
            ESandboxStep::Loaded,
            ESandboxStepState::Failed,
            "Load failed: " + std::string(Pico::ToString(LastSerializationError)));
        return false;
    }

    Object = static_cast<PSandboxCharacter*>(LoadedObject);
    SetStep(ESandboxStep::Loaded, ESandboxStepState::Succeeded, "Loaded SandboxHero from disk");
    return true;
}

bool FSandboxSession::VerifyLoaded()
{
    const FSandboxSnapshot Snapshot = GetSnapshot();
    const bool bVerified =
        Object != nullptr
        && Object->GetName() == Pico::FName("SandboxHero")
        && Snapshot.EntityId == 2002
        && Snapshot.bEnabled
        && Snapshot.Health == 75
        && std::abs(Snapshot.MoveSpeed - 720.0f) < 0.001f
        && !Snapshot.bAlive
        && Snapshot.Velocity.Equals(Pico::FVector3(100.0f, 0.0f, 25.0f))
        && Snapshot.ViewRotation.Equals(Pico::FRotator(5.0f, 90.0f, 0.0f))
        && Snapshot.Transform.Equals(
            Pico::FTransform(
                Pico::FRotator(10.0f, 45.0f, 0.0f),
                Pico::FVector3(120.0f, 30.0f, 10.0f),
                Pico::FVector3(1.5f, 1.0f, 1.0f)),
            0.001f)
        && Snapshot.HealthSeenInPostLoad == 75
        && Snapshot.TransformSeenInPostLoad.Equals(Snapshot.Transform, 0.001f);

    SetStep(
        ESandboxStep::Verified,
        bVerified ? ESandboxStepState::Succeeded : ESandboxStepState::Failed,
        bVerified ? "Loaded values match the saved reflected state" : "Loaded values did not match");
    return bVerified;
}

bool FSandboxSession::RunFullWorkflow()
{
    if (Object != nullptr && !Pico::DestroyObject(Object))
    {
        LastMessage = "Could not clear the current object";
        return false;
    }
    Object = nullptr;

    ResetWorkflowStates();
    return Initialize()
        && Create()
        && ApplyDemoChanges()
        && Save()
        && Destroy()
        && Load()
        && VerifyLoaded();
}

void FSandboxSession::Shutdown()
{
    if (Object != nullptr && Pico::PObjectSystem::IsInitialized())
    {
        Pico::DestroyObject(Object);
        Object = nullptr;
    }

    if (bOwnsObjectSystem && Pico::PObjectSystem::IsInitialized())
    {
        Pico::PObjectSystem::Shutdown();
    }

    bInitialized = false;
    bOwnsObjectSystem = false;
    ResetWorkflowStates();
}

void FSandboxSession::MarkObjectModified(std::string Message)
{
    SetStep(ESandboxStep::Modified, ESandboxStepState::Succeeded, std::move(Message));
}

bool FSandboxSession::IsInitialized() const
{
    return bInitialized;
}

PSandboxCharacter* FSandboxSession::GetObject() const
{
    return Object;
}

FSandboxSnapshot FSandboxSession::GetSnapshot() const
{
    if (Object == nullptr)
    {
        return {};
    }

    return {
        Object->GetEntityId(),
        Object->IsEnabled(),
        Object->GetHealth(),
        Object->GetMoveSpeed(),
        Object->IsAlive(),
        Object->GetVelocity(),
        Object->GetViewRotation(),
        Object->GetTransform(),
        Object->GetHealthSeenInPostLoad(),
        Object->GetTransformSeenInPostLoad()
    };
}

ESandboxStepState FSandboxSession::GetStepState(ESandboxStep Step) const
{
    return StepStates[static_cast<std::size_t>(Step)];
}

const std::filesystem::path& FSandboxSession::GetObjectPath() const
{
    return ObjectPath;
}

const std::string& FSandboxSession::GetLastMessage() const
{
    return LastMessage;
}

Pico::EObjectSerializationError FSandboxSession::GetLastSerializationError() const
{
    return LastSerializationError;
}

std::filesystem::path FSandboxSession::GetSandboxRootDirectory()
{
    if (!Pico::FPaths::HasProject())
    {
        Pico::FPaths::Init("", std::filesystem::path(PICO_SANDBOX_PROJECT_FILE));
    }
    return Pico::FPaths::GetProjectRootDir();
}

std::filesystem::path FSandboxSession::GetDefaultObjectPath()
{
    GetSandboxRootDirectory();
    std::filesystem::path ObjectPath;
    if (!Pico::FPaths::TryGetProjectWritePath(
            Pico::EProjectWriteRoot::Content,
            std::filesystem::path("Objects") / "SandboxCharacter.pobj",
            ObjectPath))
    {
        return {};
    }
    return ObjectPath;
}

std::string_view FSandboxSession::GetStepName(ESandboxStep Step)
{
    switch (Step)
    {
    case ESandboxStep::Initialized:
        return "Initialized";
    case ESandboxStep::Registered:
        return "Registered";
    case ESandboxStep::Created:
        return "Created";
    case ESandboxStep::Modified:
        return "Modified";
    case ESandboxStep::Saved:
        return "Saved";
    case ESandboxStep::Destroyed:
        return "Destroyed";
    case ESandboxStep::Loaded:
        return "Loaded";
    case ESandboxStep::Verified:
        return "Verified";
    case ESandboxStep::Count:
        break;
    }
    return "Unknown";
}

void FSandboxSession::ResetWorkflowStates()
{
    StepStates.fill(ESandboxStepState::NotStarted);
    LastSerializationError = Pico::EObjectSerializationError::None;
    LastMessage.clear();
}

void FSandboxSession::SetStep(ESandboxStep Step, ESandboxStepState State, std::string Message)
{
    StepStates[static_cast<std::size_t>(Step)] = State;
    LastMessage = std::move(Message);
}
}
