#include "DynamicDelegateExperiment.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Samples/DemoCharacter.h"

#include <imgui.h>

#include <algorithm>
#include <sstream>
#include <utility>

namespace Pico
{
namespace
{
constexpr std::array<const char*, 3> TargetLabels {
    "Observer A", "Observer B", "Character (mismatch demo)"
};

const char* GetBindResultText(EDynamicDelegateBindResult Result)
{
    switch (Result)
    {
    case EDynamicDelegateBindResult::Success: return "Success";
    case EDynamicDelegateBindResult::InvalidTarget: return "InvalidTarget";
    case EDynamicDelegateBindResult::FunctionNotFound: return "FunctionNotFound";
    case EDynamicDelegateBindResult::FunctionNotCallable: return "FunctionNotCallable";
    case EDynamicDelegateBindResult::SignatureMismatch: return "SignatureMismatch";
    case EDynamicDelegateBindResult::AlreadyBound: return "AlreadyBound";
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

FDynamicDelegateExperiment::~FDynamicDelegateExperiment()
{
    TearDown();
}

const char* FDynamicDelegateExperiment::GetName() const
{
    return "Dynamic Multicast";
}

bool FDynamicDelegateExperiment::SetUp()
{
    Reset();
    return ResolveObject(TargetHandles[0]) != nullptr
        && ResolveObject(TargetHandles[1]) != nullptr
        && ResolveObject(TargetHandles[2]) != nullptr;
}

void FDynamicDelegateExperiment::Draw()
{
    DrawSectionLabel("Event Signature");
    ImGui::TextUnformatted("void OnHealthChanged(Int32 OldHealth, Int32 NewHealth)");

    DrawSectionLabel("Dynamic Binding Configuration");
    if (ImGui::BeginCombo("Target", TargetLabels[SelectedTargetIndex]))
    {
        for (std::size_t Index = 0; Index < TargetLabels.size(); ++Index)
        {
            if (ImGui::Selectable(TargetLabels[Index], SelectedTargetIndex == Index))
            {
                SelectedTargetIndex = Index;
                SelectedFunctionIndex = 0;
            }
        }
        ImGui::EndCombo();
    }

    PObject* Target = GetSelectedTarget();
    const std::vector<const PFunction*> Functions = GetSelectedFunctions();
    if (SelectedFunctionIndex >= Functions.size())
    {
        SelectedFunctionIndex = 0;
    }
    std::string FunctionPreviewStorage = Functions.empty()
        ? "No reflected functions"
        : Functions[SelectedFunctionIndex]->GetName().ToString();
    if (ImGui::BeginCombo("PFunction", FunctionPreviewStorage.c_str()))
    {
        for (std::size_t Index = 0; Index < Functions.size(); ++Index)
        {
            const std::string Name = Functions[Index]->GetName().ToString();
            if (ImGui::Selectable(Name.c_str(), SelectedFunctionIndex == Index))
            {
                SelectedFunctionIndex = Index;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Text(
        "Target state: %s, Rooted: %s",
        Target != nullptr ? "Alive" : "Destroyed/Collected",
        Target != nullptr && IsRooted(Target) ? "Yes" : "No");

    ImGui::BeginDisabled(Target == nullptr || Functions.empty());
    if (ImGui::Button("Add Dynamic")) AddBinding(false);
    ImGui::SameLine();
    if (ImGui::Button("Add Unique Dynamic")) AddBinding(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        Reset();
        return;
    }

    DrawSectionLabel("Delegate Bindings");
    const std::vector<FDynamicDelegateBindingView> Bindings = Delegate.GetBindings();
    if (ImGui::BeginTable(
            "DynamicBindings",
            4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("PFunction");
        ImGui::TableSetupColumn("Weak Target");
        ImGui::TableSetupColumn("Selected", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableHeadersRow();
        for (std::size_t Index = 0; Index < Bindings.size(); ++Index)
        {
            const FDynamicDelegateBindingView& Binding = Bindings[Index];
            PObject* BindingTarget = ResolveObject(Binding.TargetHandle);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                BindingTarget != nullptr ? BindingTarget->GetPathName().c_str() : "Expired");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Binding.FunctionName.ToString().c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(
                Binding.bTargetAlive
                    ? ImVec4(0.35f, 0.78f, 0.66f, 1.0f)
                    : ImVec4(0.95f, 0.42f, 0.35f, 1.0f),
                "%s",
                Binding.bTargetAlive ? "Alive" : "Expired");
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(Index));
            const bool bSelected = SelectedBindingHandle == Binding.Handle;
            if (ImGui::Selectable("Select", bSelected))
            {
                SelectedBindingHandle = Binding.Handle;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Text("Live bindings: %zu", Delegate.Num());
    ImGui::BeginDisabled(!SelectedBindingHandle.IsValid());
    if (ImGui::Button("Remove Selected")) RemoveSelectedBinding();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(Target == nullptr);
    if (ImGui::Button("Remove Target Bindings")) RemoveSelectedTargetBindings();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear All"))
    {
        Delegate.Clear();
        SelectedBindingHandle.Reset();
        AddLog("Cleared all dynamic bindings");
    }

    DrawSectionLabel("Broadcast Through ProcessEvent");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Old Health", &OldHealth);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("New Health", &NewHealth);
    if (ImGui::Button("Broadcast Dynamic Event")) Broadcast();
    if (bHasBroadcastResult)
    {
        ImGui::Text(
            "Snapshot %zu   Invoked %zu   Failed %zu   Removed invalid %zu",
            LastBroadcastReport.SnapshotBindingCount,
            LastBroadcastReport.InvokedBindingCount,
            LastBroadcastReport.FailedBindingCount,
            LastBroadcastReport.RemovedInvalidBindingCount);
    }
    for (std::size_t Index = 0; Index < 2; ++Index)
    {
        PObject* Object = ResolveObject(TargetHandles[Index]);
        PDemoHealthObserver* Observer = Object != nullptr
                && Object->IsA(PDemoHealthObserver::StaticClass())
            ? static_cast<PDemoHealthObserver*>(Object)
            : nullptr;
        ImGui::Text(
            "%s calls: %d",
            TargetLabels[Index],
            Observer != nullptr ? Observer->GetNotificationCount() : 0);
    }

    DrawSectionLabel("Lifetime and GC");
    ImGui::BeginDisabled(Target == nullptr);
    if (Target != nullptr && IsRooted(Target))
    {
        if (ImGui::Button("Remove Target Root"))
        {
            RemoveFromRoot(Target);
            AddLog(std::string("Removed Root: ") + TargetLabels[SelectedTargetIndex]);
        }
    }
    else if (Target != nullptr && ImGui::Button("Add Target Root"))
    {
        AddToRoot(Target);
        AddLog(std::string("Added Root: ") + TargetLabels[SelectedTargetIndex]);
    }
    ImGui::SameLine();
    if (ImGui::Button("Destroy Target")) DestroySelectedTarget();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Request GC")) RequestGC();
    ImGui::SameLine();
    ImGui::BeginDisabled(!IsGarbageCollectionRequested());
    if (ImGui::Button("Run GC Safe Point")) RunGCSafePoint();
    ImGui::EndDisabled();
    if (bHasGCResult)
    {
        ImGui::Text(
            "GC collected %zu object(s); %zu remain",
            LastGCResult.CollectedObjectCount,
            LastGCResult.ObjectCountAfter);
    }

    DrawSectionLabel("Event Log");
    if (ImGui::Button("Clear Log")) EventLog.clear();
    ImGui::BeginChild("DynamicDelegateLog", ImVec2(0.0f, 150.0f), true);
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

void FDynamicDelegateExperiment::Reset()
{
    TearDown();
    ResetGarbageCollectionRequests();
    PDemoHealthObserver* ObserverA =
        NewObject<PDemoHealthObserver>(nullptr, "__DynamicObserverA");
    PDemoHealthObserver* ObserverB =
        NewObject<PDemoHealthObserver>(nullptr, "__DynamicObserverB");
    PDemoCharacter* Character =
        NewObject<PDemoCharacter>(nullptr, "__DynamicCharacter");
    const std::array<PObject*, 3> Targets {ObserverA, ObserverB, Character};
    for (std::size_t Index = 0; Index < Targets.size(); ++Index)
    {
        TargetHandles[Index] = Targets[Index] != nullptr
            ? Targets[Index]->GetHandle()
            : FObjectHandle {};
        if (Targets[Index] != nullptr)
        {
            AddToRoot(Targets[Index]);
        }
    }
    SelectedTargetIndex = 0;
    SelectedFunctionIndex = 0;
    SelectedBindingHandle.Reset();
    OldHealth = 100;
    NewHealth = 75;
    LastBroadcastReport = {};
    LastGCResult = {};
    bHasBroadcastResult = false;
    bHasGCResult = false;
    EventLog.clear();
    AddLog("Created two reflected observers and one mismatch target");
}

void FDynamicDelegateExperiment::TearDown()
{
    Delegate.Clear();
    SelectedBindingHandle.Reset();
    for (FObjectHandle Handle : TargetHandles)
    {
        if (PObject* Object = ResolveObject(Handle))
        {
            RemoveFromRoot(Object);
            DestroyObject(Object);
        }
    }
    TargetHandles = {};
}

PObject* FDynamicDelegateExperiment::GetSelectedTarget() const
{
    return SelectedTargetIndex < TargetHandles.size()
        ? ResolveObject(TargetHandles[SelectedTargetIndex])
        : nullptr;
}

std::vector<const PFunction*> FDynamicDelegateExperiment::GetSelectedFunctions() const
{
    PObject* Target = GetSelectedTarget();
    return Target != nullptr ? GetAllFunctions(Target->GetClass()) : std::vector<const PFunction*> {};
}

void FDynamicDelegateExperiment::AddBinding(bool bUnique)
{
    PObject* Target = GetSelectedTarget();
    const std::vector<const PFunction*> Functions = GetSelectedFunctions();
    if (Target == nullptr || SelectedFunctionIndex >= Functions.size())
    {
        return;
    }
    const FName FunctionName = Functions[SelectedFunctionIndex]->GetName();
    const FDynamicDelegateBindingResult Result = bUnique
        ? Delegate.AddUniqueDynamic(Target, FunctionName)
        : Delegate.AddDynamic(Target, FunctionName);
    if (Result.IsSuccess())
    {
        SelectedBindingHandle = Result.Handle;
    }
    AddLog(
        std::string(bUnique ? "AddUniqueDynamic " : "AddDynamic ")
        + Target->GetPathName() + "." + FunctionName.ToString()
        + ": " + GetBindResultText(Result.Result));
}

void FDynamicDelegateExperiment::RemoveSelectedBinding()
{
    const bool bRemoved = Delegate.Remove(SelectedBindingHandle);
    AddLog(bRemoved ? "Removed selected FDelegateHandle" : "Selected handle was not active");
    SelectedBindingHandle.Reset();
}

void FDynamicDelegateExperiment::RemoveSelectedTargetBindings()
{
    PObject* Target = GetSelectedTarget();
    const std::size_t Removed = Target != nullptr ? Delegate.RemoveAll(Target) : 0;
    AddLog("RemoveAll selected target: " + std::to_string(Removed));
    SelectedBindingHandle.Reset();
}

void FDynamicDelegateExperiment::Broadcast()
{
    LastBroadcastReport = Delegate.Broadcast(OldHealth, NewHealth);
    bHasBroadcastResult = true;
    AddLog(
        "Broadcast(" + std::to_string(OldHealth) + ", " + std::to_string(NewHealth)
        + "): invoked=" + std::to_string(LastBroadcastReport.InvokedBindingCount)
        + ", failed=" + std::to_string(LastBroadcastReport.FailedBindingCount)
        + ", removed=" + std::to_string(LastBroadcastReport.RemovedInvalidBindingCount));
}

void FDynamicDelegateExperiment::DestroySelectedTarget()
{
    PObject* Target = GetSelectedTarget();
    if (Target == nullptr)
    {
        return;
    }
    const std::string Name = TargetLabels[SelectedTargetIndex];
    RemoveFromRoot(Target);
    DestroyObject(Target);
    AddLog("Destroyed target: " + Name + "; binding remains weak until Broadcast");
}

void FDynamicDelegateExperiment::RequestGC()
{
    RequestGarbageCollection();
    AddLog("Requested GC; targets remain alive until the safe point");
}

void FDynamicDelegateExperiment::RunGCSafePoint()
{
    RunIsolatedCollection();
    AddLog(
        "GC safe point collected " + std::to_string(LastGCResult.CollectedObjectCount)
        + " object(s); Broadcast will compact expired bindings");
}

void FDynamicDelegateExperiment::RunIsolatedCollection()
{
    std::vector<FObjectHandle> TemporaryRoots;
    for (PObject* Object : FObjectRegistry::GetObjects())
    {
        if (!OwnsHandle(Object->GetHandle()) && !IsRooted(Object) && AddToRoot(Object))
        {
            TemporaryRoots.push_back(Object->GetHandle());
        }
    }
    bHasGCResult = CollectGarbageIfRequested(&LastGCResult);
    for (FObjectHandle Handle : TemporaryRoots)
    {
        if (PObject* Object = ResolveObject(Handle))
        {
            RemoveFromRoot(Object);
        }
    }
}

bool FDynamicDelegateExperiment::OwnsHandle(FObjectHandle Handle) const
{
    return std::find(TargetHandles.begin(), TargetHandles.end(), Handle)
        != TargetHandles.end();
}

void FDynamicDelegateExperiment::AddLog(std::string Message)
{
    std::ostringstream Stream;
    Stream << "[Frame " << ImGui::GetFrameCount() << "] " << Message;
    EventLog.push_back(Stream.str());
    if (EventLog.size() > 200)
    {
        EventLog.erase(EventLog.begin(), EventLog.begin() + 50);
    }
}
}
