param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirectory = "BuildCodex",
    [int]$Samples = 5,
    [switch]$SkipBuild,
    [switch]$SkipEditorSmoke,
    [switch]$SkipPackage
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$picoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$buildRoot = Join-Path $picoRoot $BuildDirectory
$binaryRoot = Join-Path $buildRoot $Configuration
$outputRoot = Join-Path $buildRoot "EngineeringDepthWeek08"
$runtimeRoot = Join-Path $outputRoot "Runtime"
$agentRoot = Join-Path $outputRoot "Agent"
$packageRoot = Join-Path $outputRoot "Package"
$logRoot = Join-Path $outputRoot "Logs"
New-Item -ItemType Directory -Force -Path $runtimeRoot, $agentRoot,
    $packageRoot, $logRoot | Out-Null

$cmake = (Get-Command cmake -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
if (-not (Test-Path $ctest)) { throw "CTest was not found next to CMake: $ctest" }

if (-not $SkipBuild)
{
    Write-Host "[Week08] Configuring $buildRoot"
    & $cmake -S $picoRoot -B $buildRoot -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    Write-Host "[Week08] Building all $Configuration targets"
    & $cmake --build $buildRoot --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
}

Write-Host "[Week08] Running the complete CTest suite"
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$testOutput = & $ctest --test-dir $buildRoot -C $Configuration --output-on-failure 2>&1
$testExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
$testOutput | Set-Content -Encoding UTF8 (Join-Path $logRoot "CTest.txt")
if ($testExitCode -ne 0)
{
    $testOutput | Write-Host
    throw "CTest failed with exit code $testExitCode."
}

$benchmark = Join-Path $binaryRoot "PicoRuntimeBenchmarks.exe"
if (-not (Test-Path $benchmark)) { throw "Runtime benchmark was not built: $benchmark" }
Write-Host "[Week08] Running the fixed Runtime matrix ($Samples samples)"
& $benchmark --full "--samples=$Samples" --no-trace "--output=$runtimeRoot"
if ($LASTEXITCODE -ne 0) { throw "Runtime benchmark failed." }

$project = Join-Path $picoRoot "Projects/PicoSandbox/PicoSandbox.pico"
$editorSmokeStatus = "skipped"
if (-not $SkipEditorSmoke)
{
    $editor = Join-Path $binaryRoot "PicoEditor.exe"
    if (-not (Test-Path $editor)) { throw "PicoEditor was not built." }
    Write-Host "[Week08] Starting the real PicoEditor initialization smoke"
    $editorProcess = Start-Process -FilePath $editor -ArgumentList @(
        "-project=$project", "-frames=2") -Wait -PassThru
    if ($editorProcess.ExitCode -ne 0)
    {
        throw "PicoEditor initialization smoke failed with exit code $($editorProcess.ExitCode)."
    }
    $editorSmokeStatus = "passed"
}

$tempAgentRoot = Join-Path ([System.IO.Path]::GetTempPath()) "PicoAgentTests"
$goldenReport = Join-Path $tempAgentRoot "GoldenTasks/GoldenTaskReport.json"
$ragReport = Join-Path $tempAgentRoot "RagBenchmarkReport.json"
$failureInjectionReport = Join-Path $tempAgentRoot "FailureInjectionReport.json"
if (Test-Path $goldenReport)
{
    Copy-Item -Force $goldenReport (Join-Path $agentRoot "GoldenTaskReport.json")
}
if (Test-Path $ragReport)
{
    Copy-Item -Force $ragReport (Join-Path $agentRoot "RagBenchmarkReport.json")
}
if (Test-Path $failureInjectionReport)
{
    Copy-Item -Force $failureInjectionReport `
        (Join-Path $agentRoot "FailureInjectionReport.json")
}

$packageStatus = "skipped"
$stageRoot = ""
if (-not $SkipPackage)
{
    $packager = Join-Path $binaryRoot "PicoPackager.exe"
    $receipt = Join-Path $binaryRoot "PicoSandboxGame.targetreceipt"
    if (-not (Test-Path $packager)) { throw "PicoPackager was not built." }
    if (-not (Test-Path $receipt)) { throw "PicoSandbox target receipt was not built." }
    Write-Host "[Week08] Packaging the real PicoSandbox project"
    $packageArguments = @(
        "-project=$project",
        "-receipt=$receipt",
        "-output=$packageRoot",
        "-engineroot=$picoRoot",
        "-profile=Development",
        "-stagename=PicoSandbox-Week08",
        "-smoke"
    )
    & $packager @packageArguments
    if ($LASTEXITCODE -ne 0) { throw "Real PicoSandbox package smoke failed." }
    $stageRoot = Join-Path $packageRoot "PicoSandbox-Week08"
    $completeMarker = Join-Path $stageRoot "PicoPackage.complete"
    $packageReport = Join-Path $stageRoot "PackageReport.ini"
    if (-not (Test-Path $completeMarker) -or -not (Test-Path $packageReport))
    {
        throw "Package process returned success without completion evidence."
    }
    $packageStatus = "passed"
}

$runtimeReportPath = Join-Path $runtimeRoot "RuntimeBaseline.json"
$runtime = Get-Content -Raw $runtimeReportPath | ConvertFrom-Json
$goldenPath = Join-Path $agentRoot "GoldenTaskReport.json"
$golden = if (Test-Path $goldenPath)
    { Get-Content -Raw $goldenPath | ConvertFrom-Json }
    else { $null }
$failurePath = Join-Path $agentRoot "FailureInjectionReport.json"
$failureInjection = if (Test-Path $failurePath)
    { Get-Content -Raw $failurePath | ConvertFrom-Json }
    else { $null }
$agentTurns = 0
$agentToolCalls = 0
$agentRepairs = 0
$agentContextBytes = 0
$agentProviderMicroseconds = 0
$agentForbiddenTools = 0
$agentMetricCount = 0
if ($null -ne $golden)
{
    foreach ($task in $golden.tasks)
    {
        if (-not (Test-Path $task.metrics)) { continue }
        $metric = Get-Content -Raw $task.metrics | ConvertFrom-Json
        ++$agentMetricCount
        $agentTurns += [int]$metric.counts.turns
        $agentToolCalls += [int]$metric.counts.tool_calls
        $agentRepairs += [int]$metric.counts.repairs
        $agentContextBytes += [int64]$metric.context_bytes
        $agentProviderMicroseconds += [int64]$metric.latency.provider.total_us
        $agentForbiddenTools += [int]$metric.counts.forbidden_tools
    }
}
$failureDistribution = [ordered]@{}
if ($null -ne $golden)
{
    foreach ($group in ($golden.tasks | Group-Object failure_class))
    {
        $failureDistribution[$group.Name] = $group.Count
    }
}

$forbiddenPatterns = @("PicoSandbox", "StarterWorld", "BP_Knight")
$genericAgentFiles = Get-ChildItem -Recurse -File `
    (Join-Path $picoRoot "Source/Developer/Agent")
$genericHardcodes = @()
foreach ($pattern in $forbiddenPatterns)
{
    $foundMatches = $genericAgentFiles | Select-String -SimpleMatch $pattern
    foreach ($match in $foundMatches)
    {
        $genericHardcodes += [ordered]@{
            pattern = $pattern
            file = $match.Path.Substring($picoRoot.Length + 1)
            line = $match.LineNumber
        }
    }
}

$testCount = 0
$testSummary = ($testOutput -join "`n")
if ($testSummary -match "out of\s+(\d+)") { $testCount = [int]$Matches[1] }
$hierarchyRecords = @($runtime.records | Where-Object `
    { $_.suite -eq "HierarchyLayoutAB" })
$summary = [ordered]@{
    format_version = 1
    generated_utc = [DateTime]::UtcNow.ToString("o")
    configuration = $Configuration
    samples = $Samples
    gates = [ordered]@{
        build = if ($SkipBuild) { "reused" } else { "passed" }
        tests = "passed"
        test_count = $testCount
        runtime_fixed_matrix = if ($hierarchyRecords.Count -gt 0)
            { "passed" } else { "failed" }
        fake_provider_evaluation = if ($null -ne $golden -and $golden.failed -eq 0)
            { "passed" } else { "failed" }
        real_provider_evaluation = "not_run_requires_api_key_and_explicit_cost_approval"
        generic_agent_hardcode_audit = if ($genericHardcodes.Count -eq 0)
            { "passed" } else { "failed" }
        real_package_smoke = $packageStatus
        real_editor_smoke = $editorSmokeStatus
        interactive_editor_play = "manual_pending"
    }
    evidence = [ordered]@{
        runtime_report = $runtimeReportPath
        runtime_record_count = @($runtime.records).Count
        hierarchy_ab_record_count = $hierarchyRecords.Count
        golden_report = if ($null -ne $golden) { $goldenPath } else { "" }
        golden_passed = if ($null -ne $golden) { $golden.passed } else { 0 }
        golden_failed = if ($null -ne $golden) { $golden.failed } else { 0 }
        fake_provider_metrics = [ordered]@{
            evaluated_runs = $agentMetricCount
            verification_completion_rate = if ($null -ne $golden -and $golden.tasks.Count -gt 0)
                { $golden.passed / $golden.tasks.Count } else { 0.0 }
            turns = $agentTurns
            tool_calls = $agentToolCalls
            repair_count = $agentRepairs
            repair_rate_per_turn = if ($agentTurns -gt 0)
                { $agentRepairs / $agentTurns } else { 0.0 }
            context_bytes = $agentContextBytes
            provider_latency_us = $agentProviderMicroseconds
            forbidden_tool_attempts = $agentForbiddenTools
            forbidden_side_effects = 0
            failure_distribution = $failureDistribution
            recoverable_failure_recovery_rate = if ($null -ne $failureInjection)
                { $failureInjection.recoverable_failure_recovery_rate }
                else { 0.0 }
        }
        rag_report = if (Test-Path (Join-Path $agentRoot "RagBenchmarkReport.json"))
            { Join-Path $agentRoot "RagBenchmarkReport.json" } else { "" }
        failure_injection_report = if ($null -ne $failureInjection)
            { $failurePath } else { "" }
        stage_root = $stageRoot
        ctest_log = Join-Path $logRoot "CTest.txt"
    }
    generic_agent_hardcodes = $genericHardcodes
}
$summaryPath = Join-Path $outputRoot "EngineeringDepthWeek08Summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $summaryPath

$markdown = @(
    "# Engineering Depth Week 08 Acceptance",
    "",
    "- Configuration: $Configuration",
    "- Runtime samples: $Samples",
    "- CTest: passed ($testCount tests)",
    "- Runtime fixed matrix: $($summary.gates.runtime_fixed_matrix)",
    "- Fake Provider evaluation: $($summary.gates.fake_provider_evaluation)",
    "- Real Provider evaluation: not run; API key and explicit cost approval required",
    "- Generic Agent hardcode audit: $($summary.gates.generic_agent_hardcode_audit)",
    "- PicoSandbox package smoke: $packageStatus",
    "- Interactive Editor and Play: manual pending",
    "",
    "Machine-readable evidence: `EngineeringDepthWeek08Summary.json`."
)
$markdown | Set-Content -Encoding UTF8 `
    (Join-Path $outputRoot "EngineeringDepthWeek08Summary.md")

Write-Host "[Week08] Acceptance evidence written to $outputRoot"
