#include "TestRunner.h"

#include "Pico/Graph/GraphAsset.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
std::string ReadAll(const std::filesystem::path& FilePath)
{
    std::ifstream File(FilePath, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(File),
        std::istreambuf_iterator<char>());
}

Pico::FPicoGraphAsset MakeGraph()
{
    Pico::FPicoGraphAsset Graph;
    Graph.GraphId = Pico::CreateGraphStableId();
    Graph.Variables.push_back(Pico::FGraphVariable {
        Pico::CreateGraphStableId(), "DoorSpeed",
        Pico::EGraphValueType::Float, "120.0"});
    Graph.Nodes.push_back(Pico::MakeEntryEventNode("BeginPlay", 40.0f, 80.0f));
    Graph.Nodes.push_back(Pico::MakeGraphNode("Sequence", "Sequence", 300.0f, 80.0f));
    Pico::EGraphAssetError Error = Pico::EGraphAssetError::None;
    const bool bLinked = Pico::AddGraphLink(
        Graph,
        Graph.Nodes[0].Pins[0].Id,
        Graph.Nodes[1].Pins[0].Id,
        &Error);
    return bLinked ? Graph : Pico::FPicoGraphAsset {};
}
}

int main()
{
    FTestRunner Runner;
    Pico::FPicoGraphAsset Graph = MakeGraph();
    Pico::EGraphAssetError Error = Pico::EGraphAssetError::None;
    Runner.Expect(
        Pico::ValidateGraphAsset(Graph, &Error)
            && Graph.Nodes.size() == 2 && Graph.Links.size() == 1,
        "PicoGraph accepts stable node, pin, variable, and link identities");

    const std::filesystem::path First = std::filesystem::temp_directory_path()
        / "PicoGraphFirst.pgraph";
    const std::filesystem::path Second = std::filesystem::temp_directory_path()
        / "PicoGraphSecond.pgraph";
    Pico::FPicoGraphAsset Loaded;
    Runner.Expect(
        Pico::SaveGraphAssetToFile(First, Graph, &Error)
            && Pico::LoadGraphAssetFromFile(First, Loaded, &Error)
            && Pico::SaveGraphAssetToFile(Second, Loaded, &Error)
            && ReadAll(First) == ReadAll(Second)
            && Loaded.GraphId == Graph.GraphId
            && Loaded.Nodes[0].Id == Graph.Nodes[0].Id
            && Loaded.Nodes[0].Pins[0].Id == Graph.Nodes[0].Pins[0].Id,
        ".pgraph serialization is deterministic and preserves stable identities");

    Pico::FPicoGraphAsset Invalid = Graph;
    Invalid.Nodes[1].Id = Invalid.Nodes[0].Id;
    Runner.Expect(
        !Pico::ValidateGraphAsset(Invalid, &Error)
            && Error == Pico::EGraphAssetError::DuplicateId,
        "PicoGraph rejects duplicate stable identities");

    Invalid = Graph;
    Invalid.Links[0].InputPinId = "missing";
    Runner.Expect(
        !Pico::ValidateGraphAsset(Invalid, &Error)
            && Error == Pico::EGraphAssetError::MissingPin,
        "PicoGraph rejects links to missing pins");

    const std::string RemovedNodeId = Graph.Nodes[1].Id;
    Runner.Expect(
        Pico::RemoveGraphNode(Graph, RemovedNodeId)
            && Graph.Nodes.size() == 1 && Graph.Links.empty()
            && Pico::ValidateGraphAsset(Graph, &Error),
        "Removing a node also removes its links without invalidating the graph");

    Pico::FGraphTransactionHistory History(4);
    const Pico::FPicoGraphAsset Before = Graph;
    Graph.Nodes.push_back(Pico::MakeGraphNode("Branch", "Branch", 500.0f, 100.0f));
    History.Record(Before);
    const std::string AddedNodeId = Graph.Nodes.back().Id;
    Runner.Expect(
        History.CanUndo() && History.Undo(Graph)
            && Graph.Nodes.size() == 1 && History.CanRedo()
            && History.Redo(Graph) && Graph.Nodes.back().Id == AddedNodeId,
        "Graph transactions restore stable graph snapshots for Undo and Redo");

    std::error_code FileError;
    std::filesystem::remove(First, FileError);
    std::filesystem::remove(Second, FileError);
    return Runner.Finish();
}
