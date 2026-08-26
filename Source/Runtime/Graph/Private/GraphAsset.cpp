#include "Pico/Graph/GraphAsset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace Pico
{
namespace
{
using FJson = nlohmann::ordered_json;

void Report(EGraphAssetError* OutError, EGraphAssetError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

bool TryParseDirection(std::string_view Text, EGraphPinDirection& OutDirection)
{
    if (Text == "Input") OutDirection = EGraphPinDirection::Input;
    else if (Text == "Output") OutDirection = EGraphPinDirection::Output;
    else return false;
    return true;
}

bool TryParseType(std::string_view Text, EGraphValueType& OutType)
{
    if (Text == "Exec") OutType = EGraphValueType::Exec;
    else if (Text == "Bool") OutType = EGraphValueType::Bool;
    else if (Text == "Int") OutType = EGraphValueType::Int;
    else if (Text == "Float") OutType = EGraphValueType::Float;
    else if (Text == "String") OutType = EGraphValueType::String;
    else if (Text == "Vector") OutType = EGraphValueType::Vector;
    else if (Text == "Object") OutType = EGraphValueType::Object;
    else return false;
    return true;
}

FJson PinToJson(const FGraphPin& Pin)
{
    return FJson {
        {"id", Pin.Id},
        {"name", Pin.Name},
        {"direction", ToString(Pin.Direction)},
        {"type", ToString(Pin.Type)},
        {"default", Pin.DefaultValue}
    };
}

bool PinFromJson(const FJson& Json, FGraphPin& OutPin)
{
    if (!Json.is_object()
        || !Json.contains("id") || !Json["id"].is_string()
        || !Json.contains("name") || !Json["name"].is_string()
        || !Json.contains("direction") || !Json["direction"].is_string()
        || !Json.contains("type") || !Json["type"].is_string())
        return false;
    FGraphPin Pin;
    Pin.Id = Json["id"].get<std::string>();
    Pin.Name = Json["name"].get<std::string>();
    if (!TryParseDirection(Json["direction"].get<std::string>(), Pin.Direction)
        || !TryParseType(Json["type"].get<std::string>(), Pin.Type))
        return false;
    if (Json.contains("default") && Json["default"].is_string())
        Pin.DefaultValue = Json["default"].get<std::string>();
    OutPin = std::move(Pin);
    return true;
}
}

std::string CreateGraphStableId()
{
    static std::atomic<std::uint64_t> Counter {1};
    const auto Time = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const std::uint64_t Count = Counter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream Stream;
    Stream << std::hex << std::setfill('0')
        << std::setw(16) << Time << std::setw(16) << Count;
    return Stream.str();
}

FGraphNode MakeEntryEventNode(std::string_view EventName, float X, float Y)
{
    FGraphNode Node;
    Node.Id = CreateGraphStableId();
    Node.TypeName = "EntryEvent";
    Node.DisplayName = EventName.empty() ? "BeginPlay" : std::string(EventName);
    Node.PositionX = X;
    Node.PositionY = Y;
    Node.Pins.push_back(FGraphPin {
        CreateGraphStableId(), "Then", EGraphPinDirection::Output,
        EGraphValueType::Exec, {}});
    return Node;
}

FGraphNode MakeGraphNode(
    std::string_view TypeName,
    std::string_view DisplayName,
    float X,
    float Y)
{
    FGraphNode Node;
    Node.Id = CreateGraphStableId();
    Node.TypeName = TypeName.empty() ? "Node" : std::string(TypeName);
    Node.DisplayName = DisplayName.empty() ? Node.TypeName : std::string(DisplayName);
    Node.PositionX = X;
    Node.PositionY = Y;
    Node.Pins.push_back(FGraphPin {
        CreateGraphStableId(), "In", EGraphPinDirection::Input,
        EGraphValueType::Exec, {}});
    Node.Pins.push_back(FGraphPin {
        CreateGraphStableId(), "Then", EGraphPinDirection::Output,
        EGraphValueType::Exec, {}});
    return Node;
}

const FGraphPin* FindGraphPin(const FPicoGraphAsset& Graph, std::string_view PinId)
{
    for (const FGraphNode& Node : Graph.Nodes)
        for (const FGraphPin& Pin : Node.Pins)
            if (Pin.Id == PinId) return &Pin;
    return nullptr;
}

FGraphPin* FindGraphPin(FPicoGraphAsset& Graph, std::string_view PinId)
{
    for (FGraphNode& Node : Graph.Nodes)
        for (FGraphPin& Pin : Node.Pins)
            if (Pin.Id == PinId) return &Pin;
    return nullptr;
}

bool ValidateGraphAsset(const FPicoGraphAsset& Graph, EGraphAssetError* OutError)
{
    Report(OutError, EGraphAssetError::None);
    if (Graph.Version != PicoGraphAssetVersion)
    {
        Report(OutError, EGraphAssetError::UnsupportedVersion);
        return false;
    }
    if (Graph.GraphId.empty())
    {
        Report(OutError, EGraphAssetError::InvalidData);
        return false;
    }
    std::unordered_set<std::string> Ids;
    auto AddId = [&Ids](const std::string& Id)
    {
        return !Id.empty() && Ids.insert(Id).second;
    };
    if (!AddId(Graph.GraphId))
    {
        Report(OutError, EGraphAssetError::DuplicateId);
        return false;
    }
    for (const FGraphVariable& Variable : Graph.Variables)
    {
        if (Variable.Name.empty() || Variable.Type == EGraphValueType::Exec
            || !AddId(Variable.Id))
        {
            Report(OutError, Variable.Id.empty()
                ? EGraphAssetError::InvalidData : EGraphAssetError::DuplicateId);
            return false;
        }
    }
    for (const FGraphNode& Node : Graph.Nodes)
    {
        if (Node.TypeName.empty() || Node.DisplayName.empty() || !AddId(Node.Id))
        {
            Report(OutError, Node.Id.empty()
                ? EGraphAssetError::InvalidData : EGraphAssetError::DuplicateId);
            return false;
        }
        for (const FGraphPin& Pin : Node.Pins)
        {
            if (Pin.Name.empty() || !AddId(Pin.Id))
            {
                Report(OutError, Pin.Id.empty()
                    ? EGraphAssetError::InvalidData : EGraphAssetError::DuplicateId);
                return false;
            }
        }
    }
    std::unordered_set<std::string> InputPins;
    for (const FGraphLink& Link : Graph.Links)
    {
        if (!AddId(Link.Id))
        {
            Report(OutError, EGraphAssetError::DuplicateId);
            return false;
        }
        const FGraphPin* Output = FindGraphPin(Graph, Link.OutputPinId);
        const FGraphPin* Input = FindGraphPin(Graph, Link.InputPinId);
        if (Output == nullptr || Input == nullptr)
        {
            Report(OutError, EGraphAssetError::MissingPin);
            return false;
        }
        if (Output->Direction != EGraphPinDirection::Output
            || Input->Direction != EGraphPinDirection::Input
            || Output->Type != Input->Type
            || !InputPins.insert(Input->Id).second)
        {
            Report(OutError, EGraphAssetError::InvalidLink);
            return false;
        }
    }
    return true;
}

bool AddGraphLink(
    FPicoGraphAsset& Graph,
    std::string_view FirstPinId,
    std::string_view SecondPinId,
    EGraphAssetError* OutError)
{
    Report(OutError, EGraphAssetError::None);
    FGraphPin* First = FindGraphPin(Graph, FirstPinId);
    FGraphPin* Second = FindGraphPin(Graph, SecondPinId);
    if (First == nullptr || Second == nullptr)
    {
        Report(OutError, EGraphAssetError::MissingPin);
        return false;
    }
    FGraphPin* Output = First->Direction == EGraphPinDirection::Output ? First : Second;
    FGraphPin* Input = First->Direction == EGraphPinDirection::Input ? First : Second;
    if (Output == Input || Output->Direction != EGraphPinDirection::Output
        || Input->Direction != EGraphPinDirection::Input
        || Output->Type != Input->Type)
    {
        Report(OutError, EGraphAssetError::InvalidLink);
        return false;
    }
    Graph.Links.erase(
        std::remove_if(Graph.Links.begin(), Graph.Links.end(),
            [Input](const FGraphLink& Link) { return Link.InputPinId == Input->Id; }),
        Graph.Links.end());
    Graph.Links.push_back(FGraphLink {
        CreateGraphStableId(), Output->Id, Input->Id});
    return true;
}

bool RemoveGraphNode(FPicoGraphAsset& Graph, std::string_view NodeId)
{
    const auto Node = std::find_if(Graph.Nodes.begin(), Graph.Nodes.end(),
        [NodeId](const FGraphNode& Value) { return Value.Id == NodeId; });
    if (Node == Graph.Nodes.end()) return false;
    std::unordered_set<std::string> Pins;
    for (const FGraphPin& Pin : Node->Pins) Pins.insert(Pin.Id);
    Graph.Links.erase(
        std::remove_if(Graph.Links.begin(), Graph.Links.end(),
            [&Pins](const FGraphLink& Link)
            {
                return Pins.contains(Link.OutputPinId) || Pins.contains(Link.InputPinId);
            }),
        Graph.Links.end());
    Graph.Nodes.erase(Node);
    return true;
}

bool SaveGraphAssetToFile(
    const std::filesystem::path& FilePath,
    const FPicoGraphAsset& Graph,
    EGraphAssetError* OutError)
{
    if (FilePath.empty())
    {
        Report(OutError, EGraphAssetError::InvalidArgument);
        return false;
    }
    if (!ValidateGraphAsset(Graph, OutError)) return false;
    FJson Root;
    Root["format"] = "PicoGraph";
    Root["version"] = Graph.Version;
    Root["graph_id"] = Graph.GraphId;
    Root["variables"] = FJson::array();
    for (const FGraphVariable& Variable : Graph.Variables)
    {
        Root["variables"].push_back(FJson {
            {"id", Variable.Id}, {"name", Variable.Name},
            {"type", ToString(Variable.Type)}, {"default", Variable.DefaultValue}});
    }
    Root["nodes"] = FJson::array();
    for (const FGraphNode& Node : Graph.Nodes)
    {
        FJson JsonNode {
            {"id", Node.Id}, {"type", Node.TypeName}, {"title", Node.DisplayName},
            {"position", FJson::array({Node.PositionX, Node.PositionY})},
            {"pins", FJson::array()}};
        for (const FGraphPin& Pin : Node.Pins) JsonNode["pins"].push_back(PinToJson(Pin));
        Root["nodes"].push_back(std::move(JsonNode));
    }
    Root["links"] = FJson::array();
    for (const FGraphLink& Link : Graph.Links)
        Root["links"].push_back(FJson {
            {"id", Link.Id}, {"output", Link.OutputPinId}, {"input", Link.InputPinId}});

    std::error_code FileError;
    std::filesystem::create_directories(FilePath.parent_path(), FileError);
    std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
    if (FileError || !File)
    {
        Report(OutError, EGraphAssetError::FileWriteFailed);
        return false;
    }
    const std::string Text = Root.dump(2) + "\n";
    File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
    if (!File)
    {
        Report(OutError, EGraphAssetError::FileWriteFailed);
        return false;
    }
    Report(OutError, EGraphAssetError::None);
    return true;
}

bool LoadGraphAssetFromFile(
    const std::filesystem::path& FilePath,
    FPicoGraphAsset& OutGraph,
    EGraphAssetError* OutError)
{
    Report(OutError, EGraphAssetError::None);
    std::ifstream File(FilePath, std::ios::binary);
    if (!File)
    {
        Report(OutError, EGraphAssetError::FileReadFailed);
        return false;
    }
    FJson Root;
    try { File >> Root; }
    catch (const nlohmann::json::exception&)
    {
        Report(OutError, EGraphAssetError::ParseFailed);
        return false;
    }
    if (!Root.is_object() || Root.value("format", "") != "PicoGraph"
        || !Root.contains("version") || !Root["version"].is_number_integer()
        || !Root.contains("graph_id") || !Root["graph_id"].is_string()
        || !Root.contains("variables") || !Root["variables"].is_array()
        || !Root.contains("nodes") || !Root["nodes"].is_array()
        || !Root.contains("links") || !Root["links"].is_array())
    {
        Report(OutError, EGraphAssetError::InvalidData);
        return false;
    }
    FPicoGraphAsset Graph;
    Graph.Version = Root["version"].get<std::int32_t>();
    Graph.GraphId = Root["graph_id"].get<std::string>();
    if (Graph.Version != PicoGraphAssetVersion)
    {
        Report(OutError, EGraphAssetError::UnsupportedVersion);
        return false;
    }
    try
    {
        for (const FJson& JsonVariable : Root["variables"])
        {
            FGraphVariable Variable;
            if (!JsonVariable.is_object() || !JsonVariable.contains("id")
                || !JsonVariable["id"].is_string() || !JsonVariable.contains("name")
                || !JsonVariable["name"].is_string() || !JsonVariable.contains("type")
                || !JsonVariable["type"].is_string()
                || !TryParseType(JsonVariable["type"].get<std::string>(), Variable.Type))
                throw std::runtime_error("invalid variable");
            Variable.Id = JsonVariable["id"].get<std::string>();
            Variable.Name = JsonVariable["name"].get<std::string>();
            Variable.DefaultValue = JsonVariable.value("default", "");
            Graph.Variables.push_back(std::move(Variable));
        }
        for (const FJson& JsonNode : Root["nodes"])
        {
            if (!JsonNode.is_object() || !JsonNode.contains("id")
                || !JsonNode["id"].is_string() || !JsonNode.contains("type")
                || !JsonNode["type"].is_string() || !JsonNode.contains("title")
                || !JsonNode["title"].is_string() || !JsonNode.contains("position")
                || !JsonNode["position"].is_array() || JsonNode["position"].size() != 2
                || !JsonNode.contains("pins") || !JsonNode["pins"].is_array())
                throw std::runtime_error("invalid node");
            FGraphNode Node;
            Node.Id = JsonNode["id"].get<std::string>();
            Node.TypeName = JsonNode["type"].get<std::string>();
            Node.DisplayName = JsonNode["title"].get<std::string>();
            Node.PositionX = JsonNode["position"][0].get<float>();
            Node.PositionY = JsonNode["position"][1].get<float>();
            for (const FJson& JsonPin : JsonNode["pins"])
            {
                FGraphPin Pin;
                if (!PinFromJson(JsonPin, Pin)) throw std::runtime_error("invalid pin");
                Node.Pins.push_back(std::move(Pin));
            }
            Graph.Nodes.push_back(std::move(Node));
        }
        for (const FJson& JsonLink : Root["links"])
        {
            if (!JsonLink.is_object() || !JsonLink.contains("id")
                || !JsonLink["id"].is_string() || !JsonLink.contains("output")
                || !JsonLink["output"].is_string() || !JsonLink.contains("input")
                || !JsonLink["input"].is_string())
                throw std::runtime_error("invalid link");
            Graph.Links.push_back(FGraphLink {
                JsonLink["id"].get<std::string>(),
                JsonLink["output"].get<std::string>(),
                JsonLink["input"].get<std::string>()});
        }
    }
    catch (const std::exception&)
    {
        Report(OutError, EGraphAssetError::InvalidData);
        return false;
    }
    if (!ValidateGraphAsset(Graph, OutError)) return false;
    OutGraph = std::move(Graph);
    return true;
}

std::string_view ToString(EGraphPinDirection Direction)
{
    return Direction == EGraphPinDirection::Input ? "Input" : "Output";
}

std::string_view ToString(EGraphValueType Type)
{
    switch (Type)
    {
    case EGraphValueType::Exec: return "Exec";
    case EGraphValueType::Bool: return "Bool";
    case EGraphValueType::Int: return "Int";
    case EGraphValueType::Float: return "Float";
    case EGraphValueType::String: return "String";
    case EGraphValueType::Vector: return "Vector";
    case EGraphValueType::Object: return "Object";
    }
    return "Unknown";
}

std::string_view ToString(EGraphAssetError Error)
{
    switch (Error)
    {
    case EGraphAssetError::None: return "None";
    case EGraphAssetError::InvalidArgument: return "InvalidArgument";
    case EGraphAssetError::InvalidData: return "InvalidData";
    case EGraphAssetError::DuplicateId: return "DuplicateId";
    case EGraphAssetError::MissingPin: return "MissingPin";
    case EGraphAssetError::InvalidLink: return "InvalidLink";
    case EGraphAssetError::UnsupportedVersion: return "UnsupportedVersion";
    case EGraphAssetError::FileReadFailed: return "FileReadFailed";
    case EGraphAssetError::FileWriteFailed: return "FileWriteFailed";
    case EGraphAssetError::ParseFailed: return "ParseFailed";
    }
    return "Unknown";
}

FGraphTransactionHistory::FGraphTransactionHistory(std::size_t InMaxEntries)
    : MaxEntries(std::max<std::size_t>(InMaxEntries, 1))
{
}

void FGraphTransactionHistory::Record(FPicoGraphAsset Before)
{
    Push(UndoStack, std::move(Before));
    RedoStack.clear();
}

bool FGraphTransactionHistory::Undo(FPicoGraphAsset& InOutGraph)
{
    if (UndoStack.empty()) return false;
    Push(RedoStack, std::move(InOutGraph));
    InOutGraph = std::move(UndoStack.back());
    UndoStack.pop_back();
    return true;
}

bool FGraphTransactionHistory::Redo(FPicoGraphAsset& InOutGraph)
{
    if (RedoStack.empty()) return false;
    Push(UndoStack, std::move(InOutGraph));
    InOutGraph = std::move(RedoStack.back());
    RedoStack.pop_back();
    return true;
}

void FGraphTransactionHistory::Clear()
{
    UndoStack.clear();
    RedoStack.clear();
}

bool FGraphTransactionHistory::CanUndo() const
{
    return !UndoStack.empty();
}

bool FGraphTransactionHistory::CanRedo() const
{
    return !RedoStack.empty();
}

void FGraphTransactionHistory::Push(
    std::vector<FPicoGraphAsset>& Stack,
    FPicoGraphAsset Value)
{
    if (Stack.size() >= MaxEntries) Stack.erase(Stack.begin());
    Stack.push_back(std::move(Value));
}
}
