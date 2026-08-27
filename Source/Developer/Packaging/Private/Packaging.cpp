#include "Pico/Packaging/Packaging.h"

#include "Pico/Core/Config.h"
#include "Pico/Core/PlatformProcess.h"
#include "Pico/Graph/GraphCompiler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <set>
#include <system_error>
#include <thread>
#include <unordered_map>

namespace Pico
{
namespace
{
constexpr int PackageLayoutVersion = 1;

std::string ToLower(std::string Value)
{
    std::transform(
        Value.begin(), Value.end(), Value.begin(),
        [](unsigned char Character)
        {
            return static_cast<char>(std::tolower(Character));
        });
    return Value;
}

bool IsValidStageName(std::string_view Name)
{
    if (Name.empty() || Name == "." || Name == "..") return false;
    return std::ranges::all_of(
        Name,
        [](unsigned char Character)
        {
            return std::isalnum(Character) || Character == '_' || Character == '-';
        });
}

std::filesystem::path NormalizePath(const std::filesystem::path& Path)
{
    if (Path.empty()) return {};
    std::error_code Error;
    std::filesystem::path Absolute = Path;
    if (Absolute.is_relative()) Absolute = std::filesystem::absolute(Absolute, Error);
    if (Error) return {};
    const std::filesystem::path Canonical =
        std::filesystem::weakly_canonical(Absolute, Error);
    return Error ? Absolute.lexically_normal() : Canonical;
}

bool IsSafeRelativePath(const std::filesystem::path& Path)
{
    if (Path.empty() || Path.is_absolute()) return false;
    for (const std::filesystem::path& Part : Path)
    {
        if (Part == "..") return false;
    }
    return true;
}

bool IsWithin(
    const std::filesystem::path& Candidate,
    const std::filesystem::path& Root)
{
    const std::filesystem::path NormalCandidate = NormalizePath(Candidate);
    const std::filesystem::path NormalRoot = NormalizePath(Root);
    if (NormalCandidate.empty() || NormalRoot.empty()) return false;
    auto CandidateIt = NormalCandidate.begin();
    for (auto RootIt = NormalRoot.begin(); RootIt != NormalRoot.end(); ++RootIt)
    {
        if (CandidateIt == NormalCandidate.end()) return false;
#if defined(_WIN32)
        if (ToLower(CandidateIt->string()) != ToLower(RootIt->string())) return false;
#else
        if (*CandidateIt != *RootIt) return false;
#endif
        ++CandidateIt;
    }
    return true;
}

bool AddDirectory(
    const std::filesystem::path& SourceRoot,
    const std::filesystem::path& DestinationRoot,
    std::string Reason,
    const std::set<std::string>* Extensions,
    const std::set<std::string>* ExcludedDirectories,
    std::vector<FPackageInputFile>& OutFiles,
    std::vector<std::string>& OutErrors)
{
    std::error_code Error;
    if (!std::filesystem::is_directory(SourceRoot, Error))
    {
        OutErrors.push_back("Required directory does not exist: " + SourceRoot.string());
        return false;
    }
    std::filesystem::recursive_directory_iterator Iterator(
        SourceRoot,
        std::filesystem::directory_options::skip_permission_denied,
        Error);
    const std::filesystem::recursive_directory_iterator End;
    if (Error)
    {
        OutErrors.push_back("Could not scan directory: " + SourceRoot.string());
        return false;
    }
    while (Iterator != End)
    {
        const std::filesystem::directory_entry Entry = *Iterator;
        const std::filesystem::file_status Status = Entry.symlink_status(Error);
        if (Error)
        {
            OutErrors.push_back("Could not inspect package input: " + Entry.path().string());
            return false;
        }
        if (std::filesystem::is_symlink(Status))
        {
            if (Entry.is_directory(Error)) Iterator.disable_recursion_pending();
            Error.clear();
        }
        else if (std::filesystem::is_regular_file(Status))
        {
            std::filesystem::path Relative =
                std::filesystem::relative(Entry.path(), SourceRoot, Error);
            if (Error || !IsSafeRelativePath(Relative))
            {
                OutErrors.push_back("Could not make package-relative path for "
                    + Entry.path().string());
                return false;
            }
            const std::string Extension = ToLower(Entry.path().extension().string());
            if (Extensions == nullptr || Extensions->contains(Extension))
            {
                OutFiles.push_back(
                    {Entry.path(), DestinationRoot / Relative, Reason});
            }
        }
        else if (std::filesystem::is_directory(Status)
            && ExcludedDirectories != nullptr
            && ExcludedDirectories->contains(
                ToLower(Entry.path().filename().string())))
        {
            Iterator.disable_recursion_pending();
        }
        Iterator.increment(Error);
        if (Error)
        {
            OutErrors.push_back("Could not continue scanning " + SourceRoot.string());
            return false;
        }
    }
    return true;
}

class FEngineConfigContributor final : public IPackageContributor
{
public:
    bool Collect(
        const FPackageContext& Context,
        std::vector<FPackageInputFile>& OutFiles,
        std::vector<std::string>& OutErrors) const override
    {
        return AddDirectory(
            Context.EngineRoot / "Config", "Engine/Config", "EngineConfig",
            nullptr, nullptr, OutFiles, OutErrors);
    }
};

class FProjectContributor final : public IPackageContributor
{
public:
    bool Collect(
        const FPackageContext& Context,
        std::vector<FPackageInputFile>& OutFiles,
        std::vector<std::string>& OutErrors) const override
    {
        OutFiles.push_back({
            Context.ProjectFile,
            std::filesystem::path(Context.ProjectName)
                / Context.ProjectFile.filename(),
            "ProjectDescriptor"});
        return AddDirectory(
            Context.ProjectRoot / "Config",
            std::filesystem::path(Context.ProjectName) / "Config",
            "ProjectConfig", nullptr, nullptr, OutFiles, OutErrors);
    }
};

class FNativeAssetContributor final : public IPackageContributor
{
public:
    bool Collect(
        const FPackageContext& Context,
        std::vector<FPackageInputFile>& OutFiles,
        std::vector<std::string>& OutErrors) const override
    {
        static const std::set<std::string> NativeExtensions {
            ".pworld", ".pmesh", ".ptex", ".pmat", ".pskeleton",
            ".pskeletalmesh", ".panimation", ".panimset", ".pmontage",
            ".pcharprofile", ".pcontrolprofile", ".pblueprint", ".pgraph"
        };
        static const std::set<std::string> ExcludedDirectories {
            "source", "saved", "intermediate"
        };
        return AddDirectory(
            Context.ProjectRoot / "Content",
            std::filesystem::path(Context.ProjectName) / "Content",
            "NativeAsset", &NativeExtensions, &ExcludedDirectories,
            OutFiles, OutErrors);
    }
};

class FTargetContributor final : public IPackageContributor
{
public:
    bool Collect(
        const FPackageContext& Context,
        std::vector<FPackageInputFile>& OutFiles,
        std::vector<std::string>& OutErrors) const override
    {
        const std::filesystem::path Executable = Context.Receipt.Executable.is_absolute()
            ? Context.Receipt.Executable
            : Context.ReceiptDirectory / Context.Receipt.Executable;
        if (!std::filesystem::is_regular_file(Executable))
        {
            OutErrors.push_back("Target executable does not exist: " + Executable.string());
            return false;
        }
        OutFiles.push_back(
            {Executable, std::filesystem::path("Binaries") / Executable.filename(),
             "TargetExecutable"});
        for (const FRuntimeDependency& Dependency : Context.Receipt.RuntimeDependencies)
        {
            const std::filesystem::path Source = Dependency.Source.is_absolute()
                ? Dependency.Source : Context.ReceiptDirectory / Dependency.Source;
            OutFiles.push_back({Source, Dependency.Destination,
                Dependency.Reason.empty() ? "RuntimeDependency" : Dependency.Reason});
        }
        return true;
    }
};

bool CopyInputs(
    const std::vector<FPackageInputFile>& Inputs,
    const std::filesystem::path& StageRoot,
    std::vector<FPackageFileRecord>& OutRecords,
    std::vector<std::string>& OutErrors)
{
    std::unordered_map<std::string, std::filesystem::path> Destinations;
    for (const FPackageInputFile& Input : Inputs)
    {
        if (!IsSafeRelativePath(Input.Destination))
        {
            OutErrors.push_back("Unsafe Stage destination: " + Input.Destination.string());
            return false;
        }
        std::error_code Error;
        if (!std::filesystem::is_regular_file(Input.Source, Error)
            || std::filesystem::is_symlink(std::filesystem::symlink_status(Input.Source, Error)))
        {
            OutErrors.push_back("Package input is missing or unsafe: " + Input.Source.string());
            return false;
        }
        const std::string DestinationKey = ToLower(Input.Destination.generic_string());
        const auto [Found, bInserted] = Destinations.emplace(DestinationKey, Input.Source);
        if (!bInserted && NormalizePath(Found->second) != NormalizePath(Input.Source))
        {
            OutErrors.push_back("Multiple package inputs target "
                + Input.Destination.generic_string());
            return false;
        }
        if (!bInserted) continue;
        const std::filesystem::path Destination = StageRoot / Input.Destination;
        std::filesystem::create_directories(Destination.parent_path(), Error);
        if (Error)
        {
            OutErrors.push_back("Could not create Stage directory: "
                + Destination.parent_path().string());
            return false;
        }
        std::filesystem::copy_file(
            Input.Source, Destination,
            std::filesystem::copy_options::overwrite_existing, Error);
        if (Error)
        {
            OutErrors.push_back("Could not copy " + Input.Source.string()
                + " to " + Destination.string());
            return false;
        }
        const std::uintmax_t Size = std::filesystem::file_size(Destination, Error);
        OutRecords.push_back(
            {Input.Destination, Input.Reason, Error ? 0 : Size});
    }
    std::sort(
        OutRecords.begin(), OutRecords.end(),
        [](const FPackageFileRecord& Left, const FPackageFileRecord& Right)
        {
            return Left.Destination.generic_string() < Right.Destination.generic_string();
        });
    return true;
}

bool WriteGeneratedFiles(
    const FPackageContext& Context,
    const std::filesystem::path& StageRoot,
    std::vector<FPackageFileRecord>& OutRecords,
    std::vector<std::string>& OutErrors)
{
    FConfigFile EngineMarker;
    EngineMarker.SetString("Engine", "Name", "Pico");
    EngineMarker.SetString("Engine", "Version", Context.EngineVersion);
    EngineMarker.SetString("Engine", "LayoutVersion",
        std::to_string(PackageLayoutVersion));
    if (!EngineMarker.Save(StageRoot / "Engine/PicoEngine.root"))
    {
        OutErrors.push_back("Could not write Engine/PicoEngine.root");
        return false;
    }
    FConfigFile StageManifest;
    StageManifest.SetString("Stage", "LayoutVersion",
        std::to_string(PackageLayoutVersion));
    StageManifest.SetString("Stage", "EngineVersion", Context.EngineVersion);
    StageManifest.SetString("Stage", "ProjectName", Context.ProjectName);
    StageManifest.SetString("Stage", "EngineRelativePath", "Engine");
    StageManifest.SetString(
        "Stage", "ProjectRelativePath",
        (std::filesystem::path(Context.ProjectName)
            / Context.ProjectFile.filename()).generic_string());
    StageManifest.SetString("Stage", "TargetName", Context.Receipt.Name);
    StageManifest.SetString("Stage", "TargetType",
        std::string(ToString(Context.Receipt.Type)));
    StageManifest.SetString("Stage", "Profile",
        std::string(ToString(Context.Profile)));
    if (!StageManifest.Save(StageRoot / "PicoStage.manifest"))
    {
        OutErrors.push_back("Could not write PicoStage.manifest");
        return false;
    }
    OutRecords.push_back({"Engine/PicoEngine.root", "GeneratedEngineMarker", 0});
    OutRecords.push_back({"PicoStage.manifest", "GeneratedStageManifest", 0});
    return true;
}

bool CookStageGraphs(
    const FPackageContext& Context,
    const std::filesystem::path& StageRoot,
    std::vector<FPackageFileRecord>& InOutRecords,
    std::vector<std::string>& OutErrors)
{
    const std::filesystem::path ContentRoot =
        StageRoot / Context.ProjectName / "Content";
    std::error_code Error;
    if (!std::filesystem::is_directory(ContentRoot, Error)) return true;
    std::vector<std::filesystem::path> Sources;
    for (std::filesystem::recursive_directory_iterator It(ContentRoot, Error), End;
         !Error && It != End; It.increment(Error))
    {
        if (It->is_regular_file(Error)
            && ToLower(It->path().extension().string()) == ".pgraph")
            Sources.push_back(It->path());
    }
    if (Error)
    {
        OutErrors.push_back("Could not scan staged Graph assets: " + Error.message());
        return false;
    }
    std::sort(Sources.begin(), Sources.end());
    for (const std::filesystem::path& Source : Sources)
    {
        const std::filesystem::path Cooked = Source.string() + ".pgrb";
        std::string CookError;
        if (!CookGraphAsset(Source, Cooked, &CookError))
        {
            OutErrors.push_back("Graph Cook failed for " + Source.string() + ": " + CookError);
            return false;
        }
        std::filesystem::remove(Source, Error);
        if (Error)
        {
            OutErrors.push_back("Could not remove editor Graph source from Stage: " + Error.message());
            return false;
        }
        const std::filesystem::path Relative = std::filesystem::relative(Cooked, StageRoot, Error);
        if (Error)
        {
            OutErrors.push_back("Could not record cooked Graph output");
            return false;
        }
        InOutRecords.erase(std::remove_if(InOutRecords.begin(), InOutRecords.end(),
            [&Source, &StageRoot](const FPackageFileRecord& Record)
            {
                return (StageRoot / Record.Destination).lexically_normal()
                    == Source.lexically_normal();
            }), InOutRecords.end());
        InOutRecords.push_back({Relative, "CookedPicoGraph",
            std::filesystem::file_size(Cooked, Error)});
        if (Error)
        {
            OutErrors.push_back("Could not inspect cooked Graph output");
            return false;
        }
    }
    return true;
}

bool ValidateConfigAsset(
    const FConfigFile& Config,
    std::string_view Key,
    const FPackageContext& Context,
    const std::filesystem::path& StageRoot,
    std::vector<std::string>& OutErrors)
{
    const std::string Asset = Config.GetString("Game", Key, "");
    if (Asset.empty()) return true;
    constexpr std::string_view Prefix = "/Game/";
    if (!Asset.starts_with(Prefix))
    {
        OutErrors.push_back("Game." + std::string(Key)
            + " is not a /Game asset path: " + Asset);
        return false;
    }
    const std::filesystem::path Relative(Asset.substr(Prefix.size()));
    if (!IsSafeRelativePath(Relative)
        || !std::filesystem::is_regular_file(
            StageRoot / Context.ProjectName / "Content" / Relative))
    {
        OutErrors.push_back("Packaged asset is missing for Game."
            + std::string(Key) + ": " + Asset);
        return false;
    }
    return true;
}

bool ValidateStage(
    const FPackageContext& Context,
    const std::filesystem::path& StageRoot,
    std::vector<std::string>& OutErrors)
{
    const std::array<std::filesystem::path, 5> Required {
        "PicoStage.manifest",
        "Engine/PicoEngine.root",
        "Engine/Config/Pico.ini",
        std::filesystem::path(Context.ProjectName) / Context.ProjectFile.filename(),
        std::filesystem::path("Binaries") / Context.Receipt.Executable.filename()
    };
    for (const std::filesystem::path& Relative : Required)
    {
        if (!std::filesystem::is_regular_file(StageRoot / Relative))
        {
            OutErrors.push_back("Required Stage file is missing: " + Relative.generic_string());
        }
    }
    std::error_code Error;
    for (std::filesystem::recursive_directory_iterator Iterator(StageRoot, Error), End;
         !Error && Iterator != End; Iterator.increment(Error))
    {
        const std::filesystem::directory_entry Entry = *Iterator;
        if (Entry.is_symlink(Error))
        {
            OutErrors.push_back("Stage contains a symbolic link: " + Entry.path().string());
            break;
        }
        const std::filesystem::path Relative =
            std::filesystem::relative(Entry.path(), StageRoot, Error);
        for (const std::filesystem::path& Part : Relative)
        {
            const std::string Lower = ToLower(Part.string());
            if (Lower == "source" || Lower == "saved" || Lower == "intermediate")
            {
                OutErrors.push_back("Stage contains forbidden directory: "
                    + Relative.generic_string());
                break;
            }
        }
    }
    if (Error) OutErrors.push_back("Could not validate every Stage file");

    FConfigFile ProjectConfig;
    const std::filesystem::path ConfigPath =
        StageRoot / Context.ProjectName / "Config/Pico.ini";
    if (!ProjectConfig.Load(ConfigPath))
    {
        OutErrors.push_back("Packaged project Config/Pico.ini could not be loaded");
    }
    else
    {
        ValidateConfigAsset(ProjectConfig, "DefaultMap", Context, StageRoot, OutErrors);
        ValidateConfigAsset(ProjectConfig, "DefaultPawnProfile", Context, StageRoot, OutErrors);
    }
    return OutErrors.empty();
}

bool WriteReport(
    const FPackageContext& Context,
    const std::filesystem::path& StageRoot,
    const std::vector<FPackageFileRecord>& Records,
    bool bSucceeded,
    const std::vector<std::string>& Errors)
{
    FConfigFile Report;
    Report.SetString("Package", "Project", Context.ProjectName);
    Report.SetString("Package", "Target", Context.Receipt.Name);
    Report.SetString("Package", "TargetType", std::string(ToString(Context.Receipt.Type)));
    Report.SetString("Package", "Platform", Context.Receipt.Platform);
    Report.SetString("Package", "BuildConfiguration", Context.Receipt.Configuration);
    Report.SetString("Package", "Profile", std::string(ToString(Context.Profile)));
    Report.SetString("Package", "Succeeded", bSucceeded ? "true" : "false");
    Report.SetString("Package", "FileCount", std::to_string(Records.size()));
    Report.SetString("Package", "ErrorCount", std::to_string(Errors.size()));
    for (std::size_t Index = 0; Index < Records.size(); ++Index)
    {
        const std::string Section = "File" + std::to_string(Index);
        Report.SetString(Section, "Destination", Records[Index].Destination.generic_string());
        Report.SetString(Section, "Reason", Records[Index].Reason);
        Report.SetString(Section, "Size", std::to_string(Records[Index].Size));
    }
    for (std::size_t Index = 0; Index < Errors.size(); ++Index)
    {
        Report.SetString("Errors", "Error" + std::to_string(Index), Errors[Index]);
    }
    return Report.Save(StageRoot / "PackageReport.ini");
}

bool WriteCompletionMarker(
    const FPackageContext& Context,
    const std::filesystem::path& StageRoot,
    std::size_t FileCount)
{
    FConfigFile Marker;
    Marker.SetString("Package", "State", "Complete");
    Marker.SetString("Package", "Project", Context.ProjectName);
    Marker.SetString("Package", "Target", Context.Receipt.Name);
    Marker.SetString("Package", "Profile", std::string(ToString(Context.Profile)));
    Marker.SetString("Package", "FileCount", std::to_string(FileCount));
    return Marker.Save(StageRoot / "PicoPackage.complete");
}

bool RunSmokeTest(
    const FPackageContext& Context,
    const std::filesystem::path& StageRoot,
    std::vector<std::string>& OutErrors)
{
    const std::filesystem::path Executable =
        StageRoot / "Binaries" / Context.Receipt.Executable.filename();
    const std::filesystem::path LogFile = StageRoot / "PackageSmokeTest.log";
    std::string Error;
    FProcessHandle Process = FPlatformProcess::CreateProcess(
        Executable, {"-frames=2"}, std::filesystem::temp_directory_path(),
        LogFile, &Error);
    if (!Process.IsValid())
    {
        OutErrors.push_back("Could not start packaged Runtime: " + Error);
        return false;
    }
    int ExitCode = 0;
    if (!FPlatformProcess::WaitForExit(Process, 30000, &ExitCode))
    {
        FPlatformProcess::Terminate(Process);
        OutErrors.push_back("Packaged Runtime did not exit within 30 seconds");
        return false;
    }
    Process.Reset();
    if (ExitCode != 0)
    {
        std::string LastLine;
        std::ifstream Log(LogFile);
        for (std::string Line; std::getline(Log, Line);)
        {
            if (!Line.empty()) LastLine = std::move(Line);
        }
        OutErrors.push_back("Packaged Runtime exited with code "
            + std::to_string(ExitCode)
            + (LastLine.empty() ? "" : ": " + LastLine));
        return false;
    }
    std::error_code RemoveError;
    std::filesystem::remove(LogFile, RemoveError);
    return true;
}

bool RenameWithRetry(
    const std::filesystem::path& Source,
    const std::filesystem::path& Destination,
    std::error_code& OutError)
{
    constexpr int MaxAttempts = 8;
    for (int Attempt = 0; Attempt < MaxAttempts; ++Attempt)
    {
        OutError.clear();
        std::filesystem::rename(Source, Destination, OutError);
        if (!OutError)
        {
            return true;
        }
        if (Attempt + 1 < MaxAttempts)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
        }
    }
    return false;
}

std::string DescribeFilesystemError(const std::error_code& Error)
{
    if (Error == std::errc::permission_denied)
    {
        return "access denied (close the packaged game and check output directory ownership)";
    }
    return Error.message() + " (code " + std::to_string(Error.value()) + ")";
}

std::filesystem::path MakeWorkingStagePath(
    const std::filesystem::path& OutputRoot,
    const std::string& StageName)
{
    const auto Nonce = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return OutputRoot / (".PicoStaging-" + StageName + "-" + std::to_string(Nonce));
}

bool CommitStage(
    const std::filesystem::path& Temporary,
    const std::filesystem::path& Final,
    std::vector<std::string>& OutErrors)
{
    const std::filesystem::path Backup = Temporary.string() + ".backup";
    std::error_code Error;
    std::filesystem::remove_all(Backup, Error);
    if (Error)
    {
        OutErrors.push_back(
            "Could not remove the previous Stage backup: "
                + DescribeFilesystemError(Error));
        return false;
    }
    const bool bHadFinal = std::filesystem::exists(Final, Error);
    if (Error)
    {
        OutErrors.push_back(
            "Could not inspect the previous Stage: "
                + DescribeFilesystemError(Error));
        return false;
    }
    if (bHadFinal)
    {
        if (!RenameWithRetry(Final, Backup, Error))
        {
            OutErrors.push_back(
                "Could not preserve the previous successful Stage: "
                    + DescribeFilesystemError(Error));
            return false;
        }
    }
    if (!RenameWithRetry(Temporary, Final, Error))
    {
        if (bHadFinal)
        {
            std::error_code RestoreError;
            RenameWithRetry(Backup, Final, RestoreError);
        }
        OutErrors.push_back(
            "Could not commit the completed Stage: "
                + DescribeFilesystemError(Error));
        return false;
    }
    std::filesystem::remove_all(Backup, Error);
    return true;
}

std::optional<EPackageTargetType> ParseTargetType(std::string Value)
{
    Value = ToLower(std::move(Value));
    if (Value == "game") return EPackageTargetType::Game;
    if (Value == "client") return EPackageTargetType::Client;
    if (Value == "server") return EPackageTargetType::Server;
    return std::nullopt;
}
}

FPackageBuilder::FPackageBuilder()
{
    AddContributor(std::make_unique<FEngineConfigContributor>());
    AddContributor(std::make_unique<FProjectContributor>());
    AddContributor(std::make_unique<FNativeAssetContributor>());
    AddContributor(std::make_unique<FTargetContributor>());
}

void FPackageBuilder::AddContributor(std::unique_ptr<IPackageContributor> Contributor)
{
    if (Contributor != nullptr) Contributors.push_back(std::move(Contributor));
}

FPackageResult FPackageBuilder::Build(const FPackageRequest& Request) const
{
    FPackageResult Result;
    FPackageContext Context;
    Context.EngineRoot = NormalizePath(Request.EngineRoot);
    Context.ProjectFile = NormalizePath(Request.ProjectFile);
    Context.ProjectRoot = Context.ProjectFile.parent_path();
    Context.ReceiptDirectory = NormalizePath(Request.TargetReceiptFile).parent_path();
    Context.Profile = Request.Profile;
    if (Request.Profile == EPackageProfile::Shipping)
    {
        Result.Errors.push_back(
            "Shipping profile is not implemented; use Development packaging");
    }
    if (Context.EngineRoot.empty()
        || !std::filesystem::is_regular_file(Context.EngineRoot / "Config/Pico.ini"))
    {
        Result.Errors.push_back("Engine root does not contain Config/Pico.ini");
    }
    FConfigFile ProjectDescriptor;
    if (!ProjectDescriptor.Load(Context.ProjectFile))
    {
        Result.Errors.push_back("Could not load project descriptor");
    }
    else
    {
        Context.ProjectName = ProjectDescriptor.GetString("Project", "Name", "");
        Context.EngineVersion = ProjectDescriptor.GetString("Project", "EngineVersion", "");
        if (Context.ProjectName.empty() || Context.EngineVersion.empty())
            Result.Errors.push_back("Project descriptor identity is incomplete");
    }
    std::string ReceiptError;
    if (!LoadTargetReceipt(Request.TargetReceiptFile, Context.Receipt, &ReceiptError))
        Result.Errors.push_back(ReceiptError);
    else if (!Context.EngineVersion.empty()
        && Context.Receipt.EngineVersion != Context.EngineVersion)
        Result.Errors.push_back("Target Receipt EngineVersion does not match the project");
    const std::filesystem::path OutputRoot = NormalizePath(Request.OutputRoot);
    if (OutputRoot.empty()) Result.Errors.push_back("Package output root is empty");
    if (!Result.Errors.empty())
    {
        Result.Message = "Package preflight failed";
        return Result;
    }

    const std::string DefaultStageName = Context.ProjectName + "-"
        + Context.Receipt.Platform + "-" + std::string(ToString(Context.Profile));
    const std::string StageName = Request.StageNameOverride.empty()
        ? DefaultStageName : Request.StageNameOverride;
    if (!IsValidStageName(StageName))
    {
        Result.Errors.push_back(
            "Package name must contain only letters, digits, '_' or '-'");
        Result.Message = "Package preflight failed";
        return Result;
    }
    const std::filesystem::path FinalStage = OutputRoot / StageName;
    const std::filesystem::path TemporaryStage =
        MakeWorkingStagePath(OutputRoot, StageName);
    Result.StageRoot = TemporaryStage;
    if (!IsWithin(FinalStage, OutputRoot) || !IsWithin(TemporaryStage, OutputRoot))
    {
        Result.Errors.push_back("Computed Stage path escapes the output root");
        Result.Message = "Package preflight failed";
        return Result;
    }
    std::error_code Error;
    std::filesystem::create_directories(OutputRoot, Error);
    if (Error)
    {
        Result.Errors.push_back("Could not create package output root");
        Result.Message = "Package preflight failed";
        return Result;
    }
    std::filesystem::remove_all(TemporaryStage, Error);
    Error.clear();
    std::filesystem::create_directories(TemporaryStage, Error);
    if (Error)
    {
        Result.Errors.push_back("Could not create temporary Stage");
        Result.Message = "Package preflight failed";
        return Result;
    }

    std::vector<FPackageInputFile> Inputs;
    bool bCollected = true;
    for (const std::unique_ptr<IPackageContributor>& Contributor : Contributors)
    {
        bCollected = Contributor->Collect(Context, Inputs, Result.Errors) && bCollected;
    }
    if (bCollected)
    {
        std::sort(
            Inputs.begin(), Inputs.end(),
            [](const FPackageInputFile& Left, const FPackageInputFile& Right)
            {
                return Left.Destination.generic_string()
                    < Right.Destination.generic_string();
            });
        bCollected = CopyInputs(
            Inputs, TemporaryStage, Result.Files, Result.Errors);
    }
    if (bCollected)
        bCollected = WriteGeneratedFiles(
            Context, TemporaryStage, Result.Files, Result.Errors);
    if (bCollected)
        bCollected = CookStageGraphs(
            Context, TemporaryStage, Result.Files, Result.Errors);
    if (bCollected)
        bCollected = ValidateStage(Context, TemporaryStage, Result.Errors);
    if (bCollected && Request.bRunSmokeTest)
        bCollected = RunSmokeTest(Context, TemporaryStage, Result.Errors);
    if (!WriteReport(
            Context, TemporaryStage, Result.Files, bCollected, Result.Errors))
    {
        Result.Errors.push_back("Could not write PackageReport.ini");
        bCollected = false;
    }
    if (bCollected
        && !WriteCompletionMarker(Context, TemporaryStage, Result.Files.size()))
    {
        Result.Errors.push_back("Could not write package completion marker");
        bCollected = false;
    }
    if (!bCollected)
    {
        std::filesystem::remove_all(TemporaryStage, Error);
        Result.StageRoot = FinalStage;
        Result.Message = "Package failed; the previous successful Stage was preserved";
        return Result;
    }
    if (!CommitStage(TemporaryStage, FinalStage, Result.Errors))
    {
        std::filesystem::remove_all(TemporaryStage, Error);
        Result.StageRoot = FinalStage;
        Result.Message = "Package completed but could not replace the previous Stage";
        return Result;
    }
    Result.bSucceeded = true;
    Result.StageRoot = FinalStage;
    Result.Message = "Packaged " + Context.ProjectName + " to " + FinalStage.string();
    return Result;
}

bool LoadTargetReceipt(
    const std::filesystem::path& FilePath,
    FTargetReceipt& OutReceipt,
    std::string* OutError)
{
    if (OutError != nullptr) OutError->clear();
    FConfigFile Receipt;
    if (!Receipt.Load(FilePath))
    {
        if (OutError != nullptr) *OutError = "Could not load Target Receipt: " + FilePath.string();
        return false;
    }
    FTargetReceipt Parsed;
    Parsed.Name = Receipt.GetString("Target", "Name", "");
    Parsed.Platform = Receipt.GetString("Target", "Platform", "");
    Parsed.Configuration = Receipt.GetString("Target", "Configuration", "");
    Parsed.EngineVersion = Receipt.GetString("Target", "EngineVersion", "");
    Parsed.Executable = Receipt.GetString("Target", "Executable", "");
    const std::optional<EPackageTargetType> Type = ParseTargetType(
        Receipt.GetString("Target", "Type", ""));
    if (Parsed.Name.empty() || Parsed.Platform.empty()
        || Parsed.Configuration.empty() || Parsed.EngineVersion.empty()
        || Parsed.Executable.empty() || !Type.has_value())
    {
        if (OutError != nullptr) *OutError = "Target Receipt is incomplete: " + FilePath.string();
        return false;
    }
    Parsed.Type = *Type;
    std::vector<std::pair<std::string, std::string>> Entries =
        Receipt.GetSectionEntries("RuntimeDependencies");
    std::sort(Entries.begin(), Entries.end());
    for (const auto& [Key, Value] : Entries)
    {
        (void)Key;
        const std::size_t First = Value.find('|');
        const std::size_t Second = First == std::string::npos
            ? std::string::npos : Value.find('|', First + 1);
        if (First == std::string::npos)
        {
            if (OutError != nullptr) *OutError = "Invalid RuntimeDependency in " + FilePath.string();
            return false;
        }
        FRuntimeDependency Dependency;
        Dependency.Source = Value.substr(0, First);
        Dependency.Destination = Second == std::string::npos
            ? Value.substr(First + 1)
            : Value.substr(First + 1, Second - First - 1);
        Dependency.Reason = Second == std::string::npos
            ? "RuntimeDependency" : Value.substr(Second + 1);
        if (Dependency.Source.empty() || !IsSafeRelativePath(Dependency.Destination))
        {
            if (OutError != nullptr) *OutError = "Unsafe RuntimeDependency in " + FilePath.string();
            return false;
        }
        Parsed.RuntimeDependencies.push_back(std::move(Dependency));
    }
    OutReceipt = std::move(Parsed);
    return true;
}

std::string_view ToString(EPackageTargetType Type)
{
    switch (Type)
    {
    case EPackageTargetType::Game: return "Game";
    case EPackageTargetType::Client: return "Client";
    case EPackageTargetType::Server: return "Server";
    }
    return "Unknown";
}

std::string_view ToString(EPackageProfile Profile)
{
    switch (Profile)
    {
    case EPackageProfile::Development: return "Development";
    case EPackageProfile::Shipping: return "Shipping";
    }
    return "Unknown";
}
}
