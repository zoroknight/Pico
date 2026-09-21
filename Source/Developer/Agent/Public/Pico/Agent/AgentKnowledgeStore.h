#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Pico
{
enum class EAgentKnowledgeKind
{
    Semantic,
    Episode,
    Procedure,
    Entity
};

std::string_view ToString(EAgentKnowledgeKind Kind);
bool TryParseAgentKnowledgeKind(
    std::string_view Text,
    EAgentKnowledgeKind& OutKind);

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
    std::string ParentId;
    std::size_t ChunkIndex = 0;
    std::size_t ChunkCount = 1;
    std::vector<std::string> EntityIds;
    std::string RevisionDomain;
    std::unordered_map<std::string, std::string> Fields;
    EAgentKnowledgeKind Kind = EAgentKnowledgeKind::Semantic;
};

struct FAgentKnowledgeHit
{
    FAgentKnowledgeRecord Record;
    double Score = 0.0;
    double ExactScore = 0.0;
    double Bm25Score = 0.0;
    double EntityScore = 0.0;
    bool bFromRewrite = false;
    std::vector<std::string> MatchedFields;
};

struct FAgentKnowledgeQuery
{
    std::string Text;
    std::size_t MaxResults = 8;
    std::size_t MaxContextBytes = 12000;
    std::vector<std::string> SourceTypes;
    std::vector<std::string> ExactIdentifiers;
    std::vector<std::string> EntityIds;
    std::unordered_map<std::string, std::uint64_t> Revisions;
    std::vector<EAgentKnowledgeKind> Kinds;
    bool bEnableBm25 = true;
    bool bEnableQueryRewrite = true;
    bool bExcludeStaleRevisions = true;
    bool bEnableSummaryCompression = true;
    double LowConfidenceThreshold = 0.35;
    std::size_t CompressionThresholdBytes = 8 * 1024;
    std::size_t SummaryMaxBytes = 768;
};

struct FAgentKnowledgeQueryResult
{
    std::string OriginalQuery;
    std::vector<std::string> EffectiveQueries;
    std::vector<FAgentKnowledgeHit> Hits;
    double Confidence = 0.0;
    bool bRewriteApplied = false;
    bool bCompressionApplied = false;
    std::size_t CompressedRecordCount = 0;
    std::size_t OriginalEvidenceBytes = 0;
    std::size_t FinalEvidenceBytes = 0;
    std::size_t StaleRecordsExcluded = 0;
};

struct FAgentKnowledgeViewStats
{
    std::array<std::size_t, 4> ActiveByKind {};
    std::array<std::size_t, 4> StaleByKind {};
    std::size_t EntityCount = 0;
};

std::string RewriteAgentKnowledgeQueryDeterministically(
    std::string_view Query);

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
    FAgentKnowledgeQueryResult QueryDetailed(
        const FAgentKnowledgeQuery& Query) const;
    std::string BuildGroundingContextJson(
        const FAgentKnowledgeQuery& Query,
        std::vector<FAgentKnowledgeHit>* OutHits = nullptr,
        FAgentKnowledgeQueryResult* OutResult = nullptr) const;

    const std::filesystem::path& GetDirectory() const;
    std::filesystem::path GetIndexPath() const;
    std::filesystem::path GetAuditPath() const;
    std::size_t GetRecordCount() const;
    std::vector<FAgentKnowledgeRecord> GetKindView(
        EAgentKnowledgeKind Kind,
        std::size_t MaxRecords = 16,
        const std::unordered_map<std::string, std::uint64_t>& Revisions = {}) const;
    FAgentKnowledgeViewStats GetViewStats() const;
    std::string BuildMemoryViewsJson(
        std::size_t MaxRecordsPerKind = 4) const;

private:
    bool SaveSnapshot(
        const std::vector<FAgentKnowledgeRecord>& Snapshot,
        std::string* OutError);
    bool AppendAudit(
        std::string_view Action,
        const FAgentKnowledgeRecord& Record,
        std::string* OutError);
    void RebuildIndices();

    struct FIndexedRecord
    {
        std::unordered_map<std::string, std::size_t> TermFrequency;
        std::size_t Length = 0;
    };

    std::filesystem::path Directory;
    mutable std::mutex Mutex;
    std::vector<FAgentKnowledgeRecord> Records;
    std::unordered_map<std::string, std::vector<std::size_t>> ExactIndex;
    std::unordered_map<std::string, std::vector<std::size_t>> EntityIndex;
    std::unordered_map<std::string,
        std::unordered_map<std::uint64_t, std::vector<std::size_t>>> RevisionIndex;
    std::unordered_map<std::string, std::uint64_t> LatestRevisionByDomain;
    std::unordered_map<std::string, std::size_t> DocumentFrequency;
    std::vector<FIndexedRecord> IndexedRecords;
    double AverageDocumentLength = 0.0;
};

std::vector<FAgentKnowledgeRecord> CollectProjectTextKnowledge(
    const std::filesystem::path& ProjectRoot,
    std::size_t MaxFileBytes = 128 * 1024,
    std::size_t MaxFiles = 128);
}
