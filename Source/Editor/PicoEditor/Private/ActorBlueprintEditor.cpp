#include "ActorBlueprintEditor.h"

#include "DetailsPanel.h"

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Core/Paths.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorBlueprint.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/Controller.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/ScriptComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Property.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
FOpenGLProcedure LoadOpenGLProcedure(const char* Name)
{
    return reinterpret_cast<FOpenGLProcedure>(glfwGetProcAddress(Name));
}

void CopyToBuffer(std::string_view Value, std::span<char> Buffer)
{
    std::fill(Buffer.begin(), Buffer.end(), '\0');
    const std::size_t Count = std::min(Value.size(), Buffer.size() - 1);
    std::copy_n(Value.data(), Count, Buffer.data());
}

bool ResolveAssetFile(const FAssetPath& AssetPath, std::filesystem::path& OutFile)
{
    return AssetPath.IsValid()
        && FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Content,
            std::filesystem::path(AssetPath.GetGameRelativePath()),
            OutFile);
}

void BuildAxisSegment(
    PWorld* World,
    const char* Name,
    const FVector3& Start,
    const FVector3& End,
    float Thickness,
    const FVector3& Color)
{
    PActor* Actor = World != nullptr ? World->SpawnActor<PActor>(Name) : nullptr;
    PCubeComponent* Cube = Actor != nullptr
        ? Actor->CreateComponent<PCubeComponent>("Axis") : nullptr;
    if (Actor == nullptr || Cube == nullptr || !Actor->SetRootComponent(Cube)) return;
    const FVector3 Direction = End - Start;
    const float Length = Direction.Size();
    if (Length <= SmallNumber) return;
    const float HorizontalLength = std::sqrt(
        Direction.X * Direction.X + Direction.Y * Direction.Y);
    const FRotator Rotation(
        -RadiansToDegrees(std::atan2(Direction.Z, HorizontalLength)),
        RadiansToDegrees(std::atan2(Direction.Y, Direction.X)),
        0.0f);
    Actor->SetActorTransform(FTransform(
        Rotation,
        (Start + End) * 0.5f,
        FVector3::OneVector));
    Cube->SetExtent(FVector3(Length * 0.5f, Thickness, Thickness));
    Cube->SetColor(Color);
    Cube->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void BuildAxisArrow(
    PWorld* World,
    const char* NamePrefix,
    const FVector3& Axis,
    const FVector3& HeadSide,
    const FVector3& Color)
{
    const FVector3 Start = Axis * 5.0f;
    const FVector3 Tip = Axis * 145.0f;
    const FVector3 HeadBase = Axis * 119.0f;
    const std::string Prefix(NamePrefix);
    BuildAxisSegment(
        World, (Prefix + "Shaft").c_str(), Start, Tip, 2.5f, Color);
    BuildAxisSegment(
        World, (Prefix + "HeadA").c_str(), Tip,
        HeadBase + HeadSide * 12.0f, 3.5f, Color);
    BuildAxisSegment(
        World, (Prefix + "HeadB").c_str(), Tip,
        HeadBase - HeadSide * 12.0f, 3.5f, Color);
}
}

struct FActorBlueprintEditor::FImpl
{
    FEngineLoop* EngineLoop = nullptr;
    FStatus SetStatus;
    FSpawnInLevel SpawnInLevel;
    FAssetCreated AssetCreated;
    FWorldChanged WorldChanged;
    std::unique_ptr<FSceneViewportRenderer> Renderer;
    PWorld* PreviewWorld = nullptr;
    PActor* PreviewActor = nullptr;
    FEditorSelection Selection;
    FDetailsPanel Details;
    FAssetPath OpenedAsset;
    std::array<char, 256> CreateFolder {};
    std::array<char, 128> CreateName {};
    std::array<char, 128> ParentFilter {};
    const PClass* CreateParentClass = nullptr;
    FVector3 ViewTarget = FVector3(0.0f, 0.0f, 80.0f);
    float ViewDistance = 430.0f;
    float ViewYaw = -135.0f;
    float ViewPitch = -18.0f;
    bool bOpen = false;
    bool bCreateOpen = false;
    bool bDirty = false;

    ~FImpl()
    {
        ResetPreview();
        if (Renderer != nullptr) Renderer->Shutdown();
    }

    void Report(std::string Message, bool bError = false) const
    {
        if (SetStatus) SetStatus(std::move(Message), bError);
    }

    bool EnsureRenderer()
    {
        if (Renderer != nullptr) return Renderer->IsInitialized();
        Renderer = std::make_unique<FSceneViewportRenderer>();
        if (!Renderer->Initialize(&LoadOpenGLProcedure))
        {
            Renderer.reset();
            Report("Could not initialize Actor Blueprint preview renderer", true);
            return false;
        }
        return true;
    }

    void ResetPreview()
    {
        Selection.Clear();
        PreviewActor = nullptr;
        if (PreviewWorld != nullptr)
        {
            RemoveFromRoot(PreviewWorld);
            DestroyObjectTree(PreviewWorld);
            PreviewWorld = nullptr;
        }
    }

    bool BuildPreview()
    {
        ResetPreview();
        const PClass* GeneratedClass =
            FindActorBlueprintGeneratedClass(OpenedAsset);
        if (EngineLoop == nullptr || GeneratedClass == nullptr) return false;
        PreviewWorld = NewObject<PWorld>(
            nullptr, "ActorBlueprintPreviewWorld", EObjectFlags::Transient);
        if (PreviewWorld == nullptr || !AddToRoot(PreviewWorld)
            || !PreviewWorld->Initialize())
        {
            ResetPreview();
            return false;
        }
        PreviewWorld->SetAssetServices(
            &EngineLoop->GetAssetRegistry(), &EngineLoop->GetAssetManager());
        FActorSpawnParameters Parameters;
        Parameters.Name = FName("BlueprintPreviewActor");
        Parameters.ObjectFlags = EObjectFlags::Transient;
        PreviewActor = PreviewWorld->SpawnActor(GeneratedClass, Parameters);
        if (PreviewActor == nullptr)
        {
            ResetPreview();
            return false;
        }
        BuildAxisArrow(
            PreviewWorld, "ActorForwardX", FVector3::ForwardVector,
            FVector3::RightVector, {0.9f, 0.12f, 0.1f});
        BuildAxisArrow(
            PreviewWorld, "ActorRightY", FVector3::RightVector,
            FVector3::ForwardVector, {0.15f, 0.8f, 0.25f});
        BuildAxisArrow(
            PreviewWorld, "ActorUpZ", FVector3::UpVector,
            FVector3::ForwardVector, {0.15f, 0.35f, 0.95f});
        for (PLevel* Level : PreviewWorld->GetLevels())
        {
            if (Level == nullptr) continue;
            for (PActor* Actor : Level->GetActors())
            {
                if (Actor != nullptr) Actor->RegisterAllComponents();
            }
        }
        Selection.Set(PreviewActor);
        return true;
    }

    void Open(const FAssetPath& AssetPath)
    {
        if (EngineLoop == nullptr) return;
        EActorBlueprintError Error = EActorBlueprintError::None;
        if (!CompileActorBlueprint(
                AssetPath, EngineLoop->GetAssetRegistry(), &Error))
        {
            Report(
                "Could not compile Actor Blueprint: "
                    + std::string(ToString(Error)),
                true);
            return;
        }
        OpenedAsset = AssetPath;
        bOpen = true;
        bDirty = false;
        if (!EnsureRenderer() || !BuildPreview())
            Report("Could not construct Actor Blueprint preview", true);
    }

    void Save()
    {
        std::filesystem::path File;
        EActorBlueprintError Error = EActorBlueprintError::None;
        const PClass* GeneratedClass =
            FindActorBlueprintGeneratedClass(OpenedAsset);
        FActorBlueprintReinstancer Reinstancer(GeneratedClass);
        if (PreviewActor == nullptr || !ResolveAssetFile(OpenedAsset, File)
            || !SaveActorBlueprintDefaults(
                File, OpenedAsset, PreviewActor, &Error))
        {
            Report(
                "Could not save Actor Blueprint: "
                    + std::string(ToString(Error)),
                true);
            return;
        }

        FActorBlueprintReinstanceReport RefreshReport;
        PWorld* EditorWorld = EngineLoop != nullptr
            ? EngineLoop->GetWorld() : nullptr;
        if (Reinstancer.IsValid() && EditorWorld != nullptr
            && !Reinstancer.RefreshWorld(EditorWorld, &RefreshReport))
        {
            Report(
                "Actor Blueprint saved, but open World instances could not be refreshed; reload the map",
                true);
        }
        else if (RefreshReport.RefreshedActorCount > 0 && WorldChanged)
        {
            WorldChanged();
        }
        if (!BuildPreview())
        {
            Report("Actor Blueprint saved, but its preview could not be rebuilt", true);
            return;
        }
        bDirty = false;
        Report(
            "Compiled and saved " + std::string(OpenedAsset.ToString())
                + "; refreshed "
                + std::to_string(RefreshReport.RefreshedActorCount)
                + " of " + std::to_string(RefreshReport.MatchedActorCount)
                + " placed instance(s)");
    }

    void AddScriptComponent()
    {
        if (PreviewActor == nullptr) return;
        unsigned int Number = 1;
        PScriptComponent* Component = nullptr;
        do
        {
            Component = PreviewActor->CreateComponent<PScriptComponent>(
                "ScriptComponent_" + std::to_string(Number++));
        }
        while (Component == nullptr && Number < 10000);
        if (Component == nullptr)
        {
            Report("Could not add Script Component", true);
            return;
        }
        Selection.Set(Component);
        bDirty = true;
        Report("Added Script Component; configure GraphAsset, then Compile & Save");
    }

    void DrawCreatePopup()
    {
        if (bCreateOpen)
        {
            ImGui::OpenPopup("Create Actor Blueprint");
            bCreateOpen = false;
        }
        if (!ImGui::BeginPopupModal(
                "Create Actor Blueprint", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::InputText("Folder", CreateFolder.data(), CreateFolder.size());
        ImGui::InputText("Asset Name", CreateName.data(), CreateName.size());
        ImGui::InputTextWithHint(
            "##ParentSearch", "Filter parent Actor class",
            ParentFilter.data(), ParentFilter.size());
        const std::string ParentLabel = CreateParentClass != nullptr
            ? CreateParentClass->GetName().ToString() : "Select Parent Class";
        if (ImGui::BeginCombo("Parent Class", ParentLabel.c_str()))
        {
            std::string Filter = ParentFilter.data();
            std::transform(Filter.begin(), Filter.end(), Filter.begin(),
                [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
            for (const PClass* Class : FClassRegistry::GetClasses())
            {
                if (Class == nullptr
                    || !Class->IsChildOf(PActor::StaticClass())
                    || !Class->CanConstruct())
                    continue;
                if (FindActorBlueprintAsset(Class).IsValid()) continue;
                std::string Name = Class->GetName().ToString();
                std::string Lower = Name;
                std::transform(Lower.begin(), Lower.end(), Lower.begin(),
                    [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
                if (!Filter.empty() && Lower.find(Filter) == std::string::npos) continue;
                if (ImGui::Selectable(Name.c_str(), CreateParentClass == Class))
                    CreateParentClass = Class;
            }
            ImGui::EndCombo();
        }

        const bool bCanCreate = CreateParentClass != nullptr
            && CreateFolder[0] != '\0' && CreateName[0] != '\0';
        ImGui::BeginDisabled(!bCanCreate);
        if (ImGui::Button("Create"))
        {
            std::string Folder = CreateFolder.data();
            while (!Folder.empty() && Folder.back() == '/') Folder.pop_back();
            FAssetPath AssetPath;
            const std::string VirtualPath = Folder + "/" + CreateName.data()
                + ".pblueprint";
            std::filesystem::path File;
            EActorBlueprintError Error = EActorBlueprintError::None;
            if (!FAssetPath::TryParse(VirtualPath, AssetPath)
                || !ResolveAssetFile(AssetPath, File)
                || std::filesystem::exists(File)
                || !CreateActorBlueprintAsset(
                    File, AssetPath, CreateParentClass, &Error))
            {
                Report(
                    "Could not create Actor Blueprint: "
                        + std::string(ToString(Error)),
                    true);
            }
            else
            {
                FAssetScanReport Scan;
                EngineLoop->GetAssetRegistry().ScanProjectContent(&Scan);
                CompileActorBlueprint(AssetPath, EngineLoop->GetAssetRegistry(), &Error);
                if (AssetCreated) AssetCreated(AssetPath);
                Open(AssetPath);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void DrawComponentTree()
    {
        if (PreviewActor == nullptr) return;
        PActorComponent* ComponentToDelete = nullptr;
        const auto DrawComponentContextMenu =
            [this, &ComponentToDelete](PActorComponent* Component)
            {
                if (!ImGui::BeginPopupContextItem()) return;
                Selection.Set(Component);
                const bool bCanDelete =
                    PreviewActor->CanDestroyBlueprintComponent(Component);
                ImGui::BeginDisabled(!bCanDelete);
                if (ImGui::MenuItem("Delete Component", "Delete"))
                    ComponentToDelete = Component;
                ImGui::EndDisabled();
                if (!bCanDelete)
                {
                    ImGui::Separator();
                    ImGui::TextDisabled(
                        "Inherited, root, or parent components cannot be deleted");
                }
                ImGui::EndPopup();
            };
        if (ImGui::Selectable(
                (PreviewActor->GetClass()->GetName().ToString() + " (Self)").c_str(),
                Selection.Resolve() == PreviewActor))
            Selection.Set(PreviewActor);
        const auto DrawSceneNode = [this, &DrawComponentContextMenu](
            auto&& Self,
            PSceneComponent* Component) -> void
        {
            if (Component == nullptr) return;
            const std::string Label = Component->GetName().ToString()
                + "  [" + Component->GetClass()->GetName().ToString() + "]";
            const std::vector<PSceneComponent*> Children = Component->GetAttachChildren();
            ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow
                | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (Children.empty()) Flags |= ImGuiTreeNodeFlags_Leaf;
            if (Selection.Resolve() == Component) Flags |= ImGuiTreeNodeFlags_Selected;
            const bool bOpenNode = ImGui::TreeNodeEx(Component, Flags, "%s", Label.c_str());
            if (ImGui::IsItemClicked())
                Selection.Set(Component);
            DrawComponentContextMenu(Component);
            if (bOpenNode)
            {
                for (PSceneComponent* Child : Children) Self(Self, Child);
                ImGui::TreePop();
            }
        };
        for (PActorComponent* Component : PreviewActor->GetComponents())
        {
            auto* SceneComponent = Component != nullptr
                    && Component->IsA(PSceneComponent::StaticClass())
                ? static_cast<PSceneComponent*>(Component) : nullptr;
            if (SceneComponent != nullptr && SceneComponent->GetAttachParent() == nullptr)
                DrawSceneNode(DrawSceneNode, SceneComponent);
        }
        for (PActorComponent* Component : PreviewActor->GetComponents())
        {
            if (Component == nullptr || Component->IsA(PSceneComponent::StaticClass()))
                continue;
            const std::string Label = Component->GetName().ToString()
                + "  [" + Component->GetClass()->GetName().ToString() + "]";
            if (ImGui::Selectable(Label.c_str(), Selection.Resolve() == Component))
                Selection.Set(Component);
            DrawComponentContextMenu(Component);
        }
        if (ComponentToDelete != nullptr)
        {
            const std::string DeletedName =
                ComponentToDelete->GetName().ToString();
            if (!PreviewActor->DestroyBlueprintComponent(ComponentToDelete))
            {
                Report("Could not delete Blueprint component", true);
            }
            else
            {
                Selection.Set(PreviewActor);
                bDirty = true;
                Report(
                    "Deleted " + DeletedName
                        + "; Compile & Save to refresh placed instances");
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Red: Actor +X Forward");
        ImGui::TextDisabled("Green: Actor +Y Right");
        ImGui::TextDisabled("Blue: Actor +Z Up");
        ImGui::TextWrapped(
            "Select the skeletal mesh component and adjust Relative Transform. "
            "The Character Profile visual correction is composed after it.");
    }

    void DrawViewport()
    {
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        const uint32 Width = static_cast<uint32>(std::max(Available.x, 1.0f));
        const uint32 Height = static_cast<uint32>(std::max(Available.y, 1.0f));
        if (Renderer == nullptr || PreviewWorld == nullptr
            || Width < 2 || Height < 2)
            return;
        const float Yaw = DegreesToRadians(ViewYaw);
        const float Pitch = DegreesToRadians(ViewPitch);
        const float CosPitch = std::cos(Pitch);
        const FVector3 Direction(
            CosPitch * std::cos(Yaw),
            CosPitch * std::sin(Yaw),
            std::sin(Pitch));
        FSceneView View;
        View.Target = ViewTarget;
        View.Position = ViewTarget + Direction * ViewDistance;
        std::vector<FObjectHandle> Selected;
        if (PObject* Object = Selection.Resolve()) Selected.push_back(Object->GetHandle());
        FSceneViewportRenderOptions RenderOptions;
        RenderOptions.bDrawWorldAxes = false;
        if (!Renderer->Resize(Width, Height)
            || !Renderer->Render(
                PreviewWorld,
                EngineLoop->GetAssetRegistry(),
                EngineLoop->GetAssetManager(),
                View,
                Selected,
                RenderOptions))
        {
            ImGui::TextDisabled("Preview render failed");
            return;
        }
        ImGui::Image(
            reinterpret_cast<ImTextureID>(
                static_cast<std::uintptr_t>(Renderer->GetColorTexture())),
            Available,
            ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        const ImGuiIO& IO = ImGui::GetIO();
        const bool bHovered = ImGui::IsItemHovered();
        if (bHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            const ImVec2 Delta = IO.MouseDelta;
            ViewYaw -= Delta.x * 0.35f;
            ViewPitch = std::clamp(ViewPitch + Delta.y * 0.35f, -89.0f, 89.0f);
        }
        if (bHovered && IO.MouseWheel != 0.0f)
            ViewDistance = std::clamp(
                ViewDistance * std::pow(0.85f, IO.MouseWheel),
                20.0f, 5000.0f);

        const FVector3 ViewForward = -Direction;
        FVector3 ViewRight = FVector3::Cross(
            ViewForward, FVector3::UpVector).GetSafeNormal();
        if (ViewRight.IsNearlyZero()) ViewRight = FVector3::RightVector;
        const FVector3 ViewUp =
            FVector3::Cross(ViewRight, ViewForward).GetSafeNormal();
        if (bHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            const float PanScale = std::max(ViewDistance * 0.0025f, 0.05f);
            ViewTarget += ViewRight * (-IO.MouseDelta.x * PanScale)
                + ViewUp * (IO.MouseDelta.y * PanScale);
        }

        if (bHovered && !IO.WantTextInput)
        {
            FVector3 Movement = FVector3::ZeroVector;
            if (ImGui::IsKeyDown(ImGuiKey_W)) Movement += ViewForward;
            if (ImGui::IsKeyDown(ImGuiKey_S)) Movement -= ViewForward;
            if (ImGui::IsKeyDown(ImGuiKey_D)) Movement += ViewRight;
            if (ImGui::IsKeyDown(ImGuiKey_A)) Movement -= ViewRight;
            if (ImGui::IsKeyDown(ImGuiKey_E)) Movement += FVector3::UpVector;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) Movement -= FVector3::UpVector;
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) Movement += ViewRight;
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) Movement -= ViewRight;
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) Movement += ViewUp;
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) Movement -= ViewUp;
            if (Movement.Normalize())
            {
                const bool bFast = ImGui::IsKeyDown(ImGuiKey_LeftShift)
                    || ImGui::IsKeyDown(ImGuiKey_RightShift);
                const float Speed = std::max(ViewDistance * 0.8f, 50.0f)
                    * (bFast ? 4.0f : 1.0f);
                ViewTarget += Movement * Speed
                    * std::clamp(IO.DeltaTime, 0.0f, 0.1f);
            }
        }
    }

    void DrawDetails()
    {
        Details.Draw(
            Selection,
            [](const std::string&, std::string, bool, bool) { return true; },
            [this](const std::string&, bool Changed, bool, bool Applied)
            { if (Changed && Applied) bDirty = true; },
            [this](std::string Message, bool bError)
            { Report(std::move(Message), bError); },
            []() {},
            EngineLoop->GetAssetRegistry(),
            [](const FAssetPath&) {},
            {},
            [this](FObjectHandle Handle, FName PropertyName, const FAssetPath& Value)
            {
                PObject* Object = ResolveObject(Handle);
                const PProperty* Property = Object != nullptr
                    ? Object->GetClass()->FindProperty(PropertyName) : nullptr;
                if (Property != nullptr && Property->SetValue(Object, Value)) bDirty = true;
            });
        PObject* Selected = Selection.Resolve();
        if (Selected != nullptr
            && Selected->IsA(PSkeletalMeshComponent::StaticClass()))
        {
            auto* Mesh = static_cast<PSkeletalMeshComponent*>(Selected);
            const FTransform& ComponentTransform = Mesh->GetRelativeTransform();
            const FTransform& ProfileTransform =
                Mesh->GetCharacterProfileVisualTransform();
            const FTransform VisualTransform = Mesh->GetVisualWorldTransform();
            ImGui::Separator();
            ImGui::TextUnformatted("Forward Relationship");
            ImGui::Text("Actor gameplay forward: +X");
            ImGui::Text(
                "Component offset: P %.1f  Y %.1f  R %.1f",
                ComponentTransform.Rotation.Rotator().Pitch,
                ComponentTransform.Rotation.Rotator().Yaw,
                ComponentTransform.Rotation.Rotator().Roll);
            ImGui::Text(
                "Profile visual fix: P %.1f  Y %.1f  R %.1f",
                ProfileTransform.Rotation.Rotator().Pitch,
                ProfileTransform.Rotation.Rotator().Yaw,
                ProfileTransform.Rotation.Rotator().Roll);
            ImGui::Text(
                "Final visual yaw: %.1f",
                VisualTransform.Rotation.Rotator().Yaw);
            ImGui::TextWrapped(
                "Tune Relative Transform here. The red preview axis remains "
                "the Actor +X direction used by movement, physics, root motion, and networking.");
        }
        else if (Selected != nullptr
            && Selected->IsA(PSpringArmComponent::StaticClass()))
        {
            auto* SpringArm = static_cast<PSpringArmComponent*>(Selected);
            PActor* Owner = SpringArm->GetOwner();
            auto* Pawn = Owner != nullptr && Owner->IsA(PPawn::StaticClass())
                ? static_cast<PPawn*>(Owner) : nullptr;
            PController* Controller = Pawn != nullptr ? Pawn->GetController() : nullptr;
            const float ActorYaw = Owner != nullptr ? Owner->GetActorRotation().Yaw : 0.0f;
            const float ControlYaw = Controller != nullptr
                ? Controller->GetControlRotation().Yaw : 0.0f;
            ImGui::Separator();
            ImGui::TextUnformatted("Third Person Camera Rotation");
            ImGui::Text("Actor yaw: %.1f", ActorYaw);
            if (Controller != nullptr) ImGui::Text("Control yaw: %.1f", ControlYaw);
            else ImGui::TextDisabled("Control yaw: preview has no Controller");
            ImGui::Text("Camera target yaw: %.1f", SpringArm->GetTargetRotation().Yaw);
            ImGui::TextWrapped(
                "Use Pawn Control Rotation keeps the camera target driven by ControlRotation. "
                "Character movement may rotate the Actor without rotating the camera view.");
        }
    }

    void DrawEditor()
    {
        if (!bOpen) return;
        std::string Title = "Actor Blueprint - " + std::string(OpenedAsset.ToString());
        if (bDirty) Title += " *";
        Title += "###PicoActorBlueprintEditor";
        ImGuiWindowClass WindowClass;
        WindowClass.ClassId = static_cast<ImGuiID>(0x50414250u);
        WindowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        WindowClass.DockingAllowUnclassed = false;
        ImGui::SetNextWindowClass(&WindowClass);
        ImGui::SetNextWindowSize(ImVec2(1380.0f, 840.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(900.0f, 560.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (!ImGui::Begin(
                Title.c_str(), &bOpen,
                ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking))
        {
            ImGui::End();
            return;
        }
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::MenuItem("Compile & Save", "Ctrl+S")) Save();
            if (ImGui::MenuItem("Reset Preview")) BuildPreview();
            if (ImGui::MenuItem("Add Script Component")) AddScriptComponent();
            ImGui::BeginDisabled(PreviewActor == nullptr);
            if (ImGui::MenuItem("Spawn In Level") && SpawnInLevel)
                SpawnInLevel(OpenedAsset);
            ImGui::EndDisabled();
            ImGui::EndMenuBar();
        }
        const PClass* Generated = FindActorBlueprintGeneratedClass(OpenedAsset);
        ImGui::Text("Generated Class: %s", Generated != nullptr
            ? Generated->GetName().ToString().c_str() : "None");
        ImGui::SameLine();
        ImGui::TextDisabled("| CDO + reflected component templates");
        if (ImGui::BeginTable("ActorBlueprintLayout", 3, ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Components", ImGuiTableColumnFlags_WidthFixed, 260.0f);
            ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 450.0f);
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("BlueprintComponents")) DrawComponentTree();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("BlueprintViewport")) DrawViewport();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("BlueprintDetails")) DrawDetails();
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::End();
    }
};

FActorBlueprintEditor::FActorBlueprintEditor(
    FEngineLoop* EngineLoop,
    FStatus SetStatus,
    FSpawnInLevel SpawnInLevel,
    FAssetCreated AssetCreated,
    FWorldChanged WorldChanged)
    : Impl(std::make_unique<FImpl>())
{
    Impl->EngineLoop = EngineLoop;
    Impl->SetStatus = std::move(SetStatus);
    Impl->SpawnInLevel = std::move(SpawnInLevel);
    Impl->AssetCreated = std::move(AssetCreated);
    Impl->WorldChanged = std::move(WorldChanged);
    CopyToBuffer("/Game/Characters", Impl->CreateFolder);
    CopyToBuffer("BP_NewCharacter", Impl->CreateName);
}

FActorBlueprintEditor::~FActorBlueprintEditor() = default;

void FActorBlueprintEditor::OpenCreate()
{
    Impl->CreateParentClass = nullptr;
    Impl->ParentFilter.fill('\0');
    Impl->bCreateOpen = true;
}

void FActorBlueprintEditor::OpenAsset(const FAssetPath& AssetPath)
{
    Impl->Open(AssetPath);
}

void FActorBlueprintEditor::Draw()
{
    Impl->DrawCreatePopup();
    Impl->DrawEditor();
}

bool FActorBlueprintEditor::IsOpen() const
{
    return Impl->bOpen;
}

FAssetPath FActorBlueprintEditor::GetOpenedAsset() const
{
    return Impl->bOpen ? Impl->OpenedAsset : FAssetPath {};
}
}
