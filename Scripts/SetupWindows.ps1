param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Preparing Pico from a clean Windows checkout..."
& (Join-Path $scriptDir "CheckEnvironment.ps1")

& (Join-Path $scriptDir "BuildWindows.ps1") `
    -Configuration $Configuration `
    -RunTests

Write-Host ""
Write-Host "Pico is ready."
Write-Host "Launch: $scriptDir\..\Build\$Configuration\PicoLaunch.exe"
Write-Host "Demo:   $scriptDir\..\Build\$Configuration\PicoReflectionDemo.exe"
Write-Host "Editor: $scriptDir\..\Build\$Configuration\PicoInspector.exe"
