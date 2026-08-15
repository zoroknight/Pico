#include "Pico/Packaging/Packaging.h"

#include "Pico/Core/CommandLine.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
void PrintUsage()
{
    std::cout
        << "Usage: PicoPackager -project=<project.pico> -receipt=<targetreceipt> "
           "-output=<directory> [-engineroot=<directory>] "
           "[-profile=Development|Shipping] [-stagename=<name>] [-smoke]\n";
}
}

int main(int Argc, char** Argv)
{
    Pico::FCommandLine::Init(Argc, Argv);
    if (Pico::FCommandLine::HasSwitch("help"))
    {
        PrintUsage();
        return 0;
    }
    const auto Project = Pico::FCommandLine::GetValue("project");
    const auto Receipt = Pico::FCommandLine::GetValue("receipt");
    const auto Output = Pico::FCommandLine::GetValue("output");
    if (!Project.has_value() || !Receipt.has_value() || !Output.has_value())
    {
        PrintUsage();
        return 1;
    }

    Pico::FPackageRequest Request;
    Request.ProjectFile = *Project;
    Request.TargetReceiptFile = *Receipt;
    Request.OutputRoot = *Output;
    Request.StageNameOverride = Pico::FCommandLine::GetValue("stagename")
        .value_or("");
    Request.EngineRoot = Pico::FCommandLine::GetValue("engineroot")
        .value_or(std::filesystem::current_path().string());
    Request.bRunSmokeTest = Pico::FCommandLine::HasSwitch("smoke");
    const std::string Profile = Pico::FCommandLine::GetValue("profile")
        .value_or("Development");
    if (Profile == "Development" || Profile == "development")
    {
        Request.Profile = Pico::EPackageProfile::Development;
    }
    else if (Profile == "Shipping" || Profile == "shipping")
    {
        Request.Profile = Pico::EPackageProfile::Shipping;
    }
    else
    {
        std::cerr << "Unknown package profile: " << Profile << '\n';
        return 1;
    }

    Pico::FPackageBuilder Builder;
    const Pico::FPackageResult Result = Builder.Build(Request);
    for (const std::string& Warning : Result.Warnings)
        std::cout << "Warning: " << Warning << '\n';
    for (const std::string& Error : Result.Errors)
        std::cerr << "Error: " << Error << '\n';
    std::cout << Result.Message << '\n';
    if (!Result.StageRoot.empty())
        std::cout << "Stage: " << Result.StageRoot.string() << '\n';
    std::cout << "Files: " << Result.Files.size() << '\n';
    return Result.bSucceeded ? 0 : 1;
}
