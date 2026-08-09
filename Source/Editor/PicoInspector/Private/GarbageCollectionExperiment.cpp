#include "GarbageCollectionExperiment.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ReflectionMacros.h"

#include <imgui.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
class PGCExperimentNode final : public PObject
{
    PICO_DECLARE_CLASS(PGCExperimentNode, PObject)

public:
    void SetStrong(PGCExperimentNode* Object) { Strong = Object; }
    void SetWeak(PGCExperimentNode* Object) { Weak = Object; }

protected:
    explicit PGCExperimentNode(const FObjectConstructionParams& Params) : PObject(Params) {}

private:
    TObjectPtr<PGCExperimentNode> Strong;
    TWeakObjectPtr<PGCExperimentNode> Weak;
};

PICO_DEFINE_CLASS(PGCExperimentNode)

bool PGCExperimentNode::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Editable | EPropertyFlags::Transient;
    std::vector<PProperty> Properties;
    Properties.push_back(PProperty::Create<&ThisClass::Strong>(FName("Strong"), Metadata));
    Properties.push_back(PProperty::Create<&ThisClass::Weak>(FName("Weak"), Metadata));
    return Class.AddProperties(std::move(Properties));
}

constexpr std::array<const char*, 5> Labels {
    "Root", "Strong target", "Weak target", "Cycle A", "Cycle B"
};

constexpr std::array<const char*, 5> Relationships {
    "Root Set", "Root --strong-> target", "Root --weak-> target",
    "Cycle B --strong-> A", "Cycle A --strong-> B"
};

std::string GetReasonsText(EGarbageCollectionReason Reasons)
{
    std::string Text;
    const auto Append = [&Text](const char* Name)
    {
        if (!Text.empty())
        {
            Text += " | ";
        }
        Text += Name;
    };
    if (HasAnyGarbageCollectionReason(Reasons, EGarbageCollectionReason::Explicit)) Append("Explicit");
    if (HasAnyGarbageCollectionReason(Reasons, EGarbageCollectionReason::TimeLimit)) Append("TimeLimit");
    if (HasAnyGarbageCollectionReason(Reasons, EGarbageCollectionReason::WorldTransition)) Append("WorldTransition");
    if (HasAnyGarbageCollectionReason(Reasons, EGarbageCollectionReason::EngineExit)) Append("EngineExit");
    return Text.empty() ? "None" : Text;
}

void DrawSectionLabel(const char* Label)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(Label);
}

void DrawHelpMarker(const char* Text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", Text);
    }
}
}

FGarbageCollectionExperiment::~FGarbageCollectionExperiment()
{
    TearDown();
}

const char* FGarbageCollectionExperiment::GetName() const
{
    return "Garbage Collection";
}

bool FGarbageCollectionExperiment::SetUp()
{
    if (!PGCExperimentNode::RegisterClass())
    {
        return false;
    }
    Reset();
    return ResolveObject(Handles[0]) != nullptr;
}

void FGarbageCollectionExperiment::Draw()
{
    PObject* Root = ResolveObject(Handles[0]);

    const EGarbageCollectionReason PendingReasons =
        GetPendingGarbageCollectionReasons();
    const bool bPending = PendingReasons != EGarbageCollectionReason::None;
    DrawSectionLabel("Scheduler State");
    ImGui::TextUnformatted("State");
    ImGui::SameLine();
    ImGui::TextColored(
        bPending ? ImVec4(0.95f, 0.67f, 0.24f, 1.0f) : ImVec4(0.35f, 0.78f, 0.66f, 1.0f),
        "%s",
        bPending ? "Pending" : "Idle");
    ImGui::Text("Pending reasons: %s", GetReasonsText(PendingReasons).c_str());

    DrawSectionLabel("Schedule Requests");
    if (ImGui::Button("Request Explicit"))
    {
        RequestCollection(EGarbageCollectionReason::Explicit, "Explicit");
    }
    ImGui::SameLine();
    if (ImGui::Button("Request Time Limit"))
    {
        RequestCollection(EGarbageCollectionReason::TimeLimit, "TimeLimit");
    }
    ImGui::SameLine();
    if (ImGui::Button("Request World Transition"))
    {
        RequestCollection(EGarbageCollectionReason::WorldTransition, "WorldTransition");
    }

    ImGui::BeginDisabled(!IsGarbageCollectionRequested());
    if (ImGui::Button("Run Safe Point"))
    {
        RunSafePoint();
    }
    ImGui::EndDisabled();
    DrawHelpMarker("Consumes pending requests through CollectGarbageIfRequested.");
    ImGui::SameLine();
    if (ImGui::Button("Collect Now"))
    {
        CollectNow();
    }
    DrawHelpMarker("Calls CollectGarbage immediately without waiting for a safe point.");
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        Reset();
        Root = ResolveObject(Handles[0]);
    }

    DrawSectionLabel("Object Graph");
    if (Root != nullptr && IsRooted(Root))
    {
        if (ImGui::Button("Remove Root")) RemoveFromRoot(Root);
    }
    else if (Root != nullptr && ImGui::Button("Add Root"))
    {
        AddToRoot(Root);
    }

    if (ImGui::BeginTable(
            "GCObjects",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableSetupColumn("Reference", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Rooted", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableHeadersRow();
        for (std::size_t Index = 0; Index < Handles.size(); ++Index)
        {
            PObject* Object = ResolveObject(Handles[Index]);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(Labels[Index]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(Relationships[Index]);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(
                Object != nullptr ? ImVec4(0.35f, 0.78f, 0.66f, 1.0f) : ImVec4(0.95f, 0.42f, 0.35f, 1.0f),
                "%s",
                Object != nullptr ? "Alive" : "Collected");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("{%u, %u}", Handles[Index].Index, Handles[Index].Serial);
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(Object != nullptr && IsRooted(Object) ? "Yes" : "No");
        }
        ImGui::EndTable();
    }

    if (bHasResult)
    {
        DrawSectionLabel("Last Collection");
        ImGui::Text("Trigger: %s", LastTrigger.c_str());
        ImGui::Text("Consumed reasons: %s", GetReasonsText(LastExecutedReasons).c_str());
        ImGui::Text(
            "Before %zu   Roots %zu   Reachable %zu   Collected %zu   After %zu",
            LastResult.ObjectCountBefore,
            LastResult.RootCount,
            LastResult.ReachableObjectCount,
            LastResult.CollectedObjectCount,
            LastResult.ObjectCountAfter);
    }

    DrawSectionLabel("Event Timeline");
    if (ImGui::Button("Clear Log"))
    {
        EventLog.clear();
    }
    ImGui::BeginChild("GCEventTimeline", ImVec2(0.0f, 150.0f), true);
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

void FGarbageCollectionExperiment::Reset()
{
    TearDown();
    ResetGarbageCollectionRequests();
    EventLog.clear();
    PGCExperimentNode* Root = NewObject<PGCExperimentNode>(nullptr, "GC_Root");
    PGCExperimentNode* Strong = NewObject<PGCExperimentNode>(nullptr, "GC_Strong");
    PGCExperimentNode* Weak = NewObject<PGCExperimentNode>(nullptr, "GC_Weak");
    PGCExperimentNode* CycleA = NewObject<PGCExperimentNode>(nullptr, "GC_CycleA");
    PGCExperimentNode* CycleB = NewObject<PGCExperimentNode>(nullptr, "GC_CycleB");
    const std::array<PGCExperimentNode*, 5> Objects {Root, Strong, Weak, CycleA, CycleB};
    for (std::size_t Index = 0; Index < Objects.size(); ++Index)
    {
        Handles[Index] = Objects[Index] != nullptr ? Objects[Index]->GetHandle() : FObjectHandle {};
    }
    if (Root != nullptr)
    {
        Root->SetStrong(Strong);
        Root->SetWeak(Weak);
        AddToRoot(Root);
    }
    if (CycleA != nullptr && CycleB != nullptr)
    {
        CycleA->SetStrong(CycleB);
        CycleB->SetStrong(CycleA);
    }
    LastResult = {};
    LastExecutedReasons = EGarbageCollectionReason::None;
    LastTrigger.clear();
    bHasResult = false;
    AddLog("Created Root, Strong, Weak, and unrooted Cycle fixtures");
}

void FGarbageCollectionExperiment::TearDown()
{
    for (FObjectHandle Handle : Handles)
    {
        if (PObject* Object = ResolveObject(Handle))
        {
            RemoveFromRoot(Object);
            DestroyObject(Object);
        }
    }
    Handles = {};
    ResetGarbageCollectionRequests();
}

void FGarbageCollectionExperiment::RequestCollection(
    EGarbageCollectionReason Reason,
    const char* ReasonName)
{
    RequestGarbageCollection(Reason);
    AddLog(
        std::string("Requested GC: ") + ReasonName
        + " (pending=" + GetReasonsText(GetPendingGarbageCollectionReasons()) + ")");
}

void FGarbageCollectionExperiment::RunSafePoint()
{
    RunCollection(true, "Run Safe Point");
}

void FGarbageCollectionExperiment::CollectNow()
{
    RunCollection(false, "Collect Now");
}

void FGarbageCollectionExperiment::RunCollection(
    bool bRequireRequest,
    const char* TriggerName)
{
    const std::array<bool, 5> WasAlive {
        ResolveObject(Handles[0]) != nullptr,
        ResolveObject(Handles[1]) != nullptr,
        ResolveObject(Handles[2]) != nullptr,
        ResolveObject(Handles[3]) != nullptr,
        ResolveObject(Handles[4]) != nullptr
    };
    LastExecutedReasons = GetPendingGarbageCollectionReasons();
    std::vector<FObjectHandle> TemporaryRoots;
    for (PObject* Object : FObjectRegistry::GetObjects())
    {
        if (!OwnsHandle(Object->GetHandle()) && !IsRooted(Object) && AddToRoot(Object))
        {
            TemporaryRoots.push_back(Object->GetHandle());
        }
    }

    bool bSucceeded = false;
    if (bRequireRequest)
    {
        bSucceeded = CollectGarbageIfRequested(&LastResult);
    }
    else
    {
        LastResult = CollectGarbage();
        bSucceeded = LastResult.bSucceeded;
    }

    for (FObjectHandle Handle : TemporaryRoots)
    {
        if (PObject* Object = ResolveObject(Handle))
        {
            RemoveFromRoot(Object);
        }
    }

    LastTrigger = TriggerName;
    bHasResult = bSucceeded;
    AddLog(
        std::string(TriggerName) + (bSucceeded ? " executed" : " rejected")
        + " (reasons=" + GetReasonsText(LastExecutedReasons) + ")");
    if (!bSucceeded)
    {
        return;
    }

    for (std::size_t Index = 0; Index < Handles.size(); ++Index)
    {
        if (!WasAlive[Index])
        {
            continue;
        }
        AddLog(
            std::string(ResolveObject(Handles[Index]) != nullptr ? "Survived: " : "Collected: ")
            + Labels[Index]);
    }
}

void FGarbageCollectionExperiment::AddLog(std::string Message)
{
    std::ostringstream Stream;
    Stream << "[Frame " << ImGui::GetFrameCount() << "] " << Message;
    EventLog.push_back(Stream.str());
    if (EventLog.size() > 200)
    {
        EventLog.erase(EventLog.begin(), EventLog.begin() + 50);
    }
}

bool FGarbageCollectionExperiment::OwnsHandle(FObjectHandle Handle) const
{
    return std::find(Handles.begin(), Handles.end(), Handle) != Handles.end();
}
}
