$ErrorActionPreference = "Stop"

$minimumCMakeVersion = [Version]"3.22"
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

Write-Host "Pico Windows environment check"
Write-Host "PowerShell: $($PSVersionTable.PSVersion)"

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git)
{
    throw "Git was not found. Install Git for Windows and reopen the terminal."
}
Write-Host "[OK] Git -> $($git.Source)"

if (-not (Test-Path $vsWhere))
{
    throw "Visual Studio Installer was not found. Install Visual Studio 2022 with Desktop development with C++."
}

$visualStudioPath = & $vsWhere `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $visualStudioPath)
{
    throw "Visual Studio 2022 C++ tools were not found. Install the Desktop development with C++ workload."
}
Write-Host "[OK] Visual Studio C++ -> $visualStudioPath"

$cmakePath = Find-CMake
if (-not $cmakePath)
{
    throw "CMake was not found. Install CMake 3.22+ or the Visual Studio C++ CMake tools component."
}

$cmakeVersionLine = (& $cmakePath --version | Select-Object -First 1)
if ($cmakeVersionLine -notmatch "(\d+\.\d+\.\d+)")
{
    throw "Could not determine the CMake version from: $cmakeVersionLine"
}

$cmakeVersion = [Version]$Matches[1]
if ($cmakeVersion -lt $minimumCMakeVersion)
{
    throw "CMake $cmakeVersion is too old. Pico requires CMake $minimumCMakeVersion or newer."
}

Write-Host "[OK] CMake $cmakeVersion -> $cmakePath"
Write-Host "Environment check passed. The Windows SDK and OpenGL library will be verified during CMake configure."
