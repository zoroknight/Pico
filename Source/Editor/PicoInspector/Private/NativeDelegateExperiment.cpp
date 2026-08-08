#include "NativeDelegateExperiment.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Samples/DemoCharacter.h"

#include <imgui.h>

#include <array>
#include <sstream>
#include <variant>

namespace Pico
{
namespace
{
void DrawSectionLabel(const char* Label)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(Label);
}
}

FNativeDelegateExperiment::~FNativeDelegateExperiment()
{
    TearDown();
}

const char* FNativeDelegateExperiment::GetName() const
{
    return "Native Delegates";
}

bool FNativeDelegateExperiment::SetUp()
{
    if (GetCharacter() != nullptr)
    {
        return true;
    }

    PDemoCharacter* Character =
        NewObject<PDemoCharacter>(nullptr, "__InspectorDelegateCharacter");
    PDemoHealthObserver* Observer =
        NewObject<PDemoHealthObserver>(nullptr, "__InspectorHealthObserver");
    if (Character == nullptr || Observer == nullptr)
    {
        if (Observer != nullptr)
        {
            DestroyObject(Observer);
        }
        if (Character != nullptr)
        {
            DestroyObject(Character);
        }
        return false;
    }

    CharacterHandle = Character->GetHandle();
    ObserverHandle = Observer->GetHandle();
    AddLog("Created Character and HealthObserver fixtures");
    AddLambdaListener();
    AddObjectListener();
    return true;
}

void FNativeDelegateExperiment::Draw()
{
    SyncObjectNotifications();
    PDemoCharacter* Character = GetCharacter();
    PDemoHealthObserver* Observer = GetObserver();

    if (ImGui::Button("Reset"))
    {
        Reset();
        Character = GetCharacter();
        Observer = GetObserver();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Fixture objects are also visible in Runtime Browser");

    DrawSectionLabel("Fixture State");
    ImGui::Text("Character: %s", Character != nullptr ? Character->GetPathName().c_str() : "Missing");
    ImGui::Text("Health: %d", Character != nullptr ? Character->GetHealth() : 0);
    ImGui::Text("Observer: %s", Observer != nullptr ? "Alive" : "Destroyed");
    ImGui::Text(
        "Live delegate bindings: %zu",
        Character != nullptr ? Character->OnHealthChanged.Num() : 0);

    DrawSectionLabel("Reflected Function");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Damage", &Damage);
    if (ImGui::Button("Invoke ApplyDamage"))
    {
        InvokeApplyDamage();
    }
    ImGui::SameLine();
    if (ImGui::Button("Invoke Wrong Float Argument"))
    {
        InvokeApplyDamageWithWrongType();
    }

    DrawSectionLabel("Listeners");
    if (ImGui::BeginTable(
            "DelegateListeners",
            4,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Calls");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Lambda");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Event Log");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(LambdaHandle.IsValid() && Character != nullptr ? "Active" : "Removed");
        ImGui::TableNextColumn();
        ImGui::Text("%d", LambdaCallCount);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Weak PObject");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("HealthObserver");
        ImGui::TableNextColumn();
        const bool bObjectBindingActive =
            Character != nullptr && Observer != nullptr
            && Character->OnHealthChanged.IsBoundTo(Observer);
        ImGui::TextUnformatted(
            bObjectBindingActive ? "Active" : (bObjectBindingWasAdded ? "Expired/Removed" : "Not Added"));
        ImGui::TableNextColumn();
        ImGui::Text("%d", Observer != nullptr
            ? Observer->GetNotificationCount()
            : ObjectCallCountBeforeDestroy);
        ImGui::EndTable();
    }

    if (ImGui::Button("Add Lambda"))
    {
        AddLambdaListener();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Lambda"))
    {
        RemoveLambdaListener();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Object Listener"))
    {
        AddObjectListener();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Object Listeners"))
    {
        RemoveObjectListeners();
    }
    if (ImGui::Button("Destroy Observer"))
    {
        DestroyObserver();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All"))
    {
        ClearListeners();
    }

    DrawSectionLabel("Manual Broadcast");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Old Health", &ManualOldHealth);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("New Health", &ManualNewHealth);
    if (ImGui::Button("Broadcast OnHealthChanged"))
    {
        BroadcastManualEvent();
    }

    DrawSectionLabel("Event Log");
    if (ImGui::Button("Clear Log"))
    {
        EventLog.clear();
    }
    ImGui::BeginChild("EventLog", ImVec2(0.0f, 180.0f), true);
    for (const std::string& Entry : EventLog)
    {
        ImGui::TextUnformatted(Entry.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void FNativeDelegateExperiment::Reset()
{
    TearDown();
    EventLog.clear();
    LambdaCallCount = 0;
    LastObservedObjectCallCount = 0;
    ObjectCallCountBeforeDestroy = 0;
    Damage = 20;
    ManualOldHealth = 100;
    ManualNewHealth = 75;
    bObjectBindingWasAdded = false;
    SetUp();
}

void FNativeDelegateExperiment::TearDown()
{
    PDemoCharacter* Character = GetCharacter();
    if (Character != nullptr)
    {
        Character->OnHealthChanged.Clear();
    }
    LambdaHandle.Reset();
    ObjectHandle.Reset();

    if (PDemoHealthObserver* Observer = GetObserver())
    {
        DestroyObject(Observer);
    }
    ObserverHandle = {};
    if (Character != nullptr)
    {
        DestroyObject(Character);
    }
    CharacterHandle = {};
}

PDemoCharacter* FNativeDelegateExperiment::GetCharacter() const
{
    return static_cast<PDemoCharacter*>(ResolveObject(CharacterHandle));
}

PDemoHealthObserver* FNativeDelegateExperiment::GetObserver() const
{
    return static_cast<PDemoHealthObserver*>(ResolveObject(ObserverHandle));
}

void FNativeDelegateExperiment::AddLambdaListener()
{
    PDemoCharacter* Character = GetCharacter();
    if (Character == nullptr || LambdaHandle.IsValid())
    {
        return;
    }
    LambdaHandle = Character->OnHealthChanged.AddLambda(
        [this](int32 OldHealth, int32 NewHealth)
        {
            ++LambdaCallCount;
            std::ostringstream Stream;
            Stream << "Lambda listener: Health " << OldHealth << " -> " << NewHealth;
            AddLog(Stream.str());
        });
    AddLog("Added Lambda listener");
}

void FNativeDelegateExperiment::AddObjectListener()
{
    PDemoCharacter* Character = GetCharacter();
    PDemoHealthObserver* Observer = GetObserver();
    if (Character == nullptr || Observer == nullptr || Character->OnHealthChanged.IsBoundTo(Observer))
    {
        return;
    }
    ObjectHandle = Character->OnHealthChanged.AddObject(
        Observer,
        &PDemoHealthObserver::HandleHealthChanged);
    bObjectBindingWasAdded = ObjectHandle.IsValid();
    AddLog("Added weak HealthObserver listener");
}

void FNativeDelegateExperiment::RemoveLambdaListener()
{
    if (PDemoCharacter* Character = GetCharacter();
        Character != nullptr && Character->OnHealthChanged.Remove(LambdaHandle))
    {
        AddLog("Removed Lambda listener by FDelegateHandle");
    }
    LambdaHandle.Reset();
}

void FNativeDelegateExperiment::RemoveObjectListeners()
{
    PDemoCharacter* Character = GetCharacter();
    PDemoHealthObserver* Observer = GetObserver();
    if (Character != nullptr && Observer != nullptr)
    {
        const std::size_t Removed = Character->OnHealthChanged.RemoveAll(Observer);
        AddLog("RemoveAll(HealthObserver): " + std::to_string(Removed));
    }
    ObjectHandle.Reset();
}

void FNativeDelegateExperiment::DestroyObserver()
{
    if (PDemoHealthObserver* Observer = GetObserver())
    {
        SyncObjectNotifications();
        ObjectCallCountBeforeDestroy = Observer->GetNotificationCount();
        DestroyObject(Observer);
        ObserverHandle = {};
        AddLog("Destroyed HealthObserver; its weak binding is now expired");
    }
}

void FNativeDelegateExperiment::ClearListeners()
{
    if (PDemoCharacter* Character = GetCharacter())
    {
        Character->OnHealthChanged.Clear();
        LambdaHandle.Reset();
        ObjectHandle.Reset();
        AddLog("Cleared all listeners");
    }
}

void FNativeDelegateExperiment::InvokeApplyDamage()
{
    PDemoCharacter* Character = GetCharacter();
    const PFunction* Function = Character != nullptr
        ? Character->GetClass()->FindFunction(FName("ApplyDamage"))
        : nullptr;
    const std::array<FFunctionValue, 1> Arguments { int32 { Damage } };
    FFunctionValue ReturnValue;
    const EFunctionInvokeResult Result = Character != nullptr
        ? Character->ProcessEvent(Function, Arguments, &ReturnValue)
        : EFunctionInvokeResult::InvalidTarget;
    SyncObjectNotifications();

    std::ostringstream Stream;
    Stream << "ProcessEvent ApplyDamage(" << Damage << "): ";
    if (Result == EFunctionInvokeResult::Success && std::holds_alternative<int32>(ReturnValue))
    {
        Stream << "Success, Health=" << std::get<int32>(ReturnValue);
    }
    else
    {
        Stream << "Failed (result=" << static_cast<int>(Result) << ')';
    }
    AddLog(Stream.str());
}

void FNativeDelegateExperiment::InvokeApplyDamageWithWrongType()
{
    PDemoCharacter* Character = GetCharacter();
    const PFunction* Function = Character != nullptr
        ? Character->GetClass()->FindFunction(FName("ApplyDamage"))
        : nullptr;
    const std::array<FFunctionValue, 1> Arguments { static_cast<float>(Damage) };
    FFunctionValue ReturnValue;
    const EFunctionInvokeResult Result = Character != nullptr
        ? Character->ProcessEvent(Function, Arguments, &ReturnValue)
        : EFunctionInvokeResult::InvalidTarget;
    AddLog(Result == EFunctionInvokeResult::ArgumentTypeMismatch
        ? "Wrong Float argument rejected: ArgumentTypeMismatch"
        : "Wrong Float argument produced an unexpected result");
}

void FNativeDelegateExperiment::BroadcastManualEvent()
{
    if (PDemoCharacter* Character = GetCharacter())
    {
        Character->OnHealthChanged.Broadcast(ManualOldHealth, ManualNewHealth);
        SyncObjectNotifications();
        AddLog(
            "Manual Broadcast(" + std::to_string(ManualOldHealth)
            + ", " + std::to_string(ManualNewHealth) + ")");
    }
}

void FNativeDelegateExperiment::SyncObjectNotifications()
{
    PDemoHealthObserver* Observer = GetObserver();
    if (Observer == nullptr)
    {
        return;
    }
    while (LastObservedObjectCallCount < Observer->GetNotificationCount())
    {
        ++LastObservedObjectCallCount;
        AddLog(
            "HealthObserver listener: Health "
            + std::to_string(Observer->GetLastOldHealth()) + " -> "
            + std::to_string(Observer->GetLastNewHealth()));
    }
}

void FNativeDelegateExperiment::AddLog(std::string Message)
{
    EventLog.push_back(std::move(Message));
    if (EventLog.size() > 200)
    {
        EventLog.erase(EventLog.begin(), EventLog.begin() + 50);
    }
}
}
