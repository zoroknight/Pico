#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
struct FAgentKnowledgeRecord
{
    std::string Id;
    std::string SourceType;
    std::string SourcePath;
    std::string Title;
    std::string Content;
    std::string ContentHash;
    std::uint64_t SourceRevision = 0;
    std::vector<std::string> Tags;
    std::string Provenance;
};

struct FAgentKnowledgeHit
{
    FAgentKnowledgeRecord Record;
    double Score = 0.0;
};

struct FAgentKnowledgeQuery
{
    std::string Text;
    std::size_t MaxResults = 8;
    std::size_t MaxContextBytes = 12000;
    std::vector<std::string> SourceTypes;
};

class FAgentKnowledgeStore
{
public:
    explicit FAgentKnowledgeStore(std::filesystem::path Directory = {});

    bool Load(std::string* OutError = nullptr);
    bool ReplaceSource(
        std::string_view SourceType,
        std::vector<FAgentKnowledgeRecord> Records,
        std::string* OutError = nullptr);
    std::vector<FAgentKnowledgeHit> Query(
        const FAgentKnowledgeQuery& Query) const;
    std::string BuildGroundingContextJson(
        const FAgentKnowledgeQuery& Query,
        std::vector<FAgentKnowledgeHit>* OutHits = nullptr) const;

    const std::filesystem::path& GetDirectory() const;
    std::filesystem::path GetIndexPath() const;
    std::filesystem::path GetAuditPath() const;
    std::size_t GetRecordCount() const;

private:
    bool SaveSnapshot(
        const std::vector<FAgentKnowledgeRecord>& Snapshot,
        std::string* OutError);
    bool AppendAudit(
        std::string_view Action,
        const FAgentKnowledgeRecord& Record,
        std::string* OutError);

    std::filesystem::path Directory;
    mutable std::mutex Mutex;
    std::vector<FAgentKnowledgeRecord> Records;
};

std::vector<FAgentKnowledgeRecord> CollectProjectTextKnowledge(
    const std::filesystem::path& ProjectRoot,
    std::size_t MaxFileBytes = 128 * 1024,
    std::size_t MaxFiles = 128);
}
