#include "Pico/Editor/EditorWorldDocument.h"

#include "Pico/Core/Paths.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/World.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace Pico
{
namespace
{
FEditorDocumentResult Success(std::string Message)
{
    return { true, std::move(Message) };
}

FEditorDocumentResult Failure(std::string Message)
{
    return { false, std::move(Message) };
}

bool IsWorldExtension(const std::filesystem::path& Path)
{
    std::string Extension = Path.extension().string();
    std::transform(
        Extension.begin(), Extension.end(), Extension.begin(),
        [](unsigned char Character)
        {
            return static_cast<char>(std::tolower(Character));
        });
    return Extension == ".pworld";
}
}

FEditorWorldDocument::FEditorWorldDocument(FEngineLoop* InEngineLoop)
    : EngineLoop(InEngineLoop)
{
    PWorld* World = EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
    bHasEmptyWorldData = World != nullptr && CaptureWorld(*World, EmptyWorldData);
}

FEditorDocumentResult FEditorWorldDocument::NewWorld()
{
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (EngineLoop == nullptr || !bHasEmptyWorldData
        || !EngineLoop->ReplaceWorld(EmptyWorldData, &Error))
    {
        return Failure("Could not create a new World: " + std::string(ToString(Error)));
    }
    CurrentAssetPath = {};
    CurrentFilePath.clear();
    bDirty = false;
    return Success("Created a new untitled World");
}

FEditorDocumentResult FEditorWorldDocument::Open(
    const std::filesystem::path& FilePath)
{
    FAssetPath AssetPath;
    if (!TryMakeAssetPath(FilePath, AssetPath))
    {
        return Failure("World files must be .pworld assets inside project Content");
    }

    std::filesystem::path ResolvedPath;
    if (!TryResolveFilePath(AssetPath, ResolvedPath))
    {
        return Failure("Could not resolve the selected World asset");
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    if (EngineLoop == nullptr || !EngineLoop->LoadWorld(ResolvedPath, &Error))
    {
        return Failure("Could not open World: " + std::string(ToString(Error)));
    }

    SetCurrentFile(std::move(AssetPath), std::move(ResolvedPath));
    return Success("Opened " + std::string(CurrentAssetPath.ToString()));
}

FEditorDocumentResult FEditorWorldDocument::Open(const FAssetPath& AssetPath)
{
    std::filesystem::path FilePath;
    if (!TryResolveFilePath(AssetPath, FilePath))
    {
        return Failure("Could not resolve World asset " + std::string(AssetPath.ToString()));
    }
    return Open(FilePath);
}

FEditorDocumentResult FEditorWorldDocument::Save()
{
    if (!HasAssetPath())
    {
        return Failure("This World has no file yet; use Save As");
    }

    PWorld* World = EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (World == nullptr || !SaveWorldToFile(CurrentFilePath, *World, &Error))
    {
        return Failure("Could not save World: " + std::string(ToString(Error)));
    }

    bDirty = false;
    RefreshAssetRegistry();
    return Success("Saved " + std::string(CurrentAssetPath.ToString()));
}

FEditorDocumentResult FEditorWorldDocument::SaveAs(
    const std::filesystem::path& FilePath)
{
    std::filesystem::path TargetPath = FilePath;
    if (TargetPath.extension().empty())
    {
        TargetPath += ".pworld";
    }

    FAssetPath AssetPath;
    if (!TryMakeAssetPath(TargetPath, AssetPath))
    {
        return Failure("World files must be saved as .pworld assets inside project Content");
    }

    std::filesystem::path ResolvedPath;
    if (!TryResolveFilePath(AssetPath, ResolvedPath))
    {
        return Failure("Could not resolve the selected World path");
    }

    std::error_code FileError;
    std::filesystem::create_directories(ResolvedPath.parent_path(), FileError);
    if (FileError)
    {
        return Failure("Could not create the World directory: " + FileError.message());
    }

    PWorld* World = EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (World == nullptr || !SaveWorldToFile(ResolvedPath, *World, &Error))
    {
        return Failure("Could not save World: " + std::string(ToString(Error)));
    }

    SetCurrentFile(std::move(AssetPath), std::move(ResolvedPath));
    RefreshAssetRegistry();
    return Success("Saved " + std::string(CurrentAssetPath.ToString()));
}

void FEditorWorldDocument::MarkDirty()
{
    bDirty = true;
}

bool FEditorWorldDocument::HasAssetPath() const
{
    return CurrentAssetPath.IsValid() && !CurrentFilePath.empty();
}

bool FEditorWorldDocument::IsDirty() const
{
    return bDirty;
}

const FAssetPath& FEditorWorldDocument::GetAssetPath() const
{
    return CurrentAssetPath;
}

const std::filesystem::path& FEditorWorldDocument::GetFilePath() const
{
    return CurrentFilePath;
}

std::string FEditorWorldDocument::GetDisplayName() const
{
    std::string Name = HasAssetPath()
        ? CurrentFilePath.filename().string()
        : "Untitled";
    if (bDirty)
    {
        Name += "*";
    }
    return Name;
}

bool FEditorWorldDocument::TryResolveFilePath(
    const FAssetPath& AssetPath,
    std::filesystem::path& OutFilePath)
{
    OutFilePath.clear();
    if (!AssetPath.IsValid()
        || !IsWorldExtension(std::filesystem::path(std::string(AssetPath.ToString()))))
    {
        return false;
    }
    return FPaths::TryGetProjectWritePath(
        EProjectWriteRoot::Content,
        std::filesystem::path(std::string(AssetPath.GetGameRelativePath())),
        OutFilePath);
}

bool FEditorWorldDocument::TryMakeAssetPath(
    const std::filesystem::path& FilePath,
    FAssetPath& OutAssetPath)
{
    OutAssetPath = {};
    if (!FPaths::HasProject() || FilePath.empty() || !IsWorldExtension(FilePath))
    {
        return false;
    }

    std::error_code Error;
    std::filesystem::path AbsolutePath = FilePath;
    if (AbsolutePath.is_relative())
    {
        AbsolutePath = std::filesystem::absolute(AbsolutePath, Error);
    }
    const std::filesystem::path ContentRoot =
        std::filesystem::weakly_canonical(FPaths::GetProjectContentDir(), Error);
    if (Error || ContentRoot.empty())
    {
        return false;
    }
    Error.clear();
    const std::filesystem::path NormalPath =
        std::filesystem::weakly_canonical(AbsolutePath, Error);
    const std::filesystem::path Candidate =
        Error || NormalPath.empty() ? AbsolutePath.lexically_normal() : NormalPath;
    const std::filesystem::path Relative = Candidate.lexically_relative(ContentRoot);
    if (Relative.empty() || Relative == "." || Relative.is_absolute()
        || *Relative.begin() == "..")
    {
        return false;
    }
    return FAssetPath::TryParse("/Game/" + Relative.generic_string(), OutAssetPath);
}

void FEditorWorldDocument::SetCurrentFile(
    FAssetPath AssetPath,
    std::filesystem::path FilePath)
{
    CurrentAssetPath = std::move(AssetPath);
    CurrentFilePath = std::move(FilePath);
    bDirty = false;
}

void FEditorWorldDocument::RefreshAssetRegistry()
{
    if (EngineLoop != nullptr)
    {
        EngineLoop->GetAssetRegistry().ScanProjectContent();
    }
}
}
