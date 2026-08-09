#include "PropertyNotificationExperiment.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Property.h"
#include "Pico/Samples/DemoCharacter.h"

#include <imgui.h>

#include <array>
#include <sstream>
#include <string_view>

namespace Pico
{
namespace
{
constexpr std::array<const char*, 4> ChangeTypeLabels {
    "ValueSet", "Interactive", "Load", "UndoRedo"
};

const char* GetChangeTypeLabel(EPropertyChangeType ChangeType)
{
    switch (ChangeType)
    {
    case EPropertyChangeType::ValueSet: return "ValueSet";
    case EPropertyChangeType::Interactive: return "Interactive";
    case EPropertyChangeType::Load: return "Load";
    case EPropertyChangeType::UndoRedo: return "UndoRedo";
    }
    return "Unknown";
}

void DrawSectionLabel(const char* Label)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(Label);
}
}

FPropertyNotificationExperiment::~FPropertyNotificationExperiment()
{
    TearDown();
}

const char* FPropertyNotificationExperiment::GetName() const
{
    return "Property Notifications";
}

bool FPropertyNotificationExperiment::SetUp()
{
    if (GetInstance() != nullptr)
    {
        return true;
    }
    PDemoCharacter* Instance = NewObject<PDemoCharacter>(
        nullptr, "__InspectorPropertyInstance");
    PDemoCharacter* DefaultObject = GetDefaultObject();
    if (Instance == nullptr || DefaultObject == nullptr || GetHealthProperty() == nullptr)
    {
        if (Instance != nullptr)
        {
            DestroyObject(Instance);
        }
        return false;
    }
    AddToRoot(Instance);
    InstanceHandle = Instance->GetHandle();
    OriginalDefaultHealth = DefaultObject->GetHealth();
    NewHealth = Instance->GetHealth() - 25;
    BindNotifications();
    EventLog.push_back("Created an instance and attached CDO/instance listeners");
    return true;
}

void FPropertyNotificationExperiment::Draw()
{
    if (ImGui::Button("Reset"))
    {
        Reset();
    }

    PDemoCharacter* Instance = GetInstance();
    PDemoCharacter* DefaultObject = GetDefaultObject();
    PDemoCharacter* Future = GetFutureInstance();
    const PProperty* HealthProperty = GetHealthProperty();

    DrawSectionLabel("State");
    ImGui::Text("Instance Health: %d", Instance != nullptr ? Instance->GetHealth() : 0);
    ImGui::Text("CDO Health: %d", DefaultObject != nullptr ? DefaultObject->GetHealth() : 0);
    ImGui::Text("Future Instance Health: %s", Future != nullptr
        ? std::to_string(Future->GetHealth()).c_str() : "Not Spawned");
    ImGui::Text("Pre events: %d   Post events: %d", PreEventCount, PostEventCount);

    DrawSectionLabel("Reflected Write");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("New Health", &NewHealth);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo(
            "Change Source",
            ChangeTypeLabels[static_cast<std::size_t>(SelectedChangeType)]))
    {
        for (std::size_t Index = 0; Index < ChangeTypeLabels.size(); ++Index)
        {
            if (ImGui::Selectable(
                    ChangeTypeLabels[Index],
                    SelectedChangeType == static_cast<int>(Index)))
            {
                SelectedChangeType = static_cast<int>(Index);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(Instance == nullptr || HealthProperty == nullptr);
    if (ImGui::Button("Set Instance Through PProperty"))
    {
        SetInstanceHealth();
    }
    ImGui::EndDisabled();

    DrawSectionLabel("Class Default Object");
    ImGui::BeginDisabled(DefaultObject == nullptr || HealthProperty == nullptr);
    if (ImGui::Button("Set CDO Default"))
    {
        SetDefaultHealth();
    }
    ImGui::SameLine();
    if (ImGui::Button("Spawn From CDO"))
    {
        SpawnFutureInstance();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("The live instance keeps its value; only later instances copy the new CDO default.");

    DrawSectionLabel("Event Log");
    if (ImGui::Button("Clear Log"))
    {
        EventLog.clear();
    }
    ImGui::BeginChild("PropertyNotificationLog", ImVec2(0.0f, 190.0f), true);
    for (const std::string& Entry : EventLog)
    {
        ImGui::TextWrapped("%s", Entry.c_str());
    }
    ImGui::EndChild();
}

void FPropertyNotificationExperiment::Reset()
{
    TearDown();
    EventLog.clear();
    PreEventCount = 0;
    PostEventCount = 0;
    SelectedChangeType = 0;
    SetUp();
}

void FPropertyNotificationExperiment::TearDown()
{
    PDemoCharacter* Instance = GetInstance();
    PDemoCharacter* DefaultObject = GetDefaultObject();
    if (Instance != nullptr)
    {
        Instance->OnPropertyChanging().Remove(InstancePreHandle);
        Instance->OnPropertyChanged().Remove(InstancePostHandle);
    }
    if (DefaultObject != nullptr)
    {
        DefaultObject->OnPropertyChanging().Remove(DefaultPreHandle);
        DefaultObject->OnPropertyChanged().Remove(DefaultPostHandle);
        if (const PProperty* Property = GetHealthProperty())
        {
            Property->SetValueSilently(DefaultObject, int32 {OriginalDefaultHealth});
        }
    }
    if (PDemoCharacter* Future = GetFutureInstance())
    {
        RemoveFromRoot(Future);
        DestroyObject(Future);
    }
    if (Instance != nullptr)
    {
        RemoveFromRoot(Instance);
        DestroyObject(Instance);
    }
    InstanceHandle = {};
    FutureInstanceHandle = {};
    InstancePreHandle.Reset();
    InstancePostHandle.Reset();
    DefaultPreHandle.Reset();
    DefaultPostHandle.Reset();
}

PDemoCharacter* FPropertyNotificationExperiment::GetInstance() const
{
    PObject* Object = ResolveObject(InstanceHandle);
    return Object != nullptr && Object->IsA(PDemoCharacter::StaticClass())
        ? static_cast<PDemoCharacter*>(Object) : nullptr;
}

PDemoCharacter* FPropertyNotificationExperiment::GetFutureInstance() const
{
    PObject* Object = ResolveObject(FutureInstanceHandle);
    return Object != nullptr && Object->IsA(PDemoCharacter::StaticClass())
        ? static_cast<PDemoCharacter*>(Object) : nullptr;
}

PDemoCharacter* FPropertyNotificationExperiment::GetDefaultObject() const
{
    return static_cast<PDemoCharacter*>(
        PDemoCharacter::StaticClass()->GetMutableDefaultObject());
}

const PProperty* FPropertyNotificationExperiment::GetHealthProperty() const
{
    return PDemoCharacter::StaticClass()->FindProperty(FName("Health"));
}

EPropertyChangeType FPropertyNotificationExperiment::GetSelectedChangeType() const
{
    return static_cast<EPropertyChangeType>(SelectedChangeType);
}

void FPropertyNotificationExperiment::BindNotifications()
{
    PDemoCharacter* Instance = GetInstance();
    PDemoCharacter* DefaultObject = GetDefaultObject();
    InstancePreHandle = Instance->OnPropertyChanging().AddLambda(
        [this](PObject*, const FPropertyChangedEvent& Event)
        {
            AddEvent("Pre", "Instance", GetInstance(), Event);
        });
    InstancePostHandle = Instance->OnPropertyChanged().AddLambda(
        [this](PObject*, const FPropertyChangedEvent& Event)
        {
            AddEvent("Post", "Instance", GetInstance(), Event);
        });
    DefaultPreHandle = DefaultObject->OnPropertyChanging().AddLambda(
        [this](PObject*, const FPropertyChangedEvent& Event)
        {
            AddEvent("Pre", "CDO", GetDefaultObject(), Event);
        });
    DefaultPostHandle = DefaultObject->OnPropertyChanged().AddLambda(
        [this](PObject*, const FPropertyChangedEvent& Event)
        {
            AddEvent("Post", "CDO", GetDefaultObject(), Event);
        });
}

void FPropertyNotificationExperiment::SetInstanceHealth()
{
    GetHealthProperty()->SetValue(
        GetInstance(), int32 {NewHealth}, GetSelectedChangeType());
}

void FPropertyNotificationExperiment::SetDefaultHealth()
{
    GetHealthProperty()->SetValue(
        GetDefaultObject(), int32 {NewHealth}, GetSelectedChangeType());
}

void FPropertyNotificationExperiment::SpawnFutureInstance()
{
    if (PDemoCharacter* Existing = GetFutureInstance())
    {
        RemoveFromRoot(Existing);
        DestroyObject(Existing);
    }
    PDemoCharacter* Future = NewObject<PDemoCharacter>(
        nullptr, "__InspectorFuturePropertyInstance");
    if (Future != nullptr)
    {
        AddToRoot(Future);
        FutureInstanceHandle = Future->GetHandle();
        EventLog.push_back(
            "Spawned future instance with Health="
            + std::to_string(Future->GetHealth()));
    }
}

void FPropertyNotificationExperiment::AddEvent(
    const char* Phase,
    const char* Target,
    PDemoCharacter* Character,
    const FPropertyChangedEvent& Event)
{
    if (std::string_view(Phase) == "Pre")
    {
        ++PreEventCount;
    }
    else
    {
        ++PostEventCount;
    }
    std::ostringstream Stream;
    Stream << Phase << ' ' << Target << '.'
           << (Event.Property != nullptr
                ? Event.Property->GetName().ToString() : "None")
           << " source=" << GetChangeTypeLabel(Event.ChangeType)
           << " observed Health="
           << (Character != nullptr ? Character->GetHealth() : 0);
    EventLog.push_back(Stream.str());
}
}
