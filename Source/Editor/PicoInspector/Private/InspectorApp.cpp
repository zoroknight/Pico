#include "InspectorApp.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ObjectSerialization.h"
#include "Pico/Object/Property.h"
#include "Pico/Samples/DemoCharacter.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
void CopyText(std::span<char> Destination, const std::string& Source)
{
    if (Destination.empty())
    {
        return;
    }

    const std::size_t CopyLength = std::min(Source.size(), Destination.size() - 1);
    std::memcpy(Destination.data(), Source.data(), CopyLength);
    Destination[CopyLength] = '\0';
}

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

bool DrawTransformControl(FTransform& Value)
{
    FRotator Rotation = Value.Rotation.Rotator();
    bool bChanged = false;
    if (ImGui::BeginTable(
            "##TransformComponents",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 62.0f);
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

    if (bChanged)
    {
        Value.Rotation = Rotation.Quaternion();
    }
    return bChanged;
}
}

FInspectorApp::FInspectorApp()
    : SelectedClass(PDemoCharacter::StaticClass())
{
    CopyText(NewObjectName, "Player");
    CopyText(
        SavePath,
        (std::filesystem::current_path() / "Saved" / "Inspector" / "SelectedObject.pobj").string());

    if (PObject* Player = NewObject(SelectedClass, nullptr, FName(NewObjectName.data())))
    {
        SelectedObjectHandle = Player->GetHandle();
        SetStatus("Created Player");
    }
}

void FInspectorApp::Draw()
{
    const ImGuiViewport* Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);
    ImGui::Begin(
        "Pico Inspector",
        nullptr,
        ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings);

    DrawToolbar();
    ImGui::Separator();

    if (ImGui::BeginTable(
            "InspectorColumns",
            3,
            ImGuiTableFlags_Resizable
                | ImGuiTableFlags_BordersInnerV
                | ImGuiTableFlags_NoSavedSettings
                | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Classes", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Objects", ImGuiTableColumnFlags_WidthFixed, 210.0f);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawClassPanel();
        ImGui::TableSetColumnIndex(1);
        DrawObjectPanel();
        ImGui::TableSetColumnIndex(2);
        DrawDetailsPanel();
        ImGui::EndTable();
    }

    ImGui::Separator();
    const ImVec4 StatusColor = bStatusIsError
        ? ImVec4(0.95f, 0.42f, 0.35f, 1.0f)
        : ImVec4(0.35f, 0.78f, 0.66f, 1.0f);
    ImGui::TextColored(StatusColor, "%s", Status.empty() ? "Ready" : Status.c_str());
    ImGui::End();
}

void FInspectorApp::DrawToolbar()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(56.0f);
    ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 150.0f));
    ImGui::InputText("##ObjectName", NewObjectName.data(), NewObjectName.size());
    ImGui::SameLine();
    if (ImGui::Button("Create", ImVec2(66.0f, 0.0f)))
    {
        CreateSelectedClass();
    }
    ImGui::SameLine();
    if (ImGui::Button("Destroy", ImVec2(76.0f, 0.0f)))
    {
        DestroySelectedObject();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("File");
    ImGui::SameLine(56.0f);
    ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 150.0f));
    ImGui::InputText("##SavePath", SavePath.data(), SavePath.size());
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(66.0f, 0.0f)))
    {
        SaveSelectedObject();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(76.0f, 0.0f)))
    {
        LoadObjectFromDisk();
    }
}

void FInspectorApp::DrawClassPanel()
{
    for (const PClass* Class : FClassRegistry::GetClasses())
    {
        const std::string ClassName = Class->GetName().ToString();
        if (ImGui::Selectable(ClassName.c_str(), SelectedClass == Class))
        {
            SelectedClass = Class;
            SelectedObjectHandle = {};
        }
    }
}

void FInspectorApp::DrawObjectPanel()
{
    for (PObject* Object : FObjectRegistry::GetObjects())
    {
        ImGui::PushID(static_cast<int>(Object->GetHandle().Index));
        const std::string Label =
            Object->GetPathName() + "\n" + Object->GetClass()->GetName().ToString();
        if (ImGui::Selectable(Label.c_str(), SelectedObjectHandle == Object->GetHandle()))
        {
            SelectedObjectHandle = Object->GetHandle();
            SelectedClass = Object->GetClass();
        }
        ImGui::PopID();
    }
}

void FInspectorApp::DrawDetailsPanel()
{
    if (PObject* Object = GetSelectedObject())
    {
        DrawObjectDetails(Object);
    }
    else
    {
        DrawClassDetails(SelectedClass);
    }
}

void FInspectorApp::DrawClassDetails(const PClass* Class)
{
    if (Class == nullptr)
    {
        ImGui::TextUnformatted("No class selected");
        return;
    }

    ImGui::Text("Class: %s", Class->GetName().ToString().c_str());
    ImGui::Text(
        "Super: %s",
        Class->GetSuperClass() != nullptr
            ? Class->GetSuperClass()->GetName().ToString().c_str()
            : "None");
    ImGui::Text("Native size: %zu", Class->GetSize());
    ImGui::Text("Constructible: %s", Class->CanConstruct() ? "true" : "false");
    ImGui::Separator();
    ImGui::TextUnformatted("Reflected properties");

    for (const PProperty* Property : GetAllProperties(Class))
    {
        ImGui::Text(
            "%s  %s",
            Property->GetName().ToString().c_str(),
            GetPropertyTypeName(Property->GetType()).data());
        ImGui::TextDisabled("Declared by %s", Property->GetOwnerClass()->GetName().ToString().c_str());
    }
}

void FInspectorApp::DrawObjectDetails(PObject* Object)
{
    const FObjectHandle Handle = Object->GetHandle();
    ImGui::Text("Object: %s", Object->GetPathName().c_str());
    ImGui::Text("Class: %s", Object->GetClass()->GetName().ToString().c_str());
    ImGui::Text("Outer: %s", Object->GetOuter() != nullptr ? Object->GetOuter()->GetPathName().c_str() : "None");
    ImGui::Text("Handle: {%u, %u}", Handle.Index, Handle.Serial);
    ImGui::Separator();
    ImGui::TextUnformatted("Properties");

    if (ImGui::BeginTable(
            "ObjectProperties",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        for (const PProperty* Property : GetAllProperties(Object->GetClass()))
        {
            DrawPropertyEditor(Object, Property);
        }
        ImGui::EndTable();
    }
}

void FInspectorApp::DrawPropertyEditor(PObject* Object, const PProperty* Property)
{
    const std::string PropertyName = Property->GetName().ToString();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(PropertyName.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushID(Property);

    switch (Property->GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        if (Property->GetValue(Object, Value)
            && ImGui::InputInt("##Value", &Value))
        {
            Property->SetValue(Object, Value);
            SetStatus("Changed " + PropertyName);
        }
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (Property->GetValue(Object, Value)
            && ImGui::DragFloat("##Value", &Value, 1.0f))
        {
            Property->SetValue(Object, Value);
            SetStatus("Changed " + PropertyName);
        }
        break;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        if (Property->GetValue(Object, Value)
            && ImGui::Checkbox("##Value", &Value))
        {
            Property->SetValue(Object, Value);
            SetStatus("Changed " + PropertyName);
        }
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (Property->GetValue(Object, Value)
            && DrawVector3Control("##Value", Value))
        {
            Property->SetValue(Object, Value);
            SetStatus("Changed " + PropertyName);
        }
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        if (Property->GetValue(Object, Value)
            && DrawRotatorControl("##Value", Value))
        {
            Property->SetValue(Object, Value);
            SetStatus("Changed " + PropertyName);
        }
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        if (Property->GetValue(Object, Value)
            && DrawTransformControl(Value))
        {
            Property->SetValue(Object, Value);
            SetStatus("Changed " + PropertyName);
        }
        break;
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        if (Property->GetValue(Object, Value))
        {
            ImGui::TextUnformatted(std::string(Value.ToString()).c_str());
        }
        break;
    }
    }

    ImGui::PopID();
}

PObject* FInspectorApp::GetSelectedObject() const
{
    return ResolveObject(SelectedObjectHandle);
}

void FInspectorApp::CreateSelectedClass()
{
    if (SelectedClass == nullptr || !SelectedClass->CanConstruct() || NewObjectName[0] == '\0')
    {
        SetStatus("Select a constructible class and enter a name", true);
        return;
    }

    PObject* Object = NewObject(SelectedClass, nullptr, FName(NewObjectName.data()));
    if (Object == nullptr)
    {
        SetStatus("Object creation failed; the name may already exist", true);
        return;
    }

    SelectedObjectHandle = Object->GetHandle();
    SetStatus("Created " + Object->GetPathName());
}

void FInspectorApp::DestroySelectedObject()
{
    PObject* Object = GetSelectedObject();
    if (Object == nullptr)
    {
        SetStatus("Select an object to destroy", true);
        return;
    }

    const std::string PathName = Object->GetPathName();
    if (!DestroyObject(Object))
    {
        SetStatus("Destroy failed; remove child objects first", true);
        return;
    }

    SelectedObjectHandle = {};
    SetStatus("Destroyed " + PathName);
}

void FInspectorApp::SaveSelectedObject()
{
    PObject* Object = GetSelectedObject();
    if (Object == nullptr || SavePath[0] == '\0')
    {
        SetStatus("Select an object and enter a save path", true);
        return;
    }

    const std::filesystem::path FilePath(SavePath.data());
    std::error_code FileError;
    if (!FilePath.parent_path().empty())
    {
        std::filesystem::create_directories(FilePath.parent_path(), FileError);
    }

    EObjectSerializationError SerializationError = EObjectSerializationError::None;
    if (FileError || !SaveObjectToFile(FilePath, Object, &SerializationError))
    {
        SetStatus("Save failed: " + std::string(ToString(SerializationError)), true);
        return;
    }

    SetStatus("Saved " + Object->GetPathName());
}

void FInspectorApp::LoadObjectFromDisk()
{
    if (SavePath[0] == '\0')
    {
        SetStatus("Enter a save path", true);
        return;
    }

    EObjectSerializationError SerializationError = EObjectSerializationError::None;
    PObject* Object = LoadObjectFromFile(SavePath.data(), nullptr, &SerializationError);
    if (Object == nullptr)
    {
        SetStatus("Load failed: " + std::string(ToString(SerializationError)), true);
        return;
    }

    SelectedObjectHandle = Object->GetHandle();
    SelectedClass = Object->GetClass();
    SetStatus("Loaded " + Object->GetPathName());
}

void FInspectorApp::SetStatus(std::string Message, bool bIsError)
{
    Status = std::move(Message);
    bStatusIsError = bIsError;
}
}
