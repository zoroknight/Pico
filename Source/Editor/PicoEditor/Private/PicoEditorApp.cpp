#include "PicoEditorApp.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Render/SceneViewportRenderer.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Property.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
bool DrawVector3Control(const char* Label, FVector3& Value, float Speed = 0.1f)
{
    float Components[] = { Value.X, Value.Y, Value.Z };
    if (!ImGui::DragFloat3(Label, Components, Speed))
    {
        return false;
    }

    Value = FVector3(Components[0], Components[1], Components[2]);
    return true;
}

bool DrawRotatorControl(const char* Label, FRotator& Value)
{
    float Components[] = { Value.Pitch, Value.Yaw, Value.Roll };
    if (!ImGui::DragFloat3(Label, Components, 0.25f))
    {
        return false;
    }

    Value = FRotator(Components[0], Components[1], Components[2]).GetNormalized();
    return true;
}

bool DrawTransformControl(const char* Id, FTransform& Value)
{
    FRotator Rotation = Value.Rotation.Rotator();
    bool bChanged = false;

    ImGui::PushID(Id);
    if (ImGui::BeginTable(
            "TransformComponents",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Values", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Location");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        bChanged |= DrawVector3Control("##Location", Value.Translation);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        bChanged |= DrawRotatorControl("##Rotation", Rotation);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Scale");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        bChanged |= DrawVector3Control("##Scale", Value.Scale, 0.01f);
        ImGui::EndTable();
    }
    ImGui::PopID();

    if (bChanged)
    {
        Value.Rotation = Rotation.Quaternion();
    }
    return bChanged;
}

void PushObjectId(const PObject* Object)
{
    const FObjectHandle Handle = Object->GetHandle();
    ImGui::PushID(static_cast<int>(Handle.Index));
    ImGui::PushID(static_cast<int>(Handle.Serial));
}

void PopObjectId()
{
    ImGui::PopID();
    ImGui::PopID();
}

void BuildDefaultDockLayout(ImGuiID DockspaceId, const ImVec2& DockspaceSize)
{
    ImGui::DockBuilderRemoveNode(DockspaceId);
    ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockspaceId, DockspaceSize);

    ImGuiID CenterNodeId = DockspaceId;
    ImGuiID OutlinerNodeId = 0;
    ImGuiID DetailsNodeId = 0;
    ImGui::DockBuilderSplitNode(
        CenterNodeId,
        ImGuiDir_Left,
        0.20f,
        &OutlinerNodeId,
        &CenterNodeId);
    ImGui::DockBuilderSplitNode(
        CenterNodeId,
        ImGuiDir_Right,
        0.28f,
        &DetailsNodeId,
        &CenterNodeId);

    ImGui::DockBuilderDockWindow("Scene Outliner", OutlinerNodeId);
    ImGui::DockBuilderDockWindow("Viewport", CenterNodeId);
    ImGui::DockBuilderDockWindow("Details", DetailsNodeId);
    ImGui::DockBuilderFinish(DockspaceId);
}
}

FPicoEditorApp::FPicoEditorApp(PWorld* World, FSceneViewportRenderer* InViewportRenderer)
    : WorldHandle(World != nullptr ? World->GetHandle() : FObjectHandle {})
    , ViewportRenderer(InViewportRenderer)
{
    PActor* CubeActor = CreateActorWithRoot("CubeActor");
    if (CubeActor != nullptr)
    {
        PSceneComponent* Root = CubeActor->GetRootComponent();
        PCubeComponent* Child = CubeActor->CreateComponent<PCubeComponent>("CubeComponent");
        if (Root != nullptr
            && Child != nullptr
            && Child->AttachToComponent(Root, EAttachmentTransformRule::KeepRelative))
        {
            Child->SetRelativeLocation(FVector3(0.0f, 0.0f, 50.0f));
        }
    }
    Select(CubeActor != nullptr ? static_cast<PObject*>(CubeActor) : static_cast<PObject*>(World));
    SetStatus(
        CubeActor != nullptr
            ? "Created GameWorld.PersistentLevel.CubeActor"
            : "Editor world is ready, but the sample Actor could not be created",
        CubeActor == nullptr);
}

void FPicoEditorApp::Draw()
{
    if (SelectedObjectHandle.IsValid() && GetSelectedObject() == nullptr)
    {
        SelectedObjectHandle = {};
    }

    const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(MainViewport->WorkPos);
    ImGui::SetNextWindowSize(MainViewport->WorkSize);
    ImGui::SetNextWindowViewport(MainViewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(
        "PicoEditorDockspaceHost",
        nullptr,
        ImGuiWindowFlags_MenuBar
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus
            | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar())
    {
        DrawToolbar();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        DrawStatusBar();
        ImGui::EndMenuBar();
    }

    const ImGuiID DockspaceId = ImGui::GetID("PicoEditorDockspace");
    const ImVec2 DockspaceSize = ImGui::GetContentRegionAvail();
    const bool bNeedsDefaultLayout =
        bResetDockLayout || ImGui::DockBuilderGetNode(DockspaceId) == nullptr;
    ImGui::DockSpace(DockspaceId, ImVec2(0.0f, 0.0f));
    if (bNeedsDefaultLayout)
    {
        BuildDefaultDockLayout(DockspaceId, DockspaceSize);
        bResetDockLayout = false;
    }
    ImGui::End();

    if (ImGui::Begin("Scene Outliner"))
    {
        DrawSceneOutliner();
    }
    ImGui::End();

    if (ImGui::Begin(
            "Viewport",
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        DrawViewport(Available.x, Available.y);
    }
    ImGui::End();

    if (ImGui::Begin("Details"))
    {
        DrawDetails();
    }
    ImGui::End();
}

void FPicoEditorApp::DrawViewport(float Width, float Height)
{
    if (ViewportRenderer == nullptr || !ViewportRenderer->IsInitialized())
    {
        ImGui::TextDisabled("Viewport unavailable");
        return;
    }

    ImVec2 Available = ImGui::GetContentRegionAvail();
    Available.x = std::max(Width, 64.0f);
    Available.y = std::max(Height, 64.0f);
    const ImGuiIO& IO = ImGui::GetIO();
    const uint32 RenderWidth = static_cast<uint32>(std::clamp(
        Available.x * IO.DisplayFramebufferScale.x,
        64.0f,
        4096.0f));
    const uint32 RenderHeight = static_cast<uint32>(std::clamp(
        Available.y * IO.DisplayFramebufferScale.y,
        64.0f,
        4096.0f));

    const float YawRadians = DegreesToRadians(CameraYawDegrees);
    const float PitchRadians = DegreesToRadians(CameraPitchDegrees);
    const float CosPitch = std::cos(PitchRadians);
    const FVector3 CameraOffset(
        CameraDistance * CosPitch * std::cos(YawRadians),
        CameraDistance * CosPitch * std::sin(YawRadians),
        CameraDistance * std::sin(PitchRadians));

    FSceneView View;
    View.Target = CameraTarget;
    View.Position = CameraTarget + CameraOffset;
    if (!ViewportRenderer->Resize(RenderWidth, RenderHeight)
        || !ViewportRenderer->Render(GetWorld(), View))
    {
        ImGui::TextDisabled("Viewport render failed");
        return;
    }

    ImGui::Image(
        reinterpret_cast<ImTextureID>(
            static_cast<std::uintptr_t>(ViewportRenderer->GetColorTexture())),
        Available,
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f));

    if (!ImGui::IsItemHovered())
    {
        return;
    }

    if (IO.MouseWheel != 0.0f)
    {
        CameraDistance = std::clamp(
            CameraDistance * std::pow(0.88f, IO.MouseWheel),
            80.0f,
            5000.0f);
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        CameraYawDegrees -= IO.MouseDelta.x * 0.25f;
        CameraPitchDegrees = std::clamp(
            CameraPitchDegrees + IO.MouseDelta.y * 0.25f,
            -85.0f,
            85.0f);
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        const FVector3 Forward = (View.Target - View.Position).GetSafeNormal();
        const FVector3 Right =
            FVector3::Cross(Forward, FVector3::UpVector).GetSafeNormal();
        const FVector3 CameraUp = FVector3::Cross(Right, Forward).GetSafeNormal();
        const float PanSpeed = CameraDistance * 0.0015f;
        CameraTarget += Right * (-IO.MouseDelta.x * PanSpeed);
        CameraTarget += CameraUp * (IO.MouseDelta.y * PanSpeed);
    }
}

PWorld* FPicoEditorApp::GetWorld() const
{
    PObject* Object = ResolveObject(WorldHandle);
    return Object != nullptr && Object->IsA(PWorld::StaticClass())
        ? static_cast<PWorld*>(Object)
        : nullptr;
}

PObject* FPicoEditorApp::GetSelectedObject() const
{
    return ResolveObject(SelectedObjectHandle);
}

void FPicoEditorApp::DrawToolbar()
{
    PObject* SelectedObject = GetSelectedObject();
    PActor* SelectedActor =
        SelectedObject != nullptr && SelectedObject->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(SelectedObject)
        : nullptr;
    PSceneComponent* SelectedSceneComponent =
        SelectedObject != nullptr && SelectedObject->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(SelectedObject)
        : nullptr;

    if (ImGui::Button("Spawn Actor"))
    {
        SpawnActor();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(SelectedActor == nullptr || SelectedActor->GetRootComponent() != nullptr);
    if (ImGui::Button("Add Scene Root"))
    {
        AddRootToSelectedActor();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(SelectedSceneComponent == nullptr);
    if (ImGui::Button("Add Child"))
    {
        AddChildToSelectedComponent();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool bCanSetRoot =
        SelectedSceneComponent != nullptr
        && SelectedSceneComponent->GetOwner() != nullptr
        && SelectedSceneComponent->GetOwner()->GetRootComponent() != SelectedSceneComponent;
    ImGui::BeginDisabled(!bCanSetRoot);
    if (ImGui::Button("Set As Root"))
    {
        SetSelectedComponentAsRoot();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool bCanDestroy =
        SelectedObject != nullptr
        && (SelectedObject->IsA(PActor::StaticClass())
            || SelectedObject->IsA(PActorComponent::StaticClass()));
    ImGui::BeginDisabled(!bCanDestroy);
    if (ImGui::Button("Destroy"))
    {
        DestroySelectedObject();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Reset Layout"))
    {
        bResetDockLayout = true;
    }
}

void FPicoEditorApp::DrawSceneOutliner()
{
    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        ImGui::TextDisabled("No active world");
        return;
    }

    PushObjectId(World);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (SelectedObjectHandle == World->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpen = ImGui::TreeNodeEx(World->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(World);
    }
    if (bOpen)
    {
        for (PLevel* Level : World->GetLevels())
        {
            DrawLevelNode(Level);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FPicoEditorApp::DrawLevelNode(PLevel* Level)
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
    if (SelectedObjectHandle == Level->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpen = ImGui::TreeNodeEx(Level->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Level);
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

void FPicoEditorApp::DrawActorNode(PActor* Actor)
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
    if (SelectedObjectHandle == Actor->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpen = ImGui::TreeNodeEx(Actor->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Actor);
    }
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

void FPicoEditorApp::DrawComponentNode(PActorComponent* Component, PActor* Owner)
{
    if (Component == nullptr)
    {
        return;
    }

    PSceneComponent* SceneComponent =
        Component->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Component)
        : nullptr;
    const std::vector<PSceneComponent*> Children =
        SceneComponent != nullptr
        ? SceneComponent->GetAttachChildren()
        : std::vector<PSceneComponent*> {};

    PushObjectId(Component);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Children.empty())
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (SelectedObjectHandle == Component->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string Label = Component->GetName().ToString();
    if (Owner != nullptr && Owner->GetRootComponent() == Component)
    {
        Label += " [Root]";
    }

    const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Component);
    }
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

void FPicoEditorApp::DrawDetails()
{
    PObject* Object = GetSelectedObject();
    if (Object == nullptr)
    {
        ImGui::TextDisabled("No object selected");
        return;
    }

    DrawObjectIdentity(Object);

    if (Object->IsA(PActor::StaticClass()))
    {
        DrawActorDetails(static_cast<PActor*>(Object));
    }
    else if (Object->IsA(PSceneComponent::StaticClass()))
    {
        DrawSceneComponentDetails(static_cast<PSceneComponent*>(Object));
    }
    else if (Object->IsA(PLevel::StaticClass()))
    {
        PLevel* Level = static_cast<PLevel*>(Object);
        ImGui::Separator();
        ImGui::Text("Actors: %zu", Level->GetActors().size());
    }
    else if (Object->IsA(PWorld::StaticClass()))
    {
        PWorld* World = static_cast<PWorld*>(Object);
        ImGui::Separator();
        ImGui::Text("Levels: %zu", World->GetLevels().size());
        ImGui::Text("Tick: %llu", static_cast<unsigned long long>(World->GetTickCount()));
        ImGui::Text("Time: %.3f s", World->GetTimeSeconds());
    }
}

void FPicoEditorApp::DrawObjectIdentity(PObject* Object)
{
    const FObjectHandle Handle = Object->GetHandle();
    ImGui::Text("Name: %s", Object->GetName().ToString().c_str());
    ImGui::Text("Class: %s", Object->GetClass()->GetName().ToString().c_str());
    ImGui::Text("Path: %s", Object->GetPathName().c_str());
    ImGui::Text(
        "Outer: %s",
        Object->GetOuter() != nullptr ? Object->GetOuter()->GetPathName().c_str() : "None");
    ImGui::Text("Handle: {%u, %u}", Handle.Index, Handle.Serial);
}

void FPicoEditorApp::DrawActorDetails(PActor* Actor)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Transform");

    PSceneComponent* RootComponent = Actor->GetRootComponent();
    if (RootComponent == nullptr)
    {
        ImGui::TextDisabled("No RootComponent");
        if (ImGui::Button("Add Scene Root"))
        {
            AddRootToSelectedActor();
        }
    }
    else
    {
        FTransform Transform = Actor->GetActorTransform();
        if (DrawTransformControl("ActorTransform", Transform))
        {
            if (Actor->SetActorTransform(Transform))
            {
                SetStatus("Changed " + Actor->GetPathName() + " transform");
            }
        }
        ImGui::TextDisabled("Provided by %s", RootComponent->GetName().ToString().c_str());
    }

    ImGui::Separator();
    ImGui::Text("Components: %zu", Actor->GetComponents().size());
    ImGui::Text("Begun Play: %s", Actor->HasBegunPlay() ? "true" : "false");
}

void FPicoEditorApp::DrawSceneComponentDetails(PSceneComponent* Component)
{
    PActor* Owner = Component->GetOwner();
    ImGui::Separator();
    ImGui::Text(
        "Owner: %s",
        Owner != nullptr ? Owner->GetPathName().c_str() : "None");
    ImGui::Text(
        "Role: %s",
        Owner != nullptr && Owner->GetRootComponent() == Component ? "RootComponent" : "SceneComponent");
    PSceneComponent* Parent = Component->GetAttachParent();
    ImGui::Text(
        "Attach Parent: %s",
        Parent != nullptr ? Parent->GetPathName().c_str() : "None");
    ImGui::Text("Attach Children: %zu", Component->GetAttachChildren().size());
    ImGui::Text("Registered: %s", Component->IsRegistered() ? "true" : "false");

    ImGui::Separator();
    ImGui::TextUnformatted("World Transform");
    FTransform WorldTransform = Component->GetWorldTransform();
    ImGui::BeginDisabled();
    DrawTransformControl("WorldTransform", WorldTransform);
    ImGui::EndDisabled();

    DrawReflectedProperties(Component);
}

void FPicoEditorApp::DrawReflectedProperties(PObject* Object)
{
    const std::vector<const PProperty*> Properties = GetAllProperties(Object->GetClass());
    if (Properties.empty())
    {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Reflected Properties");
    if (ImGui::BeginTable(
            "ReflectedProperties",
            2,
            ImGuiTableFlags_SizingStretchProp
                | ImGuiTableFlags_BordersInnerH
                | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        for (const PProperty* Property : Properties)
        {
            DrawPropertyEditor(Object, Property);
        }
        ImGui::EndTable();
    }
}

void FPicoEditorApp::DrawPropertyEditor(PObject* Object, const PProperty* Property)
{
    const std::string PropertyName = Property->GetName().ToString();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(PropertyName.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushID(Property);

    bool bChanged = false;
    switch (Property->GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        bChanged =
            Property->GetValue(Object, Value)
            && ImGui::InputInt("##Value", &Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        bChanged =
            Property->GetValue(Object, Value)
            && ImGui::DragFloat("##Value", &Value, 0.1f)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        bChanged =
            Property->GetValue(Object, Value)
            && ImGui::Checkbox("##Value", &Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        bChanged =
            Property->GetValue(Object, Value)
            && DrawVector3Control("##Value", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        bChanged =
            Property->GetValue(Object, Value)
            && DrawRotatorControl("##Value", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        bChanged =
            Property->GetValue(Object, Value)
            && DrawTransformControl("PropertyTransform", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    }

    if (bChanged)
    {
        SetStatus("Changed " + Object->GetPathName() + "." + PropertyName);
    }
    ImGui::PopID();
}

void FPicoEditorApp::DrawStatusBar()
{
    if (Status.empty())
    {
        ImGui::TextDisabled("Ready");
        return;
    }

    const ImVec4 Color = bStatusIsError
        ? ImVec4(0.95f, 0.42f, 0.35f, 1.0f)
        : ImVec4(0.35f, 0.78f, 0.66f, 1.0f);
    ImGui::TextColored(Color, "%s", Status.c_str());
}

PActor* FPicoEditorApp::CreateActorWithRoot(std::string Name)
{
    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        return nullptr;
    }

    PActor* Actor = World->SpawnActor<PActor>(Name);
    if (Actor == nullptr)
    {
        return nullptr;
    }

    if (AddSceneRoot(Actor) == nullptr)
    {
        World->DestroyActor(Actor);
        return nullptr;
    }
    return Actor;
}

PSceneComponent* FPicoEditorApp::AddSceneRoot(PActor* Actor)
{
    if (Actor == nullptr || Actor->GetRootComponent() != nullptr)
    {
        return nullptr;
    }

    PSceneComponent* Root = Actor->CreateComponent<PSceneComponent>("RootComponent");
    if (Root == nullptr || !Actor->SetRootComponent(Root))
    {
        return nullptr;
    }
    return Root;
}

void FPicoEditorApp::SpawnActor()
{
    std::string Name;
    PActor* Actor = nullptr;
    do
    {
        Name = "Actor_" + std::to_string(NextActorNumber++);
        Actor = CreateActorWithRoot(Name);
    }
    while (Actor == nullptr && NextActorNumber < 10000);

    if (Actor == nullptr)
    {
        SetStatus("Failed to spawn an Actor", true);
        return;
    }

    Select(Actor);
    SetStatus("Spawned " + Actor->GetPathName());
}

void FPicoEditorApp::AddRootToSelectedActor()
{
    PObject* Object = GetSelectedObject();
    PActor* Actor =
        Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object)
        : nullptr;
    PSceneComponent* Root = AddSceneRoot(Actor);
    if (Root == nullptr)
    {
        SetStatus("Selected Actor could not create a scene root", true);
        return;
    }

    Select(Root);
    SetStatus("Added " + Root->GetPathName());
}

void FPicoEditorApp::AddChildToSelectedComponent()
{
    PObject* Object = GetSelectedObject();
    PSceneComponent* Parent =
        Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object)
        : nullptr;
    PActor* Owner = Parent != nullptr ? Parent->GetOwner() : nullptr;
    if (Owner == nullptr)
    {
        SetStatus("Selected object cannot own a scene child", true);
        return;
    }

    PSceneComponent* Child = nullptr;
    std::string Name;
    do
    {
        Name = "SceneComponent_" + std::to_string(NextComponentNumber++);
        Child = Owner->CreateComponent<PSceneComponent>(Name);
    }
    while (Child == nullptr && NextComponentNumber < 10000);

    if (Child == nullptr
        || !Child->AttachToComponent(Parent, EAttachmentTransformRule::KeepRelative))
    {
        if (Child != nullptr)
        {
            DestroyObject(Child);
        }
        SetStatus("Could not add a child to the selected component", true);
        return;
    }

    Select(Child);
    SetStatus("Attached " + Child->GetPathName() + " to " + Parent->GetPathName());
}

void FPicoEditorApp::SetSelectedComponentAsRoot()
{
    PObject* Object = GetSelectedObject();
    PSceneComponent* Component =
        Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object)
        : nullptr;
    PActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
    if (Owner == nullptr || !Owner->SetRootComponent(Component))
    {
        SetStatus("Selected component could not become the root", true);
        return;
    }

    SetStatus("Set " + Component->GetPathName() + " as RootComponent");
}

void FPicoEditorApp::DestroySelectedObject()
{
    PObject* Object = GetSelectedObject();
    if (Object == nullptr)
    {
        SetStatus("No object selected", true);
        return;
    }

    const std::string Path = Object->GetPathName();
    bool bDestroyed = false;
    if (Object->IsA(PActor::StaticClass()))
    {
        PActor* Actor = static_cast<PActor*>(Object);
        PWorld* World = Actor->GetWorld();
        bDestroyed = World != nullptr && World->DestroyActor(Actor);
    }
    else if (Object->IsA(PActorComponent::StaticClass()))
    {
        bDestroyed = DestroyObject(Object);
    }

    if (!bDestroyed)
    {
        SetStatus("Could not destroy " + Path, true);
        return;
    }

    SelectedObjectHandle = {};
    SetStatus("Destroyed " + Path);
}

void FPicoEditorApp::Select(PObject* Object)
{
    SelectedObjectHandle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
}

void FPicoEditorApp::SetStatus(std::string Message, bool bIsError)
{
    Status = std::move(Message);
    bStatusIsError = bIsError;
}
}
