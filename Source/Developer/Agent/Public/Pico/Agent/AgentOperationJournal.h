#pragma once

#include "Pico/Agent/AgentTypes.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
enum class EAgentOperationState
{
    Prepared,
    Executing,
    Applied,
    Committed
};

struct FAgentOperationRecord
{
    std::string OperationId;
    std::string ToolName;
    std::string ArgumentsJson = "{}";
    EAgentOperationState State = EAgentOperationState::Prepared;
    std::optional<FAgentToolResult> Result;
    std::int64_t UpdatedAtMilliseconds = 0;
};

class FAgentOperationJournal
{
public:
    explicit FAgentOperationJournal(std::filesystem::path Directory = {});

    bool Prepare(const FAgentToolCall& Call, std::string* OutError = nullptr);
    bool MarkExecuting(const FAgentToolCall& Call, std::string* OutError = nullptr);
    bool MarkApplied(
        const FAgentToolCall& Call,
        const FAgentToolResult& Result,
        std::string* OutError = nullptr);
    bool MarkCommitted(const FAgentToolCall& Call, std::string* OutError = nullptr);

    std::optional<FAgentToolResult> FindApplied(
        const FAgentToolCall& Call,
        std::string* OutError = nullptr) const;
    std::vector<FAgentOperationRecord> ListIncomplete() const;

private:
    std::filesystem::path RecordPath(std::string_view OperationId) const;
    std::optional<FAgentOperationRecord> Load(
        const FAgentToolCall& Call,
        std::string* OutError) const;
    bool Save(const FAgentOperationRecord& Record, std::string* OutError) const;

    std::filesystem::path Directory;
    mutable std::mutex Mutex;
};

std::string_view ToString(EAgentOperationState State);
}
