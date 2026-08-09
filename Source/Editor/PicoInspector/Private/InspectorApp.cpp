#include "InspectorApp.h"
#include "GarbageCollectionExperiment.h"
#include "NativeDelegateExperiment.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Function.h"
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
#include <sstream>
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

void DrawSectionLabel(const char* Label)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(Label);
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

const char* GetInvokeResultName(EFunctionInvokeResult Result)
{
    switch (Result)
    {
    case EFunctionInvokeResult::Success: return "Success";
    case EFunctionInvokeResult::InvalidFunction: return "InvalidFunction";
    case EFunctionInvokeResult::InvalidTarget: return "InvalidTarget";
    case EFunctionInvokeResult::ArgumentCountMismatch: return "ArgumentCountMismatch";
    case EFunctionInvokeResult::ArgumentTypeMismatch: return "ArgumentTypeMismatch";
    case EFunctionInvokeResult::InvalidObjectArgument: return "InvalidObjectArgument";
    case EFunctionInvokeResult::MissingReturnStorage: return "MissingReturnStorage";
    case EFunctionInvokeResult::InvocationFailed: return "InvocationFailed";
    }
    return "Unknown";
}

std::string GetFunctionFlagsText(EFunctionFlags Flags)
{
    std::string Result;
    const auto Append = [&Result](const char* Name)
    {
        if (!Result.empty())
        {
            Result += " | ";
        }
        Result += Name;
    };
    if (HasAnyFlags(Flags, EFunctionFlags::Native)) Append("Native");
    if (HasAnyFlags(Flags, EFunctionFlags::Callable)) Append("Callable");
    if (HasAnyFlags(Flags, EFunctionFlags::Pure)) Append("Pure");
    if (HasAnyFlags(Flags, EFunctionFlags::Const)) Append("Const");
    if (HasAnyFlags(Flags, EFunctionFlags::Server)) Append("Server");
    if (HasAnyFlags(Flags, EFunctionFlags::Client)) Append("Client");
    if (HasAnyFlags(Flags, EFunctionFlags::NetMulticast)) Append("NetMulticast");
    if (HasAnyFlags(Flags, EFunctionFlags::Reliable)) Append("Reliable");
    return Result.empty() ? "None" : Result;
}

std::string GetFunctionValueText(const FFunctionValue& Value)
{
    return std::visit(
        [](const auto& TypedValue) -> std::string
        {
            using TValue = std::decay_t<decltype(TypedValue)>;
            std::ostringstream Stream;
            if constexpr (std::is_same_v<TValue, std::monostate>)
            {
                return "Void";
            }
            else if constexpr (std::is_same_v<TValue, bool>)
            {
                return TypedValue ? "true" : "false";
            }
            else if constexpr (std::is_same_v<TValue, FName>)
            {
                return TypedValue.ToString();
            }
            else if constexpr (std::is_same_v<TValue, std::string>)
            {
                return TypedValue;
            }
            else if constexpr (std::is_same_v<TValue, FVector3>)
            {
                Stream << '(' << TypedValue.X << ", " << TypedValue.Y << ", " << TypedValue.Z << ')';
            }
            else if constexpr (std::is_same_v<TValue, FRotator>)
            {
                Stream << "(Pitch=" << TypedValue.Pitch << ", Yaw=" << TypedValue.Yaw
                       << ", Roll=" << TypedValue.Roll << ')';
            }
            else if constexpr (std::is_same_v<TValue, FTransform>)
            {
                Stream << "Transform(Location=" << TypedValue.Translation.X << ','
                       << TypedValue.Translation.Y << ',' << TypedValue.Translation.Z << ')';
            }
            else if constexpr (std::is_same_v<TValue, FAssetPath>)
            {
                return std::string(TypedValue.ToString());
            }
            else if constexpr (std::is_same_v<TValue, PObject*>)
            {
                return TypedValue != nullptr ? TypedValue->GetPathName() : "None";
            }
            else
            {
                Stream << TypedValue;
            }
            return Stream.str();
        },
        Value);
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

    Experiments.push_back(std::make_unique<FNativeDelegateExperiment>());
    Experiments.push_back(std::make_unique<FGarbageCollectionExperiment>());
    for (const std::unique_ptr<IInspectorExperiment>& Experiment : Experiments)
    {
        if (!Experiment->SetUp())
        {
            SetStatus(std::string("Failed to set up ") + Experiment->GetName(), true);
        }
    }
}

FInspectorApp::~FInspectorApp() = default;

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

    if (ImGui::BeginTabBar("InspectorModes"))
    {
        if (ImGui::BeginTabItem("Experiments"))
        {
            DrawExperiments();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Runtime Browser"))
        {
            DrawRuntimeBrowser();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    const ImVec4 StatusColor = bStatusIsError
        ? ImVec4(0.95f, 0.42f, 0.35f, 1.0f)
        : ImVec4(0.35f, 0.78f, 0.66f, 1.0f);
    ImGui::TextColored(StatusColor, "%s", Status.empty() ? "Ready" : Status.c_str());
    ImGui::End();
}

void FInspectorApp::DrawRuntimeBrowser()
{
    if (!ImGui::BeginTable(
            "InspectorColumns",
            3,
            ImGuiTableFlags_Resizable
                | ImGuiTableFlags_BordersInnerV
                | ImGuiTableFlags_NoSavedSettings
                | ImGuiTableFlags_SizingStretchProp))
    {
        return;
    }
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

void FInspectorApp::DrawExperiments()
{
    if (Experiments.empty())
    {
        ImGui::TextUnformatted("No experiments registered");
        return;
    }

    ImGui::BeginChild("ExperimentList", ImVec2(190.0f, 0.0f), true);
    for (std::size_t Index = 0; Index < Experiments.size(); ++Index)
    {
        if (ImGui::Selectable(
                Experiments[Index]->GetName(),
                SelectedExperimentIndex == Index))
        {
            SelectedExperimentIndex = Index;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("ExperimentContent", ImVec2(0.0f, 0.0f), true);
    Experiments[SelectedExperimentIndex]->Draw();
    ImGui::EndChild();
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
            ResetFunctionEditor();
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
            const bool bSelectionChanged = SelectedObjectHandle != Object->GetHandle();
            SelectedObjectHandle = Object->GetHandle();
            SelectedClass = Object->GetClass();
            if (bSelectionChanged)
            {
                ResetFunctionEditor();
            }
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
    if (ImGui::BeginTabBar("ObjectDetailTabs"))
    {
        if (ImGui::BeginTabItem("Properties"))
        {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Functions"))
        {
            DrawFunctionPanel(Object);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void FInspectorApp::DrawFunctionPanel(PObject* Object)
{
    const std::vector<const PFunction*> Functions = GetAllFunctions(Object->GetClass());
    ImGui::BeginChild("FunctionList", ImVec2(180.0f, 0.0f), true);
    for (const PFunction* Function : Functions)
    {
        const std::string Name = Function->GetName().ToString();
        if (ImGui::Selectable(Name.c_str(), SelectedFunction == Function))
        {
            ResetFunctionEditor(Function);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Declared by %s",
                Function->GetOwnerClass()->GetName().ToString().c_str());
        }
    }
    if (Functions.empty())
    {
        ImGui::TextDisabled("No reflected functions");
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("FunctionEditor", ImVec2(0.0f, 0.0f), true);
    DrawFunctionEditor(Object);
    ImGui::EndChild();
}

void FInspectorApp::DrawFunctionEditor(PObject* Object)
{
    if (SelectedFunction == nullptr)
    {
        ImGui::TextUnformatted("Select a reflected function");
        return;
    }

    ImGui::Text("%s", SelectedFunction->GetName().ToString().c_str());
    ImGui::TextDisabled(
        "Declared by %s",
        SelectedFunction->GetOwnerClass()->GetName().ToString().c_str());
    ImGui::TextWrapped("Flags: %s", GetFunctionFlagsText(SelectedFunction->GetFlags()).c_str());
    ImGui::Text(
        "Return: %s",
        GetFunctionValueTypeName(SelectedFunction->GetReturnValue().Type).data());
    DrawSectionLabel("Parameters");
    for (std::size_t Index = 0; Index < FunctionInputs.size(); ++Index)
    {
        DrawFunctionParameter(Index);
    }
    if (FunctionInputs.empty())
    {
        ImGui::TextDisabled("No parameters");
    }

    if (SelectedFunction->GetReturnValue().Type != EFunctionValueType::Void)
    {
        ImGui::Checkbox("Provide return storage", &bProvideFunctionReturnStorage);
    }
    if (ImGui::Button("Invoke"))
    {
        InvokeSelectedFunction(Object);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Parameters"))
    {
        ResetFunctionEditor(SelectedFunction);
    }

    if (bHasFunctionInvokeResult)
    {
        DrawSectionLabel("Result");
        const bool bSuccess = FunctionInvokeResult == EFunctionInvokeResult::Success;
        ImGui::TextColored(
            bSuccess ? ImVec4(0.35f, 0.78f, 0.66f, 1.0f) : ImVec4(0.95f, 0.42f, 0.35f, 1.0f),
            "%s",
            GetInvokeResultName(FunctionInvokeResult));
        if (bSuccess)
        {
            ImGui::TextWrapped("Return value: %s", GetFunctionValueText(FunctionReturnValue).c_str());
        }
    }
}

void FInspectorApp::DrawFunctionParameter(std::size_t Index)
{
    if (SelectedFunction == nullptr || Index >= FunctionInputs.size())
    {
        return;
    }
    const FFunctionParameter& Parameter = SelectedFunction->GetParameters()[Index];
    FFunctionParameterInput& Input = FunctionInputs[Index];
    const std::string Label = Parameter.Name.ToString();

    ImGui::PushID(static_cast<int>(Index));
    ImGui::Text(
        "%s (%s)",
        Label.c_str(),
        GetFunctionValueTypeName(Parameter.Value.Type).data());
    ImGui::SetNextItemWidth(-1.0f);
    switch (Parameter.Value.Type)
    {
    case EFunctionValueType::Int32:
        ImGui::InputInt("##Value", &std::get<int32>(Input.Value));
        break;
    case EFunctionValueType::Float:
        ImGui::DragFloat("##Value", &std::get<float>(Input.Value), 0.1f);
        break;
    case EFunctionValueType::Bool:
        ImGui::Checkbox("##Value", &std::get<bool>(Input.Value));
        break;
    case EFunctionValueType::Name:
    case EFunctionValueType::String:
    case EFunctionValueType::AssetPath:
        ImGui::InputText("##Value", Input.Text.data(), Input.Text.size());
        break;
    case EFunctionValueType::Vector3:
        DrawVector3Control("##Value", std::get<FVector3>(Input.Value));
        break;
    case EFunctionValueType::Rotator:
        DrawRotatorControl("##Value", std::get<FRotator>(Input.Value));
        break;
    case EFunctionValueType::Transform:
        DrawTransformControl(std::get<FTransform>(Input.Value));
        break;
    case EFunctionValueType::Object:
    {
        PObject* SelectedObject = ResolveObject(Input.ObjectHandle);
        const std::string Preview = SelectedObject != nullptr ? SelectedObject->GetPathName() : "None";
        if (ImGui::BeginCombo("##Value", Preview.c_str()))
        {
            if (ImGui::Selectable("None", SelectedObject == nullptr))
            {
                Input.ObjectHandle = {};
            }
            for (PObject* Candidate : FObjectRegistry::GetObjects())
            {
                if (!Candidate->IsA(Parameter.Value.ObjectClass))
                {
                    continue;
                }
                const std::string CandidateLabel =
                    Candidate->GetPathName() + " (" + Candidate->GetClass()->GetName().ToString() + ')';
                if (ImGui::Selectable(
                        CandidateLabel.c_str(),
                        Candidate->GetHandle() == Input.ObjectHandle))
                {
                    Input.ObjectHandle = Candidate->GetHandle();
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    case EFunctionValueType::Void:
        ImGui::TextDisabled("Void is not a valid parameter type");
        break;
    }
    ImGui::PopID();
}

void FInspectorApp::ResetFunctionEditor(const PFunction* Function)
{
    SelectedFunction = Function;
    FunctionInputs.clear();
    FunctionReturnValue = std::monostate {};
    FunctionInvokeResult = EFunctionInvokeResult::InvalidFunction;
    bHasFunctionInvokeResult = false;
    bProvideFunctionReturnStorage = true;
    if (Function == nullptr)
    {
        return;
    }

    FunctionInputs.reserve(Function->GetParameters().size());
    for (const FFunctionParameter& Parameter : Function->GetParameters())
    {
        FFunctionParameterInput Input;
        switch (Parameter.Value.Type)
        {
        case EFunctionValueType::Int32: Input.Value = int32 { 0 }; break;
        case EFunctionValueType::Float: Input.Value = 0.0f; break;
        case EFunctionValueType::Bool: Input.Value = false; break;
        case EFunctionValueType::Name: Input.Value = FName(); break;
        case EFunctionValueType::String: Input.Value = std::string(); break;
        case EFunctionValueType::Vector3: Input.Value = FVector3(); break;
        case EFunctionValueType::Rotator: Input.Value = FRotator(); break;
        case EFunctionValueType::Transform: Input.Value = FTransform(); break;
        case EFunctionValueType::AssetPath: Input.Value = FAssetPath(); break;
        case EFunctionValueType::Object: Input.Value = static_cast<PObject*>(nullptr); break;
        case EFunctionValueType::Void: Input.Value = std::monostate {}; break;
        }
        FunctionInputs.push_back(std::move(Input));
    }
}

void FInspectorApp::InvokeSelectedFunction(PObject* Object)
{
    if (SelectedFunction == nullptr || Object == nullptr)
    {
        FunctionInvokeResult = EFunctionInvokeResult::InvalidTarget;
        bHasFunctionInvokeResult = true;
        return;
    }

    std::vector<FFunctionValue> Arguments;
    Arguments.reserve(FunctionInputs.size());
    for (std::size_t Index = 0; Index < FunctionInputs.size(); ++Index)
    {
        const EFunctionValueType Type = SelectedFunction->GetParameters()[Index].Value.Type;
        FFunctionParameterInput& Input = FunctionInputs[Index];
        switch (Type)
        {
        case EFunctionValueType::Name:
            Input.Value = FName(Input.Text.data());
            break;
        case EFunctionValueType::String:
            Input.Value = std::string(Input.Text.data());
            break;
        case EFunctionValueType::AssetPath:
        {
            FAssetPath Path;
            if (!FAssetPath::TryParse(Input.Text.data(), Path))
            {
                SetStatus("AssetPath parameter is invalid", true);
                return;
            }
            Input.Value = Path;
            break;
        }
        case EFunctionValueType::Object:
            Input.Value = ResolveObject(Input.ObjectHandle);
            break;
        default:
            break;
        }
        Arguments.push_back(Input.Value);
    }

    FFunctionValue* ReturnStorage =
        SelectedFunction->GetReturnValue().Type == EFunctionValueType::Void
            || bProvideFunctionReturnStorage
        ? &FunctionReturnValue
        : nullptr;
    FunctionInvokeResult = Object->ProcessEvent(
        SelectedFunction,
        Arguments,
        ReturnStorage);
    bHasFunctionInvokeResult = true;
    SetStatus(
        std::string("Invoke ") + SelectedFunction->GetName().ToString()
            + ": " + GetInvokeResultName(FunctionInvokeResult),
        FunctionInvokeResult != EFunctionInvokeResult::Success);
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
    case EPropertyType::Object:
    {
        PObject* Referenced = Property->GetReferencedObject(Object);
        ImGui::Text(
            "%s (%s)",
            Referenced != nullptr ? Referenced->GetPathName().c_str() : "None",
            Property->GetObjectReferenceKind() == EObjectReferenceKind::Strong ? "Strong" : "Weak");
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
    ResetFunctionEditor();
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
    ResetFunctionEditor();
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
    ResetFunctionEditor();
    SetStatus("Loaded " + Object->GetPathName());
}

void FInspectorApp::SetStatus(std::string Message, bool bIsError)
{
    Status = std::move(Message);
    bStatusIsError = bIsError;
}
}
