#include "Pico/Agent/AgentProjectHandoff.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <utility>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

constexpr int HandoffVersion = 1;

std::filesystem::path ManifestPath(const std::filesystem::path& ProjectRoot)
{
    return ProjectRoot / "Saved/Agent/ProjectHandoff.json";
}

bool Fail(std::string Message, std::string* OutError)
{
    if (OutError) *OutError = std::move(Message);
    return false;
}
}

bool PrepareAgentProjectHandoff(
    const FAgentProjectHandoff& Handoff,
    std::string* OutError)
{
    if (Handoff.TargetProjectFile.empty() || Handoff.SessionPath.empty()
        || Handoff.SessionId.empty() || Handoff.Provider.empty())
    {
        return Fail("Project handoff requires a target project, session, and provider",
            OutError);
    }
    if (!std::filesystem::is_regular_file(Handoff.TargetProjectFile))
        return Fail("Project handoff target descriptor does not exist", OutError);
    if (!std::filesystem::is_regular_file(Handoff.SessionPath))
        return Fail("Project handoff source session does not exist", OutError);
    const std::filesystem::path SessionIdPath(Handoff.SessionId);
    if (SessionIdPath.has_parent_path() || SessionIdPath.filename() != SessionIdPath)
        return Fail("Project handoff session id is unsafe", OutError);

    const std::filesystem::path ProjectRoot = Handoff.TargetProjectFile.parent_path();
    const std::filesystem::path SessionDirectory =
        ProjectRoot / "Saved/Agent/Sessions";
    const std::filesystem::path TargetSession =
        SessionDirectory / (Handoff.SessionId + ".jsonl");
    const std::filesystem::path Manifest = ManifestPath(ProjectRoot);
    const std::filesystem::path TemporaryManifest = Manifest.string() + ".tmp";
    std::error_code Error;
    std::filesystem::create_directories(SessionDirectory, Error);
    if (Error)
        return Fail("Could not create target Agent session directory: "
            + Error.message(), OutError);

    std::filesystem::copy_file(Handoff.SessionPath, TargetSession,
        std::filesystem::copy_options::overwrite_existing, Error);
    if (Error)
        return Fail("Could not copy Agent session into the new project: "
            + Error.message(), OutError);

    const FJson Json {{"version", HandoffVersion},
        {"source_project_file", Handoff.SourceProjectFile.string()},
        {"target_project_file", Handoff.TargetProjectFile.string()},
        {"session_id", Handoff.SessionId},
        {"session_file", TargetSession.filename().string()},
        {"provider", Handoff.Provider}, {"model", Handoff.Model},
        {"goal", Handoff.Goal}};
    {
        std::ofstream Stream(TemporaryManifest,
            std::ios::binary | std::ios::trunc);
        if (!Stream)
            return Fail("Could not write temporary project handoff manifest", OutError);
        Stream << Json.dump(2) << '\n';
        Stream.flush();
        if (!Stream)
            return Fail("Could not flush project handoff manifest", OutError);
    }
    std::filesystem::remove(Manifest, Error);
    Error.clear();
    std::filesystem::rename(TemporaryManifest, Manifest, Error);
    if (Error)
    {
        std::filesystem::remove(TemporaryManifest);
        return Fail("Could not publish project handoff manifest: " + Error.message(),
            OutError);
    }
    return true;
}

std::optional<FAgentProjectHandoff> ConsumeAgentProjectHandoff(
    const std::filesystem::path& ProjectRoot,
    std::string* OutError)
{
    const std::filesystem::path Manifest = ManifestPath(ProjectRoot);
    if (!std::filesystem::exists(Manifest)) return std::nullopt;
    try
    {
        std::ifstream Stream(Manifest, std::ios::binary);
        if (!Stream)
        {
            Fail("Could not open project handoff manifest", OutError);
            return std::nullopt;
        }
        const FJson Json = FJson::parse(Stream);
        Stream.close();
        if (Json.at("version").get<int>() != HandoffVersion)
        {
            Fail("Unsupported project handoff manifest version", OutError);
            return std::nullopt;
        }
        FAgentProjectHandoff Result;
        Result.SourceProjectFile = Json.value("source_project_file", "");
        Result.TargetProjectFile = Json.at("target_project_file").get<std::string>();
        Result.SessionId = Json.at("session_id").get<std::string>();
        Result.Provider = Json.at("provider").get<std::string>();
        Result.Model = Json.value("model", "");
        Result.Goal = Json.value("goal", "");
        const std::filesystem::path SessionFile =
            Json.at("session_file").get<std::string>();
        if (SessionFile.empty() || SessionFile.has_parent_path()
            || SessionFile.extension() != ".jsonl"
            || SessionFile.stem().string() != Result.SessionId)
        {
            Fail("Project handoff manifest contains an unsafe session filename",
                OutError);
            return std::nullopt;
        }
        Result.SessionPath = ProjectRoot / "Saved/Agent/Sessions"
            / SessionFile;
        if (Result.SessionId.empty() || Result.Provider.empty()
            || !std::filesystem::is_regular_file(Result.SessionPath))
        {
            Fail("Project handoff manifest references an invalid session", OutError);
            return std::nullopt;
        }
        std::error_code Error;
        std::filesystem::remove(Manifest, Error);
        if (Error)
        {
            Fail("Could not consume project handoff manifest: " + Error.message(),
                OutError);
            return std::nullopt;
        }
        return Result;
    }
    catch (const std::exception& Exception)
    {
        Fail("Could not parse project handoff manifest: "
            + std::string(Exception.what()), OutError);
        return std::nullopt;
    }
}
}
