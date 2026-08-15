param(
    [string]$ProjectFile = "Projects/PicoSandbox/PicoSandbox.pico",
    [string]$Target = "PicoSandboxGame",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$OutputRoot = "",
    [string]$StageName = "",
    [bool]$SmokeTest = $true
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$picoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$buildDir = Join-Path $picoRoot "Build"
$projectPath = (Resolve-Path (Join-Path $picoRoot $ProjectFile)).Path
$projectRoot = Split-Path -Parent $projectPath
if (-not $OutputRoot)
{
    $OutputRoot = Join-Path $projectRoot "Saved/StagedBuilds"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputRoot))
{
    $OutputRoot = Join-Path $picoRoot $OutputRoot
}

$cmake = (Get-Command cmake -ErrorAction Stop).Source
Write-Host "Configuring Pico package targets..."
& $cmake -S $picoRoot -B $buildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "Building $Target and PicoPackager ($Configuration)..."
& $cmake --build $buildDir --config $Configuration --target $Target PicoPackager --parallel
if ($LASTEXITCODE -ne 0) { throw "Package target build failed." }

$packager = Join-Path $buildDir "$Configuration/PicoPackager.exe"
$receipt = Join-Path $buildDir "$Configuration/$Target.targetreceipt"
$arguments = @(
    "-project=$projectPath",
    "-receipt=$receipt",
    "-output=$OutputRoot",
    "-engineroot=$picoRoot",
    "-profile=Development"
)
if ($StageName) { $arguments += "-stagename=$StageName" }
if ($SmokeTest) { $arguments += "-smoke" }

Write-Host "Staging project..."
& $packager @arguments
if ($LASTEXITCODE -ne 0) { throw "PicoPackager failed." }
