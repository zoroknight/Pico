Write-Host "Pico environment check"
Write-Host "PowerShell: $($PSVersionTable.PSVersion)"

$tools = @("cmake", "cl", "ninja", "git")

foreach ($tool in $tools) {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    if ($command) {
        Write-Host "[OK] $tool -> $($command.Source)"
    } else {
        Write-Host "[Missing] $tool"
    }
}

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    Write-Host "[OK] vswhere -> $vsWhere"
    & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
} else {
    Write-Host "[Missing] vswhere"
}
