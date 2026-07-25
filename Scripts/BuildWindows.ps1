$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$picoRoot = Resolve-Path (Join-Path $scriptDir "..")
$buildDir = Join-Path $picoRoot "Build"

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }

if (-not $cmakePath) {
    $candidates = @(
        "${env:ProgramFiles}\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate.EndsWith("vswhere.exe") -and (Test-Path $candidate)) {
            $vsPath = & $candidate -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
            if (-not $vsPath) {
                $vsPath = & $candidate -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
            }

            if (-not $vsPath) {
                continue
            }

            $vsCmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $vsCmake) {
                $cmakePath = $vsCmake
                break
            }
        } elseif (Test-Path $candidate) {
            $cmakePath = $candidate
            break
        }
    }
}

if (-not $cmakePath) {
    throw "CMake was not found. Install CMake or Visual Studio with the C++ CMake tools component."
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

& $cmakePath -S $picoRoot -B $buildDir
& $cmakePath --build $buildDir --config Debug
