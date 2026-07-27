#include "SandboxEditorApp.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Property.h"
#include "PicoSandbox/SandboxCharacter.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace PicoSandbox
{
namespace
{
ImVec4 GetStepColor(ESandboxStepState State)
{
    switch (State)
    {
    case ESandboxStepState::Succeeded:
        return ImVec4(0.35f, 0.78f, 0.66f, 1.0f);
    case ESandboxStepState::Failed:
        return ImVec4(0.95f, 0.42f, 0.35f, 1.0f);
    case ESandboxStepState::NotStarted:
        return ImVec4(0.56f, 0.59f, 0.61f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

const char* GetStepMarker(ESandboxStepState State)
{
    switch (State)
    {
    case ESandboxStepState::Succeeded:
        return "[OK]";
    case ESandboxStepState::Failed:
        return "[!!]";
    case ESandboxStepState::NotStarted:
        return "[  ]";
    }
    return "[  ]";
}

bool DrawVector3Control(const char* Label, Pico::FVector3& Value, float Speed = 0.1f)
{
    float Components[] = { Value.X, Value.Y, Value.Z };
    if (!ImGui::DragFloat3(Label, Components, Speed))
    {
        return false;
    }

    Value = Pico::FVector3(Components[0], Components[1], Components[2]);
    return true;
}

bool DrawRotatorControl(const char* Label, Pico::FRotator& Value)
{
    float Components[] = { Value.Pitch, Value.Yaw, Value.Roll };
    if (!ImGui::DragFloat3(Label, Components, 0.25f))
    {
        return false;
    }

    Value = Pico::FRotator(Components[0], Components[1], Components[2]).GetNormalized();
    return true;
}

bool DrawTransformControl(Pico::FTransform& Value)
{
    Pico::FRotator Rotation = Value.Rotation.Rotator();
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

FSandboxEditorApp::FSandboxEditorApp()
{
    if (Session.Initialize() && Session.Create())
    {
        AppendEvent("Ready with a new SandboxHero");
    }
    else
    {
        AppendEvent(Session.GetLastMessage());
    }
}

void FSandboxEditorApp::Draw()
{
    const ImGuiViewport* Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);
    ImGui::Begin(
        "Pico Sandbox",
        nullptr,
        ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings);

    DrawToolbar();
    ImGui::Separator();

    const float LogHeight = 150.0f;
    if (ImGui::BeginTable(
            "SandboxPanels",
            3,
            ImGuiTableFlags_Resizable
                | ImGuiTableFlags_BordersInnerV
                | ImGuiTableFlags_NoSavedSettings
                | ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, -LogHeight)))
    {
        ImGui::TableSetupColumn("Workflow", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Live Object", ImGuiTableColumnFlags_WidthFixed, 290.0f);
        ImGui::TableSetupColumn("Reflected Details", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawWorkflowPanel();
        ImGui::TableSetColumnIndex(1);
        DrawObjectPanel();
        ImGui::TableSetColumnIndex(2);
        DrawDetailsPanel();
        ImGui::EndTable();
    }

    ImGui::Separator();
    DrawEventLog();
    ImGui::End();
}

void FSandboxEditorApp::DrawToolbar()
{
    if (ImGui::Button("New Session"))
    {
        StartNewSession();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Changes"))
    {
        RunAction("Apply Changes", &FSandboxSession::ApplyDemoChanges);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        RunAction("Save", &FSandboxSession::Save);
    }
    ImGui::SameLine();
    if (ImGui::Button("Destroy"))
    {
        RunAction("Destroy", &FSandboxSession::Destroy);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        RunAction("Load", &FSandboxSession::Load);
    }
    ImGui::SameLine();
    if (ImGui::Button("Verify"))
    {
        RunAction("Verify", &FSandboxSession::VerifyLoaded);
    }
    ImGui::SameLine();
    if (ImGui::Button("Run Full Flow"))
    {
        RunAction("Run Full Flow", &FSandboxSession::RunFullWorkflow);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Example Asset"))
    {
        RunAction("Reset Example Asset", &FSandboxSession::RunFullWorkflow);
    }
}

void FSandboxEditorApp::DrawWorkflowPanel()
{
    for (std::size_t Index = 0; Index < static_cast<std::size_t>(ESandboxStep::Count); ++Index)
    {
        const ESandboxStep Step = static_cast<ESandboxStep>(Index);
        const ESandboxStepState State = Session.GetStepState(Step);
        ImGui::TextColored(
            GetStepColor(State),
            "%s  %s",
            GetStepMarker(State),
            FSandboxSession::GetStepName(Step).data());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Expected loaded state");
    ImGui::TextDisabled("EntityId    2002");
    ImGui::TextDisabled("Health      75");
    ImGui::TextDisabled("MoveSpeed   720");
    ImGui::TextDisabled("bAlive      false");
    ImGui::TextDisabled("Velocity    100, 0, 25");
    ImGui::TextDisabled("ViewRot     5, 90, 0");
    ImGui::TextDisabled("Location    120, 30, 10");
    ImGui::TextDisabled("Rotation    10, 45, 0");
    ImGui::TextDisabled("Scale       1.5, 1, 1");
}

void FSandboxEditorApp::DrawObjectPanel()
{
    Pico::PObject* Object = Session.GetObject();
    if (Object != nullptr)
    {
        const Pico::FObjectHandle Handle = Object->GetHandle();
        ImGui::Text("Name: %s", Object->GetName().ToString().c_str());
        ImGui::Text("Class: %s", Object->GetClass()->GetName().ToString().c_str());
        ImGui::Text("Handle: {%u, %u}", Handle.Index, Handle.Serial);
        ImGui::TextColored(
            ImVec4(0.35f, 0.78f, 0.66f, 1.0f),
            "State: Live in memory");
    }
    else
    {
        ImGui::TextDisabled("No live object");
        ImGui::TextDisabled("Create or load SandboxHero");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Object file");
    std::error_code RelativePathError;
    const std::filesystem::path RelativePath = std::filesystem::relative(
        Session.GetObjectPath(),
        FSandboxSession::GetSandboxRootDirectory(),
        RelativePathError);
    const std::string DisplayPath =
        RelativePathError ? Session.GetObjectPath().string() : RelativePath.string();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", DisplayPath.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", Session.GetObjectPath().string().c_str());
    }

    std::error_code FileError;
    const bool bFileExists = std::filesystem::is_regular_file(Session.GetObjectPath(), FileError);
    if (bFileExists)
    {
        const std::uintmax_t FileSize = std::filesystem::file_size(Session.GetObjectPath(), FileError);
        ImGui::TextColored(
            ImVec4(0.35f, 0.78f, 0.66f, 1.0f),
            "On disk: %llu bytes",
            static_cast<unsigned long long>(FileSize));
    }
    else
    {
        ImGui::TextDisabled("On disk: not created");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Last operation");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", Session.GetLastMessage().empty() ? "Ready" : Session.GetLastMessage().c_str());
    ImGui::PopTextWrapPos();

    if (Session.GetLastSerializationError() != Pico::EObjectSerializationError::None)
    {
        ImGui::TextColored(
            ImVec4(0.95f, 0.42f, 0.35f, 1.0f),
            "Serialization: %s",
            Pico::ToString(Session.GetLastSerializationError()).data());
    }
}

void FSandboxEditorApp::DrawDetailsPanel()
{
    Pico::PObject* Object = Session.GetObject();
    if (Object == nullptr)
    {
        ImGui::TextDisabled("No object selected");
        return;
    }

    if (ImGui::BeginTable(
            "SandboxProperties",
            3,
            ImGuiTableFlags_SizingStretchProp
                | ImGuiTableFlags_BordersInnerH
                | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Declared By", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const Pico::PProperty* Property : Pico::GetAllProperties(Object->GetClass()))
        {
            DrawPropertyEditor(Object, Property);
        }
        ImGui::EndTable();
    }

    const FSandboxSnapshot Snapshot = Session.GetSnapshot();
    ImGui::Spacing();
    ImGui::TextDisabled(
        "PostLoad observed Health = %d",
        Snapshot.HealthSeenInPostLoad);
    ImGui::TextDisabled(
        "PostLoad observed Location = %.1f, %.1f, %.1f",
        Snapshot.TransformSeenInPostLoad.Translation.X,
        Snapshot.TransformSeenInPostLoad.Translation.Y,
        Snapshot.TransformSeenInPostLoad.Translation.Z);
}

void FSandboxEditorApp::DrawPropertyEditor(Pico::PObject* Object, const Pico::PProperty* Property)
{
    const std::string PropertyName = Property->GetName().ToString();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(PropertyName.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", Property->GetOwnerClass()->GetName().ToString().c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushID(Property);

    bool bChanged = false;
    switch (Property->GetType())
    {
    case Pico::EPropertyType::Int32:
    {
        Pico::int32 Value = 0;
        bChanged = Property->GetValue(Object, Value)
            && ImGui::InputInt("##Value", &Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case Pico::EPropertyType::Float:
    {
        float Value = 0.0f;
        bChanged = Property->GetValue(Object, Value)
            && ImGui::DragFloat("##Value", &Value, 1.0f, 0.0f, 5000.0f)
            && Property->SetValue(Object, Value);
        break;
    }
    case Pico::EPropertyType::Bool:
    {
        bool Value = false;
        bChanged = Property->GetValue(Object, Value)
            && ImGui::Checkbox("##Value", &Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case Pico::EPropertyType::Vector3:
    {
        Pico::FVector3 Value;
        bChanged = Property->GetValue(Object, Value)
            && DrawVector3Control("##Value", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case Pico::EPropertyType::Rotator:
    {
        Pico::FRotator Value;
        bChanged = Property->GetValue(Object, Value)
            && DrawRotatorControl("##Value", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case Pico::EPropertyType::Transform:
    {
        Pico::FTransform Value;
        bChanged = Property->GetValue(Object, Value)
            && DrawTransformControl(Value)
            && Property->SetValue(Object, Value);
        break;
    }
    }

    if (bChanged)
    {
        Session.MarkObjectModified("Changed " + PropertyName + " through PProperty");
        AppendEvent(Session.GetLastMessage());
    }

    ImGui::PopID();
}

void FSandboxEditorApp::DrawEventLog()
{
    ImGui::TextUnformatted("Event Log");
    ImGui::BeginChild("SandboxEventLog", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& Event : Events)
    {
        ImGui::TextUnformatted(Event.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void FSandboxEditorApp::RunAction(const char* ActionName, bool (FSandboxSession::*Action)())
{
    const bool bSucceeded = (Session.*Action)();
    AppendEvent(
        std::string(bSucceeded ? "[OK] " : "[FAILED] ")
        + ActionName
        + ": "
        + Session.GetLastMessage());
}

void FSandboxEditorApp::StartNewSession()
{
    Session.Shutdown();
    if (Session.Initialize() && Session.Create())
    {
        AppendEvent("[OK] New Session: created a default SandboxHero");
    }
    else
    {
        AppendEvent("[FAILED] New Session: " + Session.GetLastMessage());
    }
}

void FSandboxEditorApp::AppendEvent(std::string Event)
{
    Events.push_back(std::move(Event));
    if (Events.size() > 100)
    {
        Events.erase(Events.begin());
    }
}
}
