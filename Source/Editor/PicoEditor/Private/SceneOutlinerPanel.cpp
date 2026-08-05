#include "SceneOutlinerPanel.h"

#include "Pico/Editor/EditorCommandQueue.h"
#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <imgui.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
void PushObjectId(const PObject* Object)
{
    const FObjectHandle Handle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    ImGui::PushID(static_cast<int>(Handle.Index));
    ImGui::PushID(static_cast<int>(Handle.Serial));
}

void PopObjectId()
{
    ImGui::PopID();
    ImGui::PopID();
}

bool ContainsComponent(PSceneComponent* Component, FObjectHandle Handle)
{
    if (Component == nullptr || !Handle.IsValid())
    {
        return false;
    }
    if (Component->GetHandle() == Handle)
    {
        return true;
    }
    for (PSceneComponent* Child : Component->GetAttachChildren())
    {
        if (ContainsComponent(Child, Handle))
        {
            return true;
        }
    }
    return false;
}
}

void FSceneOutlinerPanel::Draw(
    PWorld* InWorld,
    FEditorSelection& InSelection,
    FEditorCommandService& InCommands,
    FEditorCommandQueue& InQueue,
    FSelectObject InSelectObject,
    FBeginRename InBeginRename,
    FApplyResult InApplyResult)
{
    World = InWorld;
    Selection = &InSelection;
    Commands = &InCommands;
    Queue = &InQueue;
    SelectObject = std::move(InSelectObject);
    RequestRename = std::move(InBeginRename);
    ApplyResult = std::move(InApplyResult);

    PWorld* CurrentWorld = InWorld;
    if (CurrentWorld == nullptr)
    {
        ImGui::TextDisabled("No active world");
        return;
    }
    BuildObjectOrder(CurrentWorld);

    PushObjectId(CurrentWorld);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Selection->Contains(CurrentWorld))
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpen = ImGui::TreeNodeEx(CurrentWorld->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(CurrentWorld);
    }
    if (ImGui::BeginPopupContextItem("WorldContext"))
    {
        EnsureSelected(CurrentWorld);
        if (ImGui::BeginMenu("Add Actor"))
        {
            if (ImGui::MenuItem("Empty Actor"))
            {
                SpawnEmptyActor();
            }
            if (ImGui::MenuItem("Cube"))
            {
                SpawnCubeActor();
            }
            ImGui::EndMenu();
        }
        ImGui::BeginDisabled(!CanPasteClipboard());
        if (ImGui::MenuItem("Paste", "Ctrl+V"))
        {
            Queue->Enqueue([this]() { PasteClipboard(); });
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (bOpen)
    {
        for (PLevel* Level : CurrentWorld->GetLevels())
        {
            DrawLevelNode(Level);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FSceneOutlinerPanel::DrawLevelNode(PLevel* Level)
{
    if (Level == nullptr)
    {
        return;
    }

    PushObjectId(Level);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Selection->Contains(Level))
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpen = ImGui::TreeNodeEx(Level->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Level);
    }
    if (ImGui::BeginPopupContextItem("LevelContext"))
    {
        EnsureSelected(Level);
        if (ImGui::BeginMenu("Add Actor"))
        {
            if (ImGui::MenuItem("Empty Actor"))
            {
                SpawnEmptyActor();
            }
            if (ImGui::MenuItem("Cube"))
            {
                SpawnCubeActor();
            }
            ImGui::EndMenu();
        }
        ImGui::BeginDisabled(!CanPasteClipboard());
        if (ImGui::MenuItem("Paste", "Ctrl+V"))
        {
            Queue->Enqueue([this]() { PasteClipboard(); });
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (bOpen)
    {
        for (PActor* Actor : Level->GetActors())
        {
            DrawActorNode(Actor);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FSceneOutlinerPanel::DrawActorNode(PActor* Actor)
{
    if (Actor == nullptr)
    {
        return;
    }

    const std::vector<PActorComponent*> Components = Actor->GetComponents();
    PushObjectId(Actor);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Components.empty())
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (Selection->Contains(Actor))
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::vector<PObject*> SelectedObjects = Selection->ResolveAll();
    if (std::any_of(
            SelectedObjects.begin(),
            SelectedObjects.end(),
            [Actor](PObject* SelectedObject)
            {
                return SelectedObject != nullptr
                    && SelectedObject->IsA(PActorComponent::StaticClass())
                    && static_cast<PActorComponent*>(SelectedObject)->GetOwner() == Actor;
            }))
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    const bool bOpen = ImGui::TreeNodeEx(Actor->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Actor);
    }
    DrawActorContextMenu(Actor);
    if (bOpen && !Components.empty())
    {
        PSceneComponent* RootComponent = Actor->GetRootComponent();
        if (RootComponent != nullptr)
        {
            DrawComponentNode(RootComponent, Actor);
        }

        for (PActorComponent* Component : Components)
        {
            if (Component == RootComponent)
            {
                continue;
            }

            if (Component->IsA(PSceneComponent::StaticClass())
                && static_cast<PSceneComponent*>(Component)->GetAttachParent() != nullptr)
            {
                continue;
            }
            DrawComponentNode(Component, Actor);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}


void FSceneOutlinerPanel::DrawComponentNode(PActorComponent* Component, PActor* Owner)
{
    if (Component == nullptr)
    {
        return;
    }

    PSceneComponent* SceneComponent = Component->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Component)
        : nullptr;
    const std::vector<PSceneComponent*> Children = SceneComponent != nullptr
        ? SceneComponent->GetAttachChildren()
        : std::vector<PSceneComponent*> {};

    PushObjectId(Component);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Children.empty())
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (Selection->Contains(Component))
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string Label = Component->GetName().ToString();
    if (Owner != nullptr && Owner->GetRootComponent() == Component)
    {
        Label += " [Root]";
    }
    const bool bContainsSelectedChild = SceneComponent != nullptr
        && std::any_of(
            Selection->GetHandles().begin(),
            Selection->GetHandles().end(),
            [SceneComponent](FObjectHandle Handle)
            {
                return SceneComponent->GetHandle() != Handle
                    && ContainsComponent(SceneComponent, Handle);
            });
    if (bContainsSelectedChild)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Component);
    }
    DrawComponentContextMenu(Component);
    if (bOpen && !Children.empty())
    {
        for (PSceneComponent* Child : Children)
        {
            DrawComponentNode(Child, Owner);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FSceneOutlinerPanel::DrawActorContextMenu(PActor* Actor)
{
    if (!ImGui::BeginPopupContextItem("ActorContext"))
    {
        return;
    }
    EnsureSelected(Actor);
    if (ImGui::BeginMenu("Add Component"))
    {
        if (ImGui::MenuItem("Scene Component")) AddSceneComponentToSelection();
        if (ImGui::MenuItem("Cube Component")) AddCubeComponentToSelection();
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy", "Ctrl+C")) CopySelectedObject();
    ImGui::BeginDisabled(!CanPasteClipboard());
    if (ImGui::MenuItem("Paste", "Ctrl+V"))
    {
        Queue->Enqueue([this]() { PasteClipboard(); });
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2")) BeginRenameObject(Actor);
    if (ImGui::MenuItem("Delete", "Delete")) QueueDestroy(Actor);
    ImGui::EndPopup();
}

void FSceneOutlinerPanel::DrawComponentContextMenu(PActorComponent* Component)
{
    if (!ImGui::BeginPopupContextItem("ComponentContext"))
    {
        return;
    }
    EnsureSelected(Component);
    if (ImGui::MenuItem("Copy", "Ctrl+C")) CopySelectedObject();
    ImGui::BeginDisabled(!CanPasteClipboard());
    if (ImGui::MenuItem("Paste", "Ctrl+V"))
    {
        Queue->Enqueue([this]() { PasteClipboard(); });
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2")) BeginRenameObject(Component);
    if (ImGui::MenuItem("Delete", "Delete")) QueueDestroy(Component);
    ImGui::EndPopup();
}

PObject* FSceneOutlinerPanel::GetSelectedObject() const
{
    return Selection != nullptr ? Selection->Resolve() : nullptr;
}

void FSceneOutlinerPanel::Select(PObject* Object)
{
    const ImGuiIO& IO = ImGui::GetIO();
    EEditorSelectionOperation Operation = EEditorSelectionOperation::Replace;
    if (IO.KeyShift)
    {
        Operation = IO.KeyCtrl
            ? EEditorSelectionOperation::RangeAdd
            : EEditorSelectionOperation::RangeReplace;
    }
    else if (IO.KeyCtrl)
    {
        Operation = EEditorSelectionOperation::Toggle;
    }
    SelectObject(Object, Operation, OrderedObjects);
}

void FSceneOutlinerPanel::EnsureSelected(PObject* Object)
{
    if (Selection != nullptr && !Selection->Contains(Object))
    {
        SelectObject(Object, EEditorSelectionOperation::Replace, OrderedObjects);
    }
}

void FSceneOutlinerPanel::SpawnEmptyActor()
{
    Queue->Enqueue([this]() { ApplyResult(Commands->SpawnActor(false)); });
}

void FSceneOutlinerPanel::SpawnCubeActor()
{
    Queue->Enqueue([this]() { ApplyResult(Commands->SpawnActor(true)); });
}

void FSceneOutlinerPanel::AddSceneComponentToSelection()
{
    Queue->Enqueue([this]() { ApplyResult(Commands->AddComponent(false)); });
}

void FSceneOutlinerPanel::AddCubeComponentToSelection()
{
    Queue->Enqueue([this]() { ApplyResult(Commands->AddComponent(true)); });
}

void FSceneOutlinerPanel::DestroySelectedObject()
{
    ApplyResult(Commands->DeleteSelectedObject());
}

void FSceneOutlinerPanel::QueueDestroy(PObject* Object)
{
    const FObjectHandle Handle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    Queue->Enqueue(
        [this, Handle]()
        {
            EnsureSelected(ResolveObject(Handle));
            DestroySelectedObject();
        });
}

void FSceneOutlinerPanel::CopySelectedObject()
{
    ApplyResult(Commands->CopySelectedObject());
}

void FSceneOutlinerPanel::PasteClipboard()
{
    ApplyResult(Commands->PasteClipboard());
}

bool FSceneOutlinerPanel::CanPasteClipboard() const
{
    return Commands != nullptr && Commands->CanPasteClipboard();
}

void FSceneOutlinerPanel::BeginRenameObject(PObject* Object)
{
    RequestRename(Object);
}

void FSceneOutlinerPanel::BuildObjectOrder(PWorld* CurrentWorld)
{
    OrderedObjects.clear();
    if (CurrentWorld == nullptr)
    {
        return;
    }
    OrderedObjects.push_back(CurrentWorld);
    for (PLevel* Level : CurrentWorld->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        OrderedObjects.push_back(Level);
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr)
            {
                continue;
            }
            OrderedObjects.push_back(Actor);
            const std::vector<PActorComponent*> Components = Actor->GetComponents();
            PSceneComponent* Root = Actor->GetRootComponent();
            if (Root != nullptr)
            {
                CollectComponentOrder(Root);
            }
            for (PActorComponent* Component : Components)
            {
                if (Component == Root)
                {
                    continue;
                }
                if (Component != nullptr
                    && Component->IsA(PSceneComponent::StaticClass())
                    && static_cast<PSceneComponent*>(Component)->GetAttachParent() != nullptr)
                {
                    continue;
                }
                CollectComponentOrder(Component);
            }
        }
    }
}

void FSceneOutlinerPanel::CollectComponentOrder(PActorComponent* Component)
{
    if (Component == nullptr)
    {
        return;
    }
    OrderedObjects.push_back(Component);
    if (!Component->IsA(PSceneComponent::StaticClass()))
    {
        return;
    }
    for (PSceneComponent* Child :
         static_cast<PSceneComponent*>(Component)->GetAttachChildren())
    {
        CollectComponentOrder(Child);
    }
}
}
