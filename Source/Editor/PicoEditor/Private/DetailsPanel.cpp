#include "DetailsPanel.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Property.h"

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
struct FEditorControlState
{
    bool bChanged = false;
    bool bActivated = false;
    bool bActive = false;

    void IncludeLastItem(bool bItemChanged)
    {
        bChanged |= bItemChanged;
        bActivated |= ImGui::IsItemActivated();
        bActive |= ImGui::IsItemActive();
    }

    void Include(const FEditorControlState& Other)
    {
        bChanged |= Other.bChanged;
        bActivated |= Other.bActivated;
        bActive |= Other.bActive;
    }
};

FEditorControlState DrawFloat3Control(
    const char* Label,
    float* Components,
    float Speed)
{
    FEditorControlState State;
    const float Spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float ComponentWidth =
        std::max((ImGui::GetContentRegionAvail().x - Spacing * 2.0f) / 3.0f, 1.0f);

    ImGui::PushID(Label);
    for (int ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
    {
        if (ComponentIndex > 0)
        {
            ImGui::SameLine(0.0f, Spacing);
        }
        ImGui::PushID(ComponentIndex);
        ImGui::SetNextItemWidth(ComponentWidth);
        const bool bChanged =
            ImGui::DragFloat("##Value", &Components[ComponentIndex], Speed);
        State.IncludeLastItem(bChanged);
        ImGui::PopID();
    }
    ImGui::PopID();
    return State;
}

FEditorControlState DrawVector3Control(
    const char* Label,
    FVector3& Value,
    float Speed = 0.1f)
{
    float Components[] = { Value.X, Value.Y, Value.Z };
    const FEditorControlState State =
        DrawFloat3Control(Label, Components, Speed);
    if (State.bChanged)
    {
        Value = FVector3(Components[0], Components[1], Components[2]);
    }
    return State;
}

FEditorControlState DrawRotatorControl(const char* Label, FRotator& Value)
{
    float Components[] = { Value.Pitch, Value.Yaw, Value.Roll };
    const FEditorControlState State =
        DrawFloat3Control(Label, Components, 0.25f);
    if (State.bChanged)
    {
        Value = FRotator(Components[0], Components[1], Components[2]).GetNormalized();
    }
    return State;
}

FEditorControlState DrawTransformControl(const char* Id, FTransform& Value)
{
    FRotator Rotation = Value.Rotation.Rotator();
    FEditorControlState State;

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
        State.Include(DrawVector3Control("##Location", Value.Translation));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        State.Include(DrawRotatorControl("##Rotation", Rotation));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Scale");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        State.Include(DrawVector3Control("##Scale", Value.Scale, 0.01f));
        ImGui::EndTable();
    }
    ImGui::PopID();

    if (State.bChanged)
    {
        Value.Rotation = Rotation.Quaternion();
    }
    return State;
}

}

void FDetailsPanel::Draw(
    FEditorSelection& InSelection,
    FPrepareEdit InPrepareEdit,
    FCompleteEdit InCompleteEdit,
    FSetStatus InSetStatus,
    FAddRoot InAddRoot,
    const FAssetRegistry& InAssetRegistry,
    FBrowseAsset InBrowseAsset,
    const FAssetPath& InSelectedAsset,
    FSetAssetReference InSetAssetReference)
{
    Selection = &InSelection;
    PrepareEdit = std::move(InPrepareEdit);
    CompleteEdit = std::move(InCompleteEdit);
    StatusSink = std::move(InSetStatus);
    AddRoot = std::move(InAddRoot);
    AssetRegistry = &InAssetRegistry;
    BrowseAsset = std::move(InBrowseAsset);
    SelectedAsset = InSelectedAsset;
    SetAssetReference = std::move(InSetAssetReference);

    PObject* Object = Selection->Resolve();
    if (Object == nullptr)
    {
        ImGui::TextDisabled("No object selected");
        return;
    }

    if (Selection->Num() > 1)
    {
        ImGui::Text("%zu objects selected", Selection->Num());
        ImGui::Separator();
        ImGui::TextDisabled("Primary selection");
        DrawObjectIdentity(Object);
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


void FDetailsPanel::DrawObjectIdentity(PObject* Object)
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


void FDetailsPanel::DrawActorDetails(PActor* Actor)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Transform");

    PSceneComponent* RootComponent = Actor->GetRootComponent();
    if (RootComponent == nullptr)
    {
        ImGui::TextDisabled("No RootComponent");
        if (ImGui::Button("Add Scene Root"))
        {
            AddRoot();
        }
    }
    else
    {
        FTransform Transform = Actor->GetActorTransform();
        const FEditorControlState State =
            DrawTransformControl("ActorTransform", Transform);
        const std::string EditKey = Actor->GetPathName() + ".ActorTransform";
        const bool bCanApply = PrepareInteractiveEdit(
            EditKey,
            "Edit " + Actor->GetPathName() + " transform",
            State.bActivated,
            State.bChanged);
        bool bChangeApplied = !State.bChanged;
        if (State.bChanged && bCanApply)
        {
            bChangeApplied = Actor->SetActorTransform(Transform);
            if (bChangeApplied)
            {
                SetStatus("Changed " + Actor->GetPathName() + " transform");
            }
        }
        CompleteInteractiveEdit(
            EditKey,
            State.bChanged,
            State.bActive,
            bChangeApplied);
        ImGui::TextDisabled("Provided by %s", RootComponent->GetName().ToString().c_str());
    }

    ImGui::Separator();
    ImGui::Text("Components: %zu", Actor->GetComponents().size());
    ImGui::Text("Begun Play: %s", Actor->HasBegunPlay() ? "true" : "false");
}


void FDetailsPanel::DrawSceneComponentDetails(PSceneComponent* Component)
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


void FDetailsPanel::DrawReflectedProperties(PObject* Object)
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
            if (Property != nullptr
                && Property->HasAnyFlags(
                    EPropertyFlags::Editable | EPropertyFlags::ReadOnly))
            {
                DrawPropertyEditor(Object, Property);
            }
        }
        ImGui::EndTable();
    }
}


void FDetailsPanel::DrawPropertyEditor(PObject* Object, const PProperty* Property)
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
    bool bChangeApplied = true;
    const std::string EditKey =
        Object->GetPathName() + "." + PropertyName;
    const std::string Description =
        "Edit " + Object->GetPathName() + "." + PropertyName;
    const bool bReadOnly = Property->HasAnyFlags(EPropertyFlags::ReadOnly);
    ImGui::BeginDisabled(bReadOnly);
    const auto ApplyValue =
        [this, &EditKey, &Description, &bChanged, &bChangeApplied](
            const FEditorControlState& State,
            auto&& Setter)
        {
            bChanged = State.bChanged;
            const bool bCanApply = PrepareInteractiveEdit(
                EditKey,
                Description,
                State.bActivated,
                State.bChanged);
            if (State.bChanged)
            {
                bChangeApplied = bCanApply && Setter();
            }
            CompleteInteractiveEdit(
                EditKey,
                State.bChanged,
                State.bActive,
                bChangeApplied);
        };

    switch (Property->GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        if (Property->GetValue(Object, Value))
        {
            FEditorControlState State;
            State.IncludeLastItem(ImGui::InputInt("##Value", &Value));
            ApplyValue(
                State,
                [Object, Property, Value]()
                {
                    return Property->SetValue(
                        Object, Value, EPropertyChangeType::Interactive);
                });
        }
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (Property->GetValue(Object, Value))
        {
            FEditorControlState State;
            State.IncludeLastItem(ImGui::DragFloat("##Value", &Value, 0.1f));
            ApplyValue(
                State,
                [Object, Property, Value]()
                {
                    return Property->SetValue(
                        Object, Value, EPropertyChangeType::Interactive);
                });
        }
        break;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        if (Property->GetValue(Object, Value))
        {
            FEditorControlState State;
            State.IncludeLastItem(ImGui::Checkbox("##Value", &Value));
            ApplyValue(
                State,
                [Object, Property, Value]()
                {
                    return Property->SetValue(Object, Value);
                });
        }
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (Property->GetValue(Object, Value))
        {
            const FEditorControlState State =
                DrawVector3Control("##Value", Value);
            ApplyValue(
                State,
                [Object, Property, Value]()
                {
                    return Property->SetValue(
                        Object, Value, EPropertyChangeType::Interactive);
                });
        }
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        if (Property->GetValue(Object, Value))
        {
            const FEditorControlState State =
                DrawRotatorControl("##Value", Value);
            ApplyValue(
                State,
                [Object, Property, Value]()
                {
                    return Property->SetValue(
                        Object, Value, EPropertyChangeType::Interactive);
                });
        }
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        if (Property->GetValue(Object, Value))
        {
            const FEditorControlState State =
                DrawTransformControl("PropertyTransform", Value);
            ApplyValue(
                State,
                [Object, Property, Value]()
                {
                    return Property->SetValue(
                        Object, Value, EPropertyChangeType::Interactive);
                });
        }
        break;
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        if (Property->GetValue(Object, Value))
        {
            const EAssetReferenceType ReferenceType =
                Property->GetAssetReferenceType();
            if (ReferenceType == EAssetReferenceType::None
                || AssetRegistry == nullptr)
            {
                ImGui::TextUnformatted(
                    Value.IsValid() ? Value.ToString().data() : "<None>");
                break;
            }

            const FAssetReferenceEditResult Result = AssetReferenceWidget.Draw(
                PropertyName.c_str(),
                Value,
                ReferenceType,
                *AssetRegistry,
                SelectedAsset);
            if (Result.bRejectedDrop)
            {
                SetStatus("Dropped asset type is not valid for " + PropertyName, true);
            }
            if (Result.bBrowseRequested && BrowseAsset)
            {
                BrowseAsset(Value);
            }
            if (Result.bChanged)
            {
                if (SetAssetReference)
                {
                    SetAssetReference(
                        Object->GetHandle(),
                        Property->GetName(),
                        Result.Value);
                }
            }
        }
        break;
    }
    case EPropertyType::Object:
    {
        PObject* Referenced = Property->GetReferencedObject(Object);
        ImGui::TextUnformatted(
            Referenced != nullptr ? Referenced->GetPathName().c_str() : "None");
        break;
    }
    case EPropertyType::DynamicMulticastDelegate:
    {
        const FDynamicMulticastDelegate* Delegate =
            Property->GetDynamicMulticastDelegate(Object);
        ImGui::Text(
            "%zu binding(s)",
            Delegate != nullptr ? Delegate->Num() : std::size_t {0});
        break;
    }
    }

    ImGui::EndDisabled();

    if (bChanged && bChangeApplied)
    {
        SetStatus("Changed " + Object->GetPathName() + "." + PropertyName);
    }
    ImGui::PopID();
}


bool FDetailsPanel::PrepareInteractiveEdit(
    const std::string& Key,
    std::string Description,
    bool bActivated,
    bool bChanged)
{
    return PrepareEdit(Key, std::move(Description), bActivated, bChanged);
}

void FDetailsPanel::CompleteInteractiveEdit(
    const std::string& Key,
    bool bChanged,
    bool bActive,
    bool bApplied)
{
    CompleteEdit(Key, bChanged, bActive, bApplied);
}

void FDetailsPanel::SetStatus(std::string Message, bool bError)
{
    StatusSink(std::move(Message), bError);
}
}
