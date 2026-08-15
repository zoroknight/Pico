#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Pico
{
enum class EPackageTargetType
{
    Game,
    Client,
    Server
};

enum class EPackageProfile
{
    Development,
    Shipping
};

struct FRuntimeDependency
{
    std::filesystem::path Source;
    std::filesystem::path Destination;
    std::string Reason;
};

struct FTargetReceipt
{
    std::string Name;
    EPackageTargetType Type = EPackageTargetType::Game;
    std::string Platform = "Windows";
    std::string Configuration = "Release";
    std::string EngineVersion;
    std::filesystem::path Executable;
    std::vector<FRuntimeDependency> RuntimeDependencies;
};

struct FPackageRequest
{
    std::filesystem::path EngineRoot;
    std::filesystem::path ProjectFile;
    std::filesystem::path TargetReceiptFile;
    std::filesystem::path OutputRoot;
    std::string StageNameOverride;
    EPackageProfile Profile = EPackageProfile::Development;
    bool bRunSmokeTest = false;
};

struct FPackageInputFile
{
    std::filesystem::path Source;
    std::filesystem::path Destination;
    std::string Reason;
};

struct FPackageFileRecord
{
    std::filesystem::path Destination;
    std::string Reason;
    std::uintmax_t Size = 0;
};

struct FPackageResult
{
    bool bSucceeded = false;
    std::filesystem::path StageRoot;
    std::string Message;
    std::vector<FPackageFileRecord> Files;
    std::vector<std::string> Warnings;
    std::vector<std::string> Errors;
};

struct FPackageContext
{
    std::filesystem::path EngineRoot;
    std::filesystem::path ProjectRoot;
    std::filesystem::path ProjectFile;
    std::filesystem::path ReceiptDirectory;
    std::string ProjectName;
    std::string EngineVersion;
    FTargetReceipt Receipt;
    EPackageProfile Profile = EPackageProfile::Development;
};

class IPackageContributor
{
public:
    virtual ~IPackageContributor() = default;
    virtual bool Collect(
        const FPackageContext& Context,
        std::vector<FPackageInputFile>& OutFiles,
        std::vector<std::string>& OutErrors) const = 0;
};

class FPackageBuilder
{
public:
    FPackageBuilder();
    void AddContributor(std::unique_ptr<IPackageContributor> Contributor);
    FPackageResult Build(const FPackageRequest& Request) const;

private:
    std::vector<std::unique_ptr<IPackageContributor>> Contributors;
};

bool LoadTargetReceipt(
    const std::filesystem::path& FilePath,
    FTargetReceipt& OutReceipt,
    std::string* OutError = nullptr);

std::string_view ToString(EPackageTargetType Type);
std::string_view ToString(EPackageProfile Profile);
}
