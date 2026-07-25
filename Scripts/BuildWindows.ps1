param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

    [switch]$RunTests
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$picoRoot = Resolve-Path (Join-Path $scriptDir "..")
$buildDir = Join-Path $picoRoot "Build"
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

function Find-CMake
{
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command)
    {
        return $command.Source
    }

    $standaloneCMake = "${env:ProgramFiles}\CMake\bin\cmake.exe"
    if (Test-Path $standaloneCMake)
    {
        return $standaloneCMake
    }

    if (Test-Path $vsWhere)
    {
        $vsPath = & $vsWhere `
            -latest `
            -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($vsPath)
        {
            $visualStudioCMake =
                Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $visualStudioCMake)
            {
                return $visualStudioCMake
            }
        }
    }

    return $null
}

$cmakePath = Find-CMake
if (-not $cmakePath)
{
    throw "CMake was not found. Run Scripts\CheckEnvironment.ps1 for setup guidance."
}

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) "ctest.exe"
if ($RunTests -and -not (Test-Path $ctestPath))
{
    throw "CTest was not found next to CMake: $ctestPath"
}

Write-Host "Configuring Pico with Visual Studio 2022 (x64)..."
& $cmakePath `
    -S $picoRoot `
    -B $buildDir `
    -G "Visual Studio 17 2022" `
    -A x64
if ($LASTEXITCODE -ne 0)
{
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building Pico ($Configuration)..."
& $cmakePath --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0)
{
    throw "Pico build failed with exit code $LASTEXITCODE."
}

if ($RunTests)
{
    Write-Host "Running Pico tests ($Configuration)..."
    & $ctestPath `
        --test-dir $buildDir `
        -C $Configuration `
        --output-on-failure
    if ($LASTEXITCODE -ne 0)
    {
        throw "Pico tests failed with exit code $LASTEXITCODE."
    }
}

Write-Host "Pico $Configuration build completed successfully."
