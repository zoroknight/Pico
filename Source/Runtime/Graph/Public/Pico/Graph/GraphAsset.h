#pragma once

#include <cstdint>
#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
inline constexpr std::int32_t PicoGraphAssetVersion = 1;

enum class EGraphPinDirection
{
    Input,
    Output
};

enum class EGraphValueType
{
    Exec,
    Bool,
    Int,
    Float,
    String,
    Vector,
    Object
};

enum class EGraphAssetError
{
    None,
    InvalidArgument,
    InvalidData,
    DuplicateId,
    MissingPin,
    InvalidLink,
    UnsupportedVersion,
    FileReadFailed,
    FileWriteFailed,
    ParseFailed
};

struct FGraphPin
{
    std::string Id;
    std::string Name;
    EGraphPinDirection Direction = EGraphPinDirection::Input;
    EGraphValueType Type = EGraphValueType::Exec;
    std::string DefaultValue;
};

struct FGraphNode
{
    std::string Id;
    std::string TypeName;
    std::string DisplayName;
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    std::vector<FGraphPin> Pins;
};

struct FGraphLink
{
    std::string Id;
    std::string OutputPinId;
    std::string InputPinId;
};

struct FGraphVariable
{
    std::string Id;
    std::string Name;
    EGraphValueType Type = EGraphValueType::Float;
    std::string DefaultValue;
};

struct FPicoGraphAsset
{
    std::int32_t Version = PicoGraphAssetVersion;
    std::string GraphId;
    std::vector<FGraphVariable> Variables;
    std::vector<FGraphNode> Nodes;
    std::vector<FGraphLink> Links;
};

std::string CreateGraphStableId();
FGraphNode MakeEntryEventNode(std::string_view EventName, float X, float Y);
FGraphNode MakeGraphNode(std::string_view TypeName, std::string_view DisplayName, float X, float Y);

bool ValidateGraphAsset(
    const FPicoGraphAsset& Graph,
    EGraphAssetError* OutError = nullptr);
bool SaveGraphAssetToFile(
    const std::filesystem::path& FilePath,
    const FPicoGraphAsset& Graph,
    EGraphAssetError* OutError = nullptr);
bool LoadGraphAssetFromFile(
    const std::filesystem::path& FilePath,
    FPicoGraphAsset& OutGraph,
    EGraphAssetError* OutError = nullptr);

const FGraphPin* FindGraphPin(const FPicoGraphAsset& Graph, std::string_view PinId);
FGraphPin* FindGraphPin(FPicoGraphAsset& Graph, std::string_view PinId);
bool AddGraphLink(
    FPicoGraphAsset& Graph,
    std::string_view FirstPinId,
    std::string_view SecondPinId,
    EGraphAssetError* OutError = nullptr);
bool RemoveGraphNode(FPicoGraphAsset& Graph, std::string_view NodeId);

std::string_view ToString(EGraphPinDirection Direction);
std::string_view ToString(EGraphValueType Type);
std::string_view ToString(EGraphAssetError Error);

class FGraphTransactionHistory
{
public:
    explicit FGraphTransactionHistory(std::size_t MaxEntries = 64);

    void Record(FPicoGraphAsset Before);
    bool Undo(FPicoGraphAsset& InOutGraph);
    bool Redo(FPicoGraphAsset& InOutGraph);
    void Clear();
    bool CanUndo() const;
    bool CanRedo() const;

private:
    void Push(std::vector<FPicoGraphAsset>& Stack, FPicoGraphAsset Value);

    std::size_t MaxEntries = 64;
    std::vector<FPicoGraphAsset> UndoStack;
    std::vector<FPicoGraphAsset> RedoStack;
};
}
