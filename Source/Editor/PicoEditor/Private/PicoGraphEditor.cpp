#include "PicoGraphEditor.h"

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Core/Paths.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Graph/GraphAsset.h"
#include "Pico/Graph/GraphCompiler.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
template<std::size_t Size>
void CopyToBuffer(std::string_view Text, std::array<char, Size>& Buffer)
{
    Buffer.fill('\0');
    const std::size_t Count = std::min(Text.size(), Size - 1);
    std::memcpy(Buffer.data(), Text.data(), Count);
}

bool ResolveGraphFile(const FAssetPath& AssetPath, std::filesystem::path& OutFile)
{
    const std::string Path(AssetPath.ToString());
    constexpr std::string_view Prefix = "/Game/";
    if (!AssetPath.IsValid() || !Path.starts_with(Prefix) || !FPaths::HasProject())
        return false;
    OutFile = FPaths::GetProjectContentDir() / Path.substr(Prefix.size());
    return true;
}

ImU32 PinColor(EGraphValueType Type)
{
    switch (Type)
    {
    case EGraphValueType::Exec: return IM_COL32(235, 235, 235, 255);
    case EGraphValueType::Bool: return IM_COL32(210, 70, 70, 255);
    case EGraphValueType::Int: return IM_COL32(70, 190, 190, 255);
    case EGraphValueType::Float: return IM_COL32(80, 200, 110, 255);
    case EGraphValueType::String: return IM_COL32(210, 90, 180, 255);
    case EGraphValueType::Vector: return IM_COL32(235, 190, 70, 255);
    case EGraphValueType::Object: return IM_COL32(90, 150, 235, 255);
    }
    return IM_COL32_WHITE;
}

bool HasCompileErrors(const std::vector<FGraphDiagnostic>& Diagnostics)
{
    return std::any_of(Diagnostics.begin(), Diagnostics.end(),
        [](const FGraphDiagnostic& Diagnostic)
        {
            return Diagnostic.Severity == EGraphDiagnosticSeverity::Error;
        });
}
}

struct FPicoGraphEditor::FImpl
{
    FEngineLoop* EngineLoop = nullptr;
    FStatus SetStatus;
    FAssetCreated AssetCreated;
    FAssetPath OpenedAsset;
    FPicoGraphAsset Graph;
    FGraphTransactionHistory Transactions;
    std::array<char, 192> CreateFolder {};
    std::array<char, 96> CreateName {};
    std::array<char, 96> VariableName {};
    std::array<char, 128> VariableDefault {};
    int VariableType = static_cast<int>(EGraphValueType::Float);
    ImVec2 Pan {40.0f, 40.0f};
    std::string SelectedNodeId;
    std::string PendingPinId;
    std::string DraggedNodeId;
    std::vector<std::string> NodeZOrder;
    FPicoGraphAsset DragSnapshot;
    FPicoGraphAsset PinEditSnapshot;
    FGraphCompileResult CompileResult;
    bool bDraggingNode = false;
    bool bEditingPin = false;
    bool bHasCompileResult = false;
    bool bLastActionCompiled = false;
    bool bOpen = false;
    bool bCreateOpen = false;
    bool bVariablePopupOpen = false;
    bool bDirty = false;
    bool bKeyboardFocused = false;

    void Report(std::string Message, bool bError = false) const
    {
        if (SetStatus) SetStatus(std::move(Message), bError);
    }

    void PushUndo(FPicoGraphAsset Before)
    {
        Transactions.Record(std::move(Before));
        bDirty = true;
        bHasCompileResult = false;
    }

    void Undo()
    {
        if (!Transactions.Undo(Graph)) return;
        SelectedNodeId.clear();
        PendingPinId.clear();
        bDirty = true;
        bHasCompileResult = false;
        SyncNodeZOrder();
    }

    void Redo()
    {
        if (!Transactions.Redo(Graph)) return;
        SelectedNodeId.clear();
        PendingPinId.clear();
        bDirty = true;
        bHasCompileResult = false;
        SyncNodeZOrder();
    }

    void Open(const FAssetPath& AssetPath)
    {
        const FAssetRecord* Record = EngineLoop != nullptr
            ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
        EGraphAssetError Error = EGraphAssetError::None;
        FPicoGraphAsset Loaded;
        if (Record == nullptr || Record->Type != EAssetType::PicoGraph
            || !LoadGraphAssetFromFile(Record->FilePath, Loaded, &Error))
        {
            Report("Could not open PicoGraph: " + std::string(ToString(Error)), true);
            return;
        }
        Graph = std::move(Loaded);
        OpenedAsset = AssetPath;
        Transactions.Clear();
        SelectedNodeId.clear();
        PendingPinId.clear();
        DraggedNodeId.clear();
        Pan = ImVec2(40.0f, 40.0f);
        bDirty = false;
        bHasCompileResult = false;
        bOpen = true;
        NodeZOrder.clear();
        SyncNodeZOrder();
        Report("Opened " + std::string(AssetPath.ToString()));
    }

    void Validate()
    {
        CompileResult = {};
        CompileResult.Diagnostics = ValidateGraphSemantics(Graph);
        CompileResult.bSucceeded = !HasCompileErrors(CompileResult.Diagnostics);
        bHasCompileResult = true;
        bLastActionCompiled = false;
        const std::size_t ErrorCount = static_cast<std::size_t>(std::count_if(
            CompileResult.Diagnostics.begin(), CompileResult.Diagnostics.end(),
            [](const FGraphDiagnostic& Diagnostic)
            {
                return Diagnostic.Severity == EGraphDiagnosticSeverity::Error;
            }));
        Report(CompileResult.bSucceeded
            ? "PicoGraph validation passed"
            : "PicoGraph validation failed with " + std::to_string(ErrorCount) + " error(s)",
            !CompileResult.bSucceeded);
    }

    void Compile()
    {
        CompileResult = CompileGraph(Graph);
        bHasCompileResult = true;
        bLastActionCompiled = true;
        if (CompileResult.bSucceeded)
        {
            Report("Compiled PicoGraph: "
                + std::to_string(CompileResult.IR.Instructions.size()) + " IR instruction(s), "
                + std::to_string(CompileResult.Bytecode.Bytes.size()) + " byte(s)");
        }
        else
        {
            const std::size_t ErrorCount = static_cast<std::size_t>(std::count_if(
                CompileResult.Diagnostics.begin(), CompileResult.Diagnostics.end(),
                [](const FGraphDiagnostic& Diagnostic)
                {
                    return Diagnostic.Severity == EGraphDiagnosticSeverity::Error;
                }));
            Report("PicoGraph compile failed with " + std::to_string(ErrorCount) + " error(s)", true);
        }
    }

    void AddSchemaNode(std::string_view TypeName)
    {
        FGraphNode Node;
        if (!MakeSchemaGraphNode(TypeName, 380.0f, 180.0f, Node))
        {
            Report("No PicoGraph Schema for node type " + std::string(TypeName), true);
            return;
        }
        const FPicoGraphAsset Before = Graph;
        SelectedNodeId = Node.Id;
        Graph.Nodes.push_back(std::move(Node));
        PushUndo(Before);
        SyncNodeZOrder();
    }

    FGraphNode* FindNode(std::string_view NodeId)
    {
        const auto Found = std::find_if(Graph.Nodes.begin(), Graph.Nodes.end(),
            [NodeId](const FGraphNode& Node) { return Node.Id == NodeId; });
        return Found == Graph.Nodes.end() ? nullptr : &*Found;
    }

    const FGraphNode* FindNode(std::string_view NodeId) const
    {
        const auto Found = std::find_if(Graph.Nodes.begin(), Graph.Nodes.end(),
            [NodeId](const FGraphNode& Node) { return Node.Id == NodeId; });
        return Found == Graph.Nodes.end() ? nullptr : &*Found;
    }

    void SyncNodeZOrder()
    {
        NodeZOrder.erase(
            std::remove_if(NodeZOrder.begin(), NodeZOrder.end(),
                [this](const std::string& NodeId) { return FindNode(NodeId) == nullptr; }),
            NodeZOrder.end());
        for (const FGraphNode& Node : Graph.Nodes)
            if (std::find(NodeZOrder.begin(), NodeZOrder.end(), Node.Id) == NodeZOrder.end())
                NodeZOrder.push_back(Node.Id);
    }

    void BringNodeToFront(std::string_view NodeId)
    {
        const auto Found = std::find(NodeZOrder.begin(), NodeZOrder.end(), NodeId);
        if (Found == NodeZOrder.end() || std::next(Found) == NodeZOrder.end()) return;
        std::string StableId = std::move(*Found);
        NodeZOrder.erase(Found);
        NodeZOrder.push_back(std::move(StableId));
    }

    bool DeleteSelectedNode()
    {
        if (SelectedNodeId.empty()) return false;
        const FPicoGraphAsset Before = Graph;
        if (!RemoveGraphNode(Graph, SelectedNodeId)) return false;
        SelectedNodeId.clear();
        PendingPinId.clear();
        DraggedNodeId.clear();
        bDraggingNode = false;
        PushUndo(Before);
        SyncNodeZOrder();
        return true;
    }

    void Save()
    {
        std::filesystem::path File;
        EGraphAssetError Error = EGraphAssetError::None;
        if (!ResolveGraphFile(OpenedAsset, File)
            || !SaveGraphAssetToFile(File, Graph, &Error))
        {
            Report("Could not save PicoGraph: " + std::string(ToString(Error)), true);
            return;
        }
        bDirty = false;
        Report("Saved " + std::string(OpenedAsset.ToString()));
    }

    void DrawCreatePopup()
    {
        if (bCreateOpen)
        {
            ImGui::OpenPopup("Create PicoGraph");
            bCreateOpen = false;
        }
        if (!ImGui::BeginPopupModal(
                "Create PicoGraph", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::InputText("Folder", CreateFolder.data(), CreateFolder.size());
        ImGui::InputText("Asset Name", CreateName.data(), CreateName.size());
        const bool bCanCreate = CreateFolder[0] != '\0' && CreateName[0] != '\0';
        ImGui::BeginDisabled(!bCanCreate);
        if (ImGui::Button("Create"))
        {
            std::string Folder = CreateFolder.data();
            while (!Folder.empty() && Folder.back() == '/') Folder.pop_back();
            FAssetPath AssetPath;
            std::filesystem::path File;
            const std::string VirtualPath = Folder + "/" + CreateName.data() + ".pgraph";
            FPicoGraphAsset NewGraph;
            NewGraph.GraphId = CreateGraphStableId();
            NewGraph.Nodes.push_back(MakeEntryEventNode("BeginPlay", 80.0f, 120.0f));
            EGraphAssetError Error = EGraphAssetError::None;
            if (!FAssetPath::TryParse(VirtualPath, AssetPath)
                || !ResolveGraphFile(AssetPath, File) || std::filesystem::exists(File)
                || !SaveGraphAssetToFile(File, NewGraph, &Error))
            {
                Report("Could not create PicoGraph: " + std::string(ToString(Error)), true);
            }
            else
            {
                FAssetScanReport Scan;
                EngineLoop->GetAssetRegistry().ScanProjectContent(&Scan);
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

    void DrawVariablePopup()
    {
        if (bVariablePopupOpen)
        {
            ImGui::OpenPopup("Add Graph Variable");
            bVariablePopupOpen = false;
        }
        if (!ImGui::BeginPopupModal(
                "Add Graph Variable", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::InputText("Name", VariableName.data(), VariableName.size());
        constexpr const char* Types[] = {"Bool", "Int", "Float", "String", "Vector", "Object"};
        int ComboType = std::clamp(VariableType - 1, 0, 5);
        if (ImGui::Combo("Type", &ComboType, Types, static_cast<int>(std::size(Types))))
            VariableType = ComboType + 1;
        ImGui::InputText("Default", VariableDefault.data(), VariableDefault.size());
        ImGui::BeginDisabled(VariableName[0] == '\0');
        if (ImGui::Button("Add"))
        {
            const FPicoGraphAsset Before = Graph;
            Graph.Variables.push_back(FGraphVariable {
                CreateGraphStableId(), VariableName.data(),
                static_cast<EGraphValueType>(VariableType), VariableDefault.data()});
            PushUndo(Before);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImVec2 PinPosition(
        const FGraphNode& Node,
        std::size_t PinIndex,
        const ImVec2& CanvasOrigin) const
    {
        const FGraphPin& Pin = Node.Pins[PinIndex];
        const float X = CanvasOrigin.x + Pan.x + Node.PositionX
            + (Pin.Direction == EGraphPinDirection::Output ? 220.0f : 0.0f);
        const float Y = CanvasOrigin.y + Pan.y + Node.PositionY + 48.0f
            + static_cast<float>(PinIndex) * 24.0f;
        return ImVec2(X, Y);
    }

    const FGraphNode* FindNodeForPin(std::string_view PinId, std::size_t& OutIndex) const
    {
        for (const FGraphNode& Node : Graph.Nodes)
            for (std::size_t Index = 0; Index < Node.Pins.size(); ++Index)
                if (Node.Pins[Index].Id == PinId)
                {
                    OutIndex = Index;
                    return &Node;
                }
        return nullptr;
    }

    void DrawCanvas()
    {
        const ImVec2 Size = ImGui::GetContentRegionAvail();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const ImVec2 End(Origin.x + Size.x, Origin.y + Size.y);
        ImGui::Dummy(Size);
        const bool bCanvasHovered = ImGui::IsWindowHovered()
            && ImGui::IsMouseHoveringRect(Origin, End, true);
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        DrawList->AddRectFilled(Origin, End, IM_COL32(23, 26, 29, 255));
        DrawList->PushClipRect(Origin, End, true);
        constexpr float Grid = 32.0f;
        for (float X = std::fmod(Pan.x, Grid); X < Size.x; X += Grid)
            DrawList->AddLine(ImVec2(Origin.x + X, Origin.y), ImVec2(Origin.x + X, End.y),
                IM_COL32(52, 57, 62, 120));
        for (float Y = std::fmod(Pan.y, Grid); Y < Size.y; Y += Grid)
            DrawList->AddLine(ImVec2(Origin.x, Origin.y + Y), ImVec2(End.x, Origin.y + Y),
                IM_COL32(52, 57, 62, 120));
        if (bCanvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            Pan.x += ImGui::GetIO().MouseDelta.x;
            Pan.y += ImGui::GetIO().MouseDelta.y;
        }

        SyncNodeZOrder();
        std::string HitNodeId;
        std::string HitPinId;
        bool bHitNodeBody = false;
        if (bCanvasHovered)
        {
            const ImVec2 Mouse = ImGui::GetIO().MousePos;
            for (auto It = NodeZOrder.rbegin(); It != NodeZOrder.rend(); ++It)
            {
                const FGraphNode* Node = FindNode(*It);
                if (Node == nullptr) continue;
                for (std::size_t PinIndex = 0; PinIndex < Node->Pins.size(); ++PinIndex)
                {
                    const ImVec2 Position = PinPosition(*Node, PinIndex, Origin);
                    const float DeltaX = Mouse.x - Position.x;
                    const float DeltaY = Mouse.y - Position.y;
                    if (DeltaX * DeltaX + DeltaY * DeltaY <= 100.0f)
                    {
                        HitNodeId = Node->Id;
                        HitPinId = Node->Pins[PinIndex].Id;
                        break;
                    }
                }
                if (!HitPinId.empty()) break;
                const ImVec2 NodeMin(
                    Origin.x + Pan.x + Node->PositionX,
                    Origin.y + Pan.y + Node->PositionY);
                const float Height = 64.0f + static_cast<float>(Node->Pins.size()) * 24.0f;
                if (Mouse.x >= NodeMin.x && Mouse.x <= NodeMin.x + 220.0f
                    && Mouse.y >= NodeMin.y && Mouse.y <= NodeMin.y + Height)
                {
                    HitNodeId = Node->Id;
                    bHitNodeBody = true;
                    break;
                }
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (!HitPinId.empty())
                {
                    SelectedNodeId = HitNodeId;
                    BringNodeToFront(HitNodeId);
                    if (PendingPinId.empty()) PendingPinId = HitPinId;
                    else
                    {
                        const FPicoGraphAsset Before = Graph;
                        EGraphAssetError Error = EGraphAssetError::None;
                        if (AddGraphLink(Graph, PendingPinId, HitPinId, &Error))
                            PushUndo(Before);
                        else Report("Could not connect pins: "
                            + std::string(ToString(Error)), true);
                        PendingPinId.clear();
                    }
                }
                else if (bHitNodeBody)
                {
                    SelectedNodeId = HitNodeId;
                    BringNodeToFront(HitNodeId);
                    DragSnapshot = Graph;
                    DraggedNodeId = HitNodeId;
                    bDraggingNode = true;
                }
                else
                {
                    SelectedNodeId.clear();
                    PendingPinId.clear();
                }
            }
            if (!HitPinId.empty()
                && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                const FPicoGraphAsset Before = Graph;
                const auto NewEnd = std::remove_if(
                    Graph.Links.begin(), Graph.Links.end(),
                    [&HitPinId](const FGraphLink& Link)
                    {
                        return Link.OutputPinId == HitPinId
                            || Link.InputPinId == HitPinId;
                    });
                if (NewEnd != Graph.Links.end())
                {
                    Graph.Links.erase(NewEnd, Graph.Links.end());
                    PushUndo(Before);
                }
                PendingPinId.clear();
            }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                PendingPinId.clear();
            }
        }

        if (bDraggingNode)
        {
            FGraphNode* Dragged = FindNode(DraggedNodeId);
            if (Dragged != nullptr && ImGui::IsMouseDown(ImGuiMouseButton_Left)
                && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                Dragged->PositionX += ImGui::GetIO().MouseDelta.x;
                Dragged->PositionY += ImGui::GetIO().MouseDelta.y;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || Dragged == nullptr)
            {
                bDraggingNode = false;
                const FGraphNode* BeforeNode = Dragged != nullptr
                    ? [&]() -> const FGraphNode*
                    {
                        const auto Found = std::find_if(
                            DragSnapshot.Nodes.begin(), DragSnapshot.Nodes.end(),
                            [this](const FGraphNode& Node) { return Node.Id == DraggedNodeId; });
                        return Found == DragSnapshot.Nodes.end() ? nullptr : &*Found;
                    }()
                    : nullptr;
                if (Dragged != nullptr && BeforeNode != nullptr
                    && (Dragged->PositionX != BeforeNode->PositionX
                        || Dragged->PositionY != BeforeNode->PositionY))
                    PushUndo(std::move(DragSnapshot));
                DraggedNodeId.clear();
            }
        }

        for (const FGraphLink& Link : Graph.Links)
        {
            std::size_t OutputIndex = 0;
            std::size_t InputIndex = 0;
            const FGraphNode* OutputNode = FindNodeForPin(Link.OutputPinId, OutputIndex);
            const FGraphNode* InputNode = FindNodeForPin(Link.InputPinId, InputIndex);
            if (OutputNode == nullptr || InputNode == nullptr) continue;
            const ImVec2 A = PinPosition(*OutputNode, OutputIndex, Origin);
            const ImVec2 B = PinPosition(*InputNode, InputIndex, Origin);
            const float Bend = std::max(60.0f, std::abs(B.x - A.x) * 0.5f);
            DrawList->AddBezierCubic(A, ImVec2(A.x + Bend, A.y),
                ImVec2(B.x - Bend, B.y), B, IM_COL32(210, 210, 210, 255), 3.0f);
        }

        for (const std::string& NodeId : NodeZOrder)
        {
            const FGraphNode* Node = FindNode(NodeId);
            if (Node == nullptr) continue;
            const ImVec2 NodeMin(
                Origin.x + Pan.x + Node->PositionX,
                Origin.y + Pan.y + Node->PositionY);
            const float Height = 64.0f + static_cast<float>(Node->Pins.size()) * 24.0f;
            const ImVec2 NodeMax(NodeMin.x + 220.0f, NodeMin.y + Height);
            const bool bSelected = SelectedNodeId == Node->Id;
            DrawList->AddRectFilled(NodeMin, NodeMax, IM_COL32(37, 41, 46, 255), 5.0f);
            DrawList->AddRectFilled(NodeMin, ImVec2(NodeMax.x, NodeMin.y + 32.0f),
                Node->TypeName == "EntryEvent"
                    ? IM_COL32(132, 45, 55, 255) : IM_COL32(45, 92, 128, 255), 5.0f);
            DrawList->AddRect(NodeMin, NodeMax,
                bSelected ? IM_COL32(245, 178, 62, 255) : IM_COL32(95, 102, 110, 255),
                5.0f, 0, bSelected ? 2.5f : 1.0f);
            DrawList->AddText(ImVec2(NodeMin.x + 10.0f, NodeMin.y + 8.0f),
                IM_COL32_WHITE, Node->DisplayName.c_str());

            for (std::size_t Index = 0; Index < Node->Pins.size(); ++Index)
            {
                const FGraphPin& Pin = Node->Pins[Index];
                const ImVec2 Position = PinPosition(*Node, Index, Origin);
                DrawList->AddCircleFilled(Position, 6.0f, PinColor(Pin.Type));
                const float TextX = Pin.Direction == EGraphPinDirection::Input
                    ? Position.x + 11.0f
                    : Position.x - 11.0f - ImGui::CalcTextSize(Pin.Name.c_str()).x;
                DrawList->AddText(ImVec2(TextX, Position.y - 7.0f), IM_COL32(225, 225, 225, 255),
                    Pin.Name.c_str());
            }
        }
        if (!PendingPinId.empty())
        {
            std::size_t Index = 0;
            const FGraphNode* Node = FindNodeForPin(PendingPinId, Index);
            if (Node != nullptr)
                DrawList->AddLine(PinPosition(*Node, Index, Origin), ImGui::GetIO().MousePos,
                    IM_COL32(245, 178, 62, 255), 2.0f);
        }
        DrawList->PopClipRect();
    }

    void DrawVariables()
    {
        if (ImGui::Button("+ Variable"))
        {
            CopyToBuffer("NewVariable", VariableName);
            CopyToBuffer("0.0", VariableDefault);
            VariableType = static_cast<int>(EGraphValueType::Float);
            bVariablePopupOpen = true;
        }
        ImGui::Separator();
        for (std::size_t Index = 0; Index < Graph.Variables.size(); ++Index)
        {
            const FGraphVariable& Variable = Graph.Variables[Index];
            ImGui::PushID(Variable.Id.c_str());
            ImGui::TextUnformatted(Variable.Name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s = %s", ToString(Variable.Type).data(), Variable.DefaultValue.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                const FPicoGraphAsset Before = Graph;
                Graph.Variables.erase(Graph.Variables.begin() + static_cast<std::ptrdiff_t>(Index));
                PushUndo(Before);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    void DrawDiagnostics()
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Compile Diagnostics");
        if (!bHasCompileResult)
        {
            ImGui::TextDisabled("Graph changed or has not been validated.");
            return;
        }
        if (CompileResult.bSucceeded)
        {
            const ImVec4 SuccessColor(0.35f, 0.82f, 0.55f, 1.0f);
            ImGui::TextColored(SuccessColor, "%s passed",
                bLastActionCompiled ? "Compile" : "Validation");
            if (bLastActionCompiled)
            {
                ImGui::Text("Typed IR: %zu instruction(s)",
                    CompileResult.IR.Instructions.size());
                ImGui::Text("Bytecode: PGRB v%u, %zu byte(s)",
                    CompileResult.Bytecode.Version,
                    CompileResult.Bytecode.Bytes.size());
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f),
                "%s failed", bLastActionCompiled ? "Compile" : "Validation");
        }
        if (CompileResult.Diagnostics.empty())
        {
            ImGui::TextDisabled("No diagnostics.");
            return;
        }
        for (std::size_t Index = 0; Index < CompileResult.Diagnostics.size(); ++Index)
        {
            const FGraphDiagnostic& Diagnostic = CompileResult.Diagnostics[Index];
            ImGui::PushID(static_cast<int>(Index));
            const ImVec4 Color = Diagnostic.Severity == EGraphDiagnosticSeverity::Error
                ? ImVec4(0.95f, 0.35f, 0.30f, 1.0f)
                : ImVec4(0.95f, 0.70f, 0.25f, 1.0f);
            const std::string Label = "[" + std::string(ToString(Diagnostic.Severity))
                + "] " + std::string(ToString(Diagnostic.Code));
            ImGui::PushStyleColor(ImGuiCol_Text, Color);
            if (ImGui::Selectable(Label.c_str(), false)
                && !Diagnostic.NodeId.empty())
                SelectedNodeId = Diagnostic.NodeId;
            ImGui::PopStyleColor();
            ImGui::TextWrapped("%s", Diagnostic.Message.c_str());
            ImGui::PopID();
        }
    }

    void DrawDetails()
    {
        auto Found = std::find_if(Graph.Nodes.begin(), Graph.Nodes.end(),
            [this](const FGraphNode& Node) { return Node.Id == SelectedNodeId; });
        if (Found == Graph.Nodes.end())
        {
            ImGui::TextDisabled("Select a node to inspect it.");
            ImGui::Separator();
            ImGui::Text("Graph Version: %d", Graph.Version);
            ImGui::TextWrapped("Graph ID: %s", Graph.GraphId.c_str());
            DrawDiagnostics();
            return;
        }
        ImGui::TextUnformatted(Found->DisplayName.c_str());
        ImGui::TextDisabled("Type: %s", Found->TypeName.c_str());
        ImGui::TextDisabled("Stable ID:");
        ImGui::TextWrapped("%s", Found->Id.c_str());
        ImGui::Separator();
        ImGui::Text("Position: %.0f, %.0f", Found->PositionX, Found->PositionY);
        ImGui::Text("Pins: %zu", Found->Pins.size());
        const FGraphNodeSchema* NodeSchema =
            GetDefaultGraphSchemaRegistry().Find(Found->TypeName);
        for (FGraphPin& Pin : Found->Pins)
        {
            ImGui::PushID(Pin.Id.c_str());
            ImGui::BulletText("%s %s (%s)", ToString(Pin.Direction).data(),
                Pin.Name.c_str(), ToString(Pin.Type).data());
            if (Pin.Type != EGraphValueType::Exec)
            {
                const FGraphPinSchema* PinSchema = nullptr;
                if (NodeSchema != nullptr)
                {
                    const auto Schema = std::find_if(
                        NodeSchema->Pins.begin(), NodeSchema->Pins.end(),
                        [&Pin](const FGraphPinSchema& Value)
                        {
                            return Value.Name == Pin.Name
                                && Value.Direction == Pin.Direction;
                        });
                    if (Schema != NodeSchema->Pins.end()) PinSchema = &*Schema;
                }
                const bool bComputedOutput = Pin.Direction == EGraphPinDirection::Output
                    && PinSchema != nullptr && PinSchema->DefaultValue.empty();
                if (bComputedOutput)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("computed");
                }
                else
                {
                    std::array<char, 192> Buffer {};
                    CopyToBuffer(Pin.DefaultValue, Buffer);
                    ImGui::SetNextItemWidth(-1.0f);
                    const bool bChanged = ImGui::InputText(
                        "##PinDefault", Buffer.data(), Buffer.size());
                    if (ImGui::IsItemActivated())
                    {
                        PinEditSnapshot = Graph;
                        bEditingPin = true;
                    }
                    if (bChanged)
                    {
                        Pin.DefaultValue = Buffer.data();
                        bDirty = true;
                        bHasCompileResult = false;
                    }
                    if (bEditingPin && ImGui::IsItemDeactivated())
                    {
                        bEditingPin = false;
                        const FGraphPin* BeforePin = FindGraphPin(PinEditSnapshot, Pin.Id);
                        if (BeforePin != nullptr && BeforePin->DefaultValue != Pin.DefaultValue)
                            Transactions.Record(std::move(PinEditSnapshot));
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("Delete Node"))
            DeleteSelectedNode();
        DrawDiagnostics();
    }

    void DrawEditor()
    {
        if (!bOpen) return;
        std::string Title = "PicoGraph - " + std::string(OpenedAsset.ToString());
        if (bDirty) Title += " *";
        Title += "###PicoGraphEditor";
        ImGuiWindowClass WindowClass;
        WindowClass.ClassId = static_cast<ImGuiID>(0x50475246u);
        WindowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        WindowClass.DockingAllowUnclassed = false;
        ImGui::SetNextWindowClass(&WindowClass);
        ImGui::SetNextWindowSize(ImVec2(1450.0f, 860.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(920.0f, 580.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (!ImGui::Begin(Title.c_str(), &bOpen,
                ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking))
        {
            bKeyboardFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            ImGui::End();
            return;
        }
        bKeyboardFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const ImGuiIO& IO = ImGui::GetIO();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
            if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
            if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (!IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                DeleteSelectedNode();
        }
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::MenuItem("Save", "Ctrl+S")) Save();
            ImGui::BeginDisabled(!Transactions.CanUndo());
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!Transactions.CanRedo());
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) Redo();
            ImGui::EndDisabled();
            ImGui::EndMenuBar();
        }
        if (ImGui::Button("Add Entry Event"))
        {
            const FPicoGraphAsset Before = Graph;
            Graph.Nodes.push_back(MakeEntryEventNode("CustomEvent", 100.0f, 300.0f));
            PushUndo(Before);
            SyncNodeZOrder();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Node")) ImGui::OpenPopup("AddGraphNode");
        if (ImGui::BeginPopup("AddGraphNode"))
        {
            for (const FGraphNodeSchema& Schema : GetDefaultGraphSchemaRegistry().GetSchemas())
            {
                if (Schema.Opcode == EGraphIROpcode::EntryEvent) continue;
                if (ImGui::MenuItem(Schema.DisplayName.c_str())) AddSchemaNode(Schema.TypeName);
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Validate")) Validate();
        ImGui::SameLine();
        if (ImGui::Button("Compile")) Compile();
        ImGui::SameLine();
        ImGui::TextDisabled("Middle-drag pans | click two compatible pins to connect");
        if (ImGui::BeginTable("PicoGraphLayout", 3, ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Variables", ImGuiTableColumnFlags_WidthFixed, 230.0f);
            ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 300.0f);
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("GraphVariables")) DrawVariables();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("GraphCanvasChild", ImVec2(0, 0), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
                DrawCanvas();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("GraphDetails")) DrawDetails();
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::End();
    }
};

FPicoGraphEditor::FPicoGraphEditor(
    FEngineLoop* EngineLoop,
    FStatus SetStatus,
    FAssetCreated AssetCreated)
    : Impl(std::make_unique<FImpl>())
{
    Impl->EngineLoop = EngineLoop;
    Impl->SetStatus = std::move(SetStatus);
    Impl->AssetCreated = std::move(AssetCreated);
    CopyToBuffer("/Game/Graphs", Impl->CreateFolder);
    CopyToBuffer("NewGameplayGraph", Impl->CreateName);
}

FPicoGraphEditor::~FPicoGraphEditor() = default;

void FPicoGraphEditor::OpenCreate()
{
    Impl->bCreateOpen = true;
}

void FPicoGraphEditor::OpenAsset(const FAssetPath& AssetPath)
{
    Impl->Open(AssetPath);
}

void FPicoGraphEditor::Draw()
{
    Impl->DrawCreatePopup();
    Impl->DrawVariablePopup();
    Impl->DrawEditor();
}

bool FPicoGraphEditor::IsOpen() const
{
    return Impl->bOpen;
}

bool FPicoGraphEditor::IsKeyboardFocused() const
{
    return Impl->bOpen && Impl->bKeyboardFocused;
}

FAssetPath FPicoGraphEditor::GetOpenedAsset() const
{
    return Impl->bOpen ? Impl->OpenedAsset : FAssetPath {};
}
}
