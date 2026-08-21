#include "DetailsPanel.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/Property.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
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

std::string MakePropertyDisplayName(const PProperty& Property)
{
    if (!Property.GetMetadata().DisplayName.empty())
    {
        return Property.GetMetadata().DisplayName;
    }

    std::string Source = Property.GetName().ToString();
    if (Property.GetType() == EPropertyType::Bool
        && Source.size() > 1
        && Source[0] == 'b'
        && std::isupper(static_cast<unsigned char>(Source[1])) != 0)
    {
        Source.erase(Source.begin());
    }
    if (Property.GetType() == EPropertyType::Int32
        && Source == "MovementReferenceValue")
    {
        Source = "MovementReference";
    }

    std::string Result;
    Result.reserve(Source.size() + 8);
    for (std::size_t Index = 0; Index < Source.size(); ++Index)
    {
        const unsigned char Current = static_cast<unsigned char>(Source[Index]);
        const unsigned char Previous = Index > 0
            ? static_cast<unsigned char>(Source[Index - 1]) : 0;
        const unsigned char Next = Index + 1 < Source.size()
            ? static_cast<unsigned char>(Source[Index + 1]) : 0;
        const bool bWordBoundary = Index > 0
            && ((std::isupper(Current) != 0
                    && (std::islower(Previous) != 0
                        || (Next != 0 && std::islower(Next) != 0)))
                || (std::isdigit(Current) != 0 && std::isdigit(Previous) == 0)
                || (std::isdigit(Current) == 0 && std::isdigit(Previous) != 0));
        if (bWordBoundary && !Result.empty() && Result.back() != ' ')
        {
            Result.push_back(' ');
        }
        Result.push_back(static_cast<char>(Current));
    }
    return Result;
}

const std::vector<FPropertyMetadata::FEnumOption>& GetEnumOptions(
    const PProperty& Property)
{
    if (!Property.GetMetadata().EnumOptions.empty())
    {
        return Property.GetMetadata().EnumOptions;
    }
    static const std::vector<FPropertyMetadata::FEnumOption>
        MovementReferenceOptions {
            {0, "Control Rotation (Third Person)"},
            {1, "Actor Rotation (Character Relative)"},
            {2, "World Axes"}
        };
    static const std::vector<FPropertyMetadata::FEnumOption> EmptyOptions;
    return Property.GetName() == FName("MovementReferenceValue")
        ? MovementReferenceOptions : EmptyOptions;
}

std::string GetEnumValueDisplayName(
    int32 Value,
    const std::vector<FPropertyMetadata::FEnumOption>& Options)
{
    const auto Found = std::find_if(
        Options.begin(), Options.end(),
        [Value](const FPropertyMetadata::FEnumOption& Option)
        {
            return Option.Value == Value;
        });
    return Found != Options.end()
        ? Found->DisplayName
        : "Unknown (" + std::to_string(Value) + ")";
}

PWorld* FindOwningWorld(PObject* Object)
{
    for (PObject* Current = Object; Current != nullptr; Current = Current->GetOuter())
    {
        if (Current->IsA(PWorld::StaticClass()))
        {
            return static_cast<PWorld*>(Current);
        }
    }
    return nullptr;
}

std::vector<PObject*> GatherWorldObjects(PObject* Owner)
{
    PWorld* World = FindOwningWorld(Owner);
    std::vector<PObject*> Result;
    if (World == nullptr)
    {
        return Result;
    }
    for (PObject* Candidate : FObjectRegistry::GetObjects())
    {
        if (Candidate != nullptr
            && !Candidate->IsBeginningDestroy()
            && FindOwningWorld(Candidate) == World)
        {
            Result.push_back(Candidate);
        }
    }
    std::sort(
        Result.begin(), Result.end(),
        [](const PObject* Left, const PObject* Right)
        {
            return Left->GetPathName() < Right->GetPathName();
        });
    return Result;
}

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
    else if (Object->IsA(PActorComponent::StaticClass()))
    {
        DrawActorComponentDetails(static_cast<PActorComponent*>(Object));
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
    DrawReflectedProperties(Actor);
}


void FDetailsPanel::DrawActorComponentDetails(PActorComponent* Component)
{
    PActor* Owner = Component->GetOwner();
    ImGui::Separator();
    ImGui::Text(
        "Owner: %s",
        Owner != nullptr ? Owner->GetPathName().c_str() : "None");
    ImGui::Text("Registered: %s", Component->IsRegistered() ? "true" : "false");
    DrawReflectedProperties(Component);
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
                && Property->GetType() != EPropertyType::DynamicMulticastDelegate
                && Property->HasAnyFlags(
                    EPropertyFlags::Editable | EPropertyFlags::ReadOnly))
            {
                DrawPropertyEditor(Object, Property);
            }
        }
        ImGui::EndTable();
    }
    DrawEventBindings(Object, Properties);
}

void FDetailsPanel::DrawEventBindings(
    PObject* Object,
    const std::vector<const PProperty*>& Properties)
{
    bool bHasEvents = false;
    for (const PProperty* Property : Properties)
    {
        bHasEvents |= Property != nullptr
            && Property->GetType() == EPropertyType::DynamicMulticastDelegate
            && Property->HasAnyFlags(EPropertyFlags::Editable | EPropertyFlags::ReadOnly);
    }
    if (!bHasEvents)
    {
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Events & Bindings");
    for (const PProperty* Property : Properties)
    {
        if (Property == nullptr
            || Property->GetType() != EPropertyType::DynamicMulticastDelegate
            || !Property->HasAnyFlags(EPropertyFlags::Editable | EPropertyFlags::ReadOnly))
        {
            continue;
        }

        ImGui::PushID(Property);
        FDynamicMulticastDelegate* Delegate =
            Property->GetDynamicMulticastDelegate(Object);
        const std::vector<FDynamicDelegateBindingView> Bindings =
            Delegate != nullptr ? Delegate->GetBindings()
                                : std::vector<FDynamicDelegateBindingView> {};
        const std::string Header = Property->GetName().ToString()
            + " (" + std::to_string(Bindings.size()) + ")";
        if (ImGui::CollapsingHeader(Header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::size_t BindingIndex = 0;
            for (const FDynamicDelegateBindingView& Binding : Bindings)
            {
                ImGui::PushID(static_cast<int>(BindingIndex++));
                PObject* Target = Binding.bTargetAlive
                    ? ResolveObject(Binding.TargetHandle) : nullptr;
                if (Target == nullptr)
                {
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.42f, 0.35f, 1.0f),
                        "Invalid target -> %s",
                        Binding.FunctionName.ToString().c_str());
                }
                else
                {
                    ImGui::Text(
                        "%s -> %s",
                        Target->GetPathName().c_str(),
                        Binding.FunctionName.ToString().c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                {
                    const std::string EditKey = Object->GetPathName()
                        + "." + Property->GetName().ToString() + ".Bindings";
                    const bool bCanApply = PrepareInteractiveEdit(
                        EditKey,
                        "Remove event binding from " + Object->GetPathName(),
                        true,
                        true);
                    bool bRemoved = false;
                    if (bCanApply && Delegate != nullptr
                        && Property->NotifyPreChange(Object))
                    {
                        bRemoved = Delegate->Remove(Binding.Handle);
                        Property->NotifyPostChange(Object);
                    }
                    CompleteInteractiveEdit(EditKey, true, false, bRemoved);
                    SetStatus(
                        bRemoved ? "Removed event binding"
                                 : "Could not remove event binding",
                        !bRemoved);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Remove binding");
                }
                ImGui::PopID();
            }

            const bool bReadOnly = Property->HasAnyFlags(EPropertyFlags::ReadOnly);
            ImGui::BeginDisabled(bReadOnly || Delegate == nullptr);
            if (ImGui::Button("Add Binding..."))
            {
                BindingOwnerHandle = Object->GetHandle();
                BindingPropertyName = Property->GetName();
                BindingTargetHandle = Object->GetHandle();
                BindingFunctionName = {};
                ImGui::OpenPopup("Add Event Binding");
            }
            ImGui::EndDisabled();

            if (ImGui::BeginPopupModal(
                    "Add Event Binding", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                PObject* BindingOwner = ResolveObject(BindingOwnerHandle);
                const PProperty* BindingProperty = BindingOwner != nullptr
                    ? BindingOwner->GetClass()->FindProperty(BindingPropertyName)
                    : nullptr;
                FDynamicMulticastDelegate* BindingDelegate = BindingProperty != nullptr
                    ? BindingProperty->GetDynamicMulticastDelegate(BindingOwner)
                    : nullptr;
                PObject* Target = ResolveObject(BindingTargetHandle);
                const std::string TargetPreview = Target != nullptr
                    ? Target->GetPathName() : "Select target";
                if (ImGui::BeginCombo("Target Object", TargetPreview.c_str()))
                {
                    for (PObject* Candidate : GatherWorldObjects(BindingOwner))
                    {
                        const bool bSelected = Candidate == Target;
                        if (ImGui::Selectable(
                                Candidate->GetPathName().c_str(), bSelected))
                        {
                            BindingTargetHandle = Candidate->GetHandle();
                            BindingFunctionName = {};
                        }
                    }
                    ImGui::EndCombo();
                }

                Target = ResolveObject(BindingTargetHandle);
                const std::string FunctionPreview = BindingFunctionName.IsNone()
                    ? "Select compatible function"
                    : BindingFunctionName.ToString();
                if (ImGui::BeginCombo("Target Function", FunctionPreview.c_str()))
                {
                    if (Target != nullptr && BindingDelegate != nullptr)
                    {
                        for (const PFunction* Function : GetAllFunctions(Target->GetClass()))
                        {
                            if (Function != nullptr
                                && Function->HasAnyFlags(EFunctionFlags::Callable)
                                && BindingDelegate->IsFunctionCompatible(*Function))
                            {
                                const bool bSelected =
                                    BindingFunctionName == Function->GetName();
                                if (ImGui::Selectable(
                                        Function->GetName().ToString().c_str(),
                                        bSelected))
                                {
                                    BindingFunctionName = Function->GetName();
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                const bool bCanAdd = BindingOwner != nullptr
                    && BindingProperty != nullptr
                    && BindingDelegate != nullptr
                    && Target != nullptr
                    && !BindingFunctionName.IsNone();
                ImGui::BeginDisabled(!bCanAdd);
                if (ImGui::Button("Add", ImVec2(100.0f, 0.0f)))
                {
                    const std::string EditKey = BindingOwner->GetPathName()
                        + "." + BindingPropertyName.ToString() + ".Bindings";
                    const bool bCanApply = PrepareInteractiveEdit(
                        EditKey,
                        "Add event binding to " + BindingOwner->GetPathName(),
                        true,
                        true);
                    bool bAdded = false;
                    if (bCanApply && BindingProperty->NotifyPreChange(BindingOwner))
                    {
                        bAdded = BindingDelegate->AddUniqueDynamic(
                            Target, BindingFunctionName).IsSuccess();
                        BindingProperty->NotifyPostChange(BindingOwner);
                    }
                    CompleteInteractiveEdit(EditKey, true, false, bAdded);
                    SetStatus(
                        bAdded ? "Added event binding"
                               : "Could not add event binding (possibly already bound)",
                        !bAdded);
                    if (bAdded)
                    {
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
}


void FDetailsPanel::DrawPropertyEditor(PObject* Object, const PProperty* Property)
{
    const std::string PropertyName = Property->GetName().ToString();
    const std::string DisplayName = MakePropertyDisplayName(*Property);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(DisplayName.c_str());
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
            const std::vector<FPropertyMetadata::FEnumOption>& Options =
                GetEnumOptions(*Property);
            if (!Options.empty())
            {
                const std::string Preview =
                    GetEnumValueDisplayName(Value, Options);
                if (ImGui::BeginCombo("##Value", Preview.c_str()))
                {
                    for (const FPropertyMetadata::FEnumOption& Option : Options)
                    {
                        const bool bSelected = Option.Value == Value;
                        if (ImGui::Selectable(
                                Option.DisplayName.c_str(), bSelected))
                        {
                            Value = Option.Value;
                            State.bChanged = true;
                            State.bActivated = true;
                        }
                        if (bSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                State.IncludeLastItem(ImGui::InputInt("##Value", &Value));
            }
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
            if (Object->IsA(PSkeletalMeshComponent::StaticClass())
                && PropertyName.starts_with("MaterialOverride"))
            {
                const auto* SkeletalMesh = static_cast<const PSkeletalMeshComponent*>(Object);
                const std::size_t SlotCount = SkeletalMesh->GetMaterialSlotCount();
                const std::string IndexText = PropertyName.substr(
                    std::string("MaterialOverride").size());
                const std::size_t SlotIndex = IndexText.empty()
                    ? 0 : static_cast<std::size_t>(std::stoul(IndexText));
                if (SlotCount > 0 && SlotIndex >= SlotCount)
                {
                    ImGui::TextDisabled("Unused (mesh has %zu slots)", SlotCount);
                    break;
                }
            }
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
