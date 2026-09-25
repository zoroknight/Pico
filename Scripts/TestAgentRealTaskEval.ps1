param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('List', 'Capture', 'Summary')]
    [string]$Action,
    [ValidatePattern('^H1-[0-9]{2}$')]
    [string]$TaskId,
    [string]$ABCaseName,
    [ValidateSet('On', 'Off')]
    [string]$Variant,
    [string]$SessionPath,
    [string]$MetricsPath,
    [string]$RunId,
    [string]$WorldHashBefore,
    [string]$Provider,
    [string]$Model
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fixturePath = Join-Path $root 'Tests/Agent/Fixtures/RealTaskEvalH1.json'
$baselineReviewsPath = Join-Path $root 'Tests/Agent/Fixtures/RealTaskEvalH1BaselineReviews.json'
$outputDir = Join-Path $root 'Projects/PicoSandbox/Saved/Agent/Evals/H1'
$worldPath = Join-Path $root 'Projects/PicoSandbox/Content/Maps/StarterWorld.pworld'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$fixture = Get-Content -LiteralPath $fixturePath -Raw -Encoding UTF8 | ConvertFrom-Json

function Get-Task([string]$Id) {
    $matches = @($fixture.tasks | Where-Object { $_.id -eq $Id })
    if ($matches.Count -ne 1) { throw "Unknown H1 task: $Id" }
    return $matches[0]
}

function Write-Json([string]$Path, $Value) {
    $json = ConvertTo-Json -InputObject $Value -Depth 30
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $utf8)
}

function Read-RunEvents([string]$Path, [string]$Id) {
    $result = New-Object System.Collections.ArrayList
    foreach ($line in [IO.File]::ReadLines($Path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $event = $line | ConvertFrom-Json
        if ($event.run_id -eq $Id) { [void]$result.Add($event) }
    }
    return $result.ToArray()
}

function Get-Answer($Events) {
    $answers = @($Events | Where-Object { $_.type -eq 'Message' -and $_.role -eq 'Assistant' })
    if ($answers.Count -eq 0) { return '' }
    return [string]$answers[-1].content
}

switch ($Action) {
    'List' {
        $fixture.tasks | Select-Object id, category, setup, baseline | Format-Table -Wrap
    }
    'Capture' {
        if (-not $TaskId) { throw 'Capture requires -TaskId.' }
        $task = Get-Task $TaskId
        $source = 'direct'
        $worldUnchanged = $null
        $projectHash = $null
        $selectedProvider = $null
        $selectedModel = $null
        if ($ABCaseName) {
            if (-not $Variant) { throw 'A/B capture requires -Variant On or Off.' }
            if ($ABCaseName -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid A/B case name.' }
            $caseDir = Join-Path $root "Projects/PicoSandbox/Saved/Agent/ABTests/$ABCaseName"
            $manifest = Get-Content -LiteralPath (Join-Path $caseDir 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($null -ne $task.PSObject.Properties['baseline'] -and $task.baseline -ne $ABCaseName) {
                throw "Task $TaskId is linked to $($task.baseline), not $ABCaseName."
            }
            $label = $Variant.ToLowerInvariant()
            $capture = $manifest.captures.$label
            if ($null -eq $capture) { throw "Missing captured variant: $label" }
            $SessionPath = Join-Path $caseDir "$label.jsonl"
            $MetricsPath = Join-Path $caseDir "$label-metrics.json"
            $RunId = [string]$capture.run_id
            $source = "$ABCaseName/$label"
            $worldUnchanged = [bool]$capture.world_unchanged
            $projectHash = [string]$manifest.world_sha256
            $selectedProvider = [string]$manifest.chat_identity.provider
            $selectedModel = [string]$manifest.chat_identity.model
            if (([string]$manifest.prompt).Trim() -cne ([string]$task.prompt).Trim()) {
                throw 'A/B prompt differs from task fixture.'
            }
        } else {
            if (-not $SessionPath -or -not $MetricsPath -or -not $RunId) {
                throw 'Direct capture requires -SessionPath, -MetricsPath and -RunId.'
            }
            if (-not $Provider -or -not $Model) {
                throw 'Direct capture requires -Provider and -Model for reproducibility.'
            }
            $selectedProvider = $Provider
            $selectedModel = $Model
            if ($WorldHashBefore) {
                $projectHash = $WorldHashBefore.ToUpperInvariant()
                $worldUnchanged = ((Get-FileHash -LiteralPath $worldPath -Algorithm SHA256).Hash -eq $projectHash)
            }
        }
        $SessionPath = (Resolve-Path -LiteralPath $SessionPath).Path
        $MetricsPath = (Resolve-Path -LiteralPath $MetricsPath).Path
        if ($ABCaseName -and (Get-FileHash -LiteralPath $SessionPath -Algorithm SHA256).Hash -ne $capture.sha256) {
            throw 'A/B session snapshot hash differs from the captured manifest.'
        }
        $metrics = Get-Content -LiteralPath $MetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($metrics.run_id -ne $RunId) { throw 'Metrics run_id does not match requested run.' }
        $events = @(Read-RunEvents $SessionPath $RunId)
        if ($events.Count -eq 0) { throw 'No events found for run.' }
        $prompts = @($events | Where-Object { $_.type -eq 'Message' -and $_.role -eq 'User' })
        if ($prompts.Count -ne 1) { throw "Expected one user prompt in run; got $($prompts.Count)." }
        if (([string]$prompts[0].content).Trim() -cne ([string]$task.prompt).Trim()) {
            throw 'Run prompt differs from task fixture; refusing mislabeled capture.'
        }
        $toolCalls = @($events | Where-Object { $_.type -eq 'ToolCall' } | ForEach-Object {
            [ordered]@{ sequence = $_.sequence; name = $_.tool_name; arguments = $_.payload }
        })
        $toolResults = @($events | Where-Object { $_.type -eq 'ToolResult' } | ForEach-Object {
            [ordered]@{ sequence = $_.sequence; name = $_.tool_name; status = $_.status }
        })
        $usage = $metrics.provider_usage
        $report = [ordered]@{
            schema_version = 1
            task_id = $TaskId
            source = $source
            run_id = $RunId
            project = 'PicoSandbox'
            world_sha256_before = $projectHash
            world_unchanged = $worldUnchanged
            provider = $selectedProvider
            model = $selectedModel
            status = $metrics.status
            trace = [ordered]@{
                session_path = $SessionPath
                session_sha256 = (Get-FileHash -LiteralPath $SessionPath -Algorithm SHA256).Hash
                metrics_path = $MetricsPath
                metrics_sha256 = (Get-FileHash -LiteralPath $MetricsPath -Algorithm SHA256).Hash
                first_sequence = $events[0].sequence
                last_sequence = $events[-1].sequence
            }
            measurements = [ordered]@{
                tool_calls = $toolCalls.Count
                tool_results = $toolResults.Count
                cache_miss_tokens = $usage.cache_miss_tokens
                cache_hit_tokens = $usage.cache_hit_tokens
                provider_input_tokens = $usage.prompt_tokens
                provider_output_tokens = $usage.completion_tokens
                provider_ms = [math]::Round($metrics.latency.provider.total_us / 1000, 1)
                tool_ms = [math]::Round($metrics.latency.tool.total_us / 1000, 1)
                total_measured_ms = [math]::Round(($metrics.latency.provider.total_us + $metrics.latency.tool.total_us + $metrics.latency.approval.total_us + $metrics.latency.validation.total_us) / 1000, 1)
            }
            tool_calls = $toolCalls
            tool_results = $toolResults
            final_answer = Get-Answer $events
            review = [ordered]@{
                criteria = @($task.criteria | ForEach-Object { [ordered]@{ item = $_; verdict = 'Unreviewed'; evidence_sequences = @(); note = '' } })
                ungrounded_claims = 0
                unrelated_edits = 'Unreviewed'
                safety = 'Unreviewed'
                notes = ''
            }
        }
        New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
        $output = Join-Path $outputDir "$TaskId-$RunId.json"
        if (Test-Path -LiteralPath $output) { throw "Report exists; refusing to overwrite review: $output" }
        Write-Json $output $report
        Write-Output "Captured: $output"
        Write-Output "Status=$($metrics.status); tools=$($toolCalls.Count); miss=$($usage.cache_miss_tokens); provider_ms=$($report.measurements.provider_ms)"
        Write-Output 'Review the criteria against the final answer and referenced ToolResult sequences; leave unknown items Unreviewed.'
    }
    'Summary' {
        if (-not (Test-Path -LiteralPath $outputDir)) { throw 'No H1 captures yet.' }
        $reports = @(Get-ChildItem -LiteralPath $outputDir -Filter 'H1-*.json' -File | ForEach-Object {
            Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
        })
        if ($reports.Count -eq 0) { throw 'No H1 captures yet.' }
        $baselineReviews = (Get-Content -LiteralPath $baselineReviewsPath -Raw -Encoding UTF8 | ConvertFrom-Json).reviews
        $rows = @($reports | ForEach-Object {
            $report = $_
            $baseline = @($baselineReviews | Where-Object { $_.run_id -eq $report.run_id -and $_.task_id -eq $report.task_id })
            if ($baseline.Count -gt 1) { throw "Duplicate baseline review: $($report.run_id)" }
            if ($baseline.Count -eq 1) {
                $review = $baseline[0]
                if ($review.source -ne $report.source) { throw "Baseline source mismatch: $($report.run_id)" }
                if (@($review.verdicts).Count -ne @($report.review.criteria).Count) {
                    throw "Baseline criterion count mismatch: $($report.run_id)"
                }
                $validSequences = @($report.tool_calls | ForEach-Object { $_.sequence }) +
                    @($report.tool_results | ForEach-Object { $_.sequence })
                foreach ($group in $review.evidence_sequences) {
                    foreach ($sequence in $group) {
                        if ($sequence -notin $validSequences) {
                            throw "Unknown evidence sequence $sequence in $($report.run_id)"
                        }
                    }
                }
                for ($i = 0; $i -lt $review.verdicts.Count; $i++) {
                    $report.review.criteria[$i].verdict = $review.verdicts[$i]
                }
                $report.review.ungrounded_claims = $review.ungrounded_claims
                $report.review.unrelated_edits = $review.unrelated_edits
                $report.review.safety = $review.safety
            }
            $criteria = @($_.review.criteria)
            foreach ($criterion in $criteria) {
                if ($criterion.verdict -notin @('Pass', 'Fail', 'Unreviewed')) {
                    throw "Invalid criterion verdict in $($_.run_id): $($criterion.verdict)"
                }
            }
            if ($_.review.safety -notin @('Pass', 'Fail', 'Unreviewed') -or
                $_.review.unrelated_edits -notin @('No', 'Yes', 'Unreviewed') -or
                [int]$_.review.ungrounded_claims -lt 0) {
                throw "Invalid review fields in $($_.run_id)"
            }
            $unknown = @($criteria | Where-Object { $_.verdict -eq 'Unreviewed' }).Count
            $failed = @($criteria | Where-Object { $_.verdict -eq 'Fail' }).Count
            $reviewed = $unknown -eq 0 -and $_.review.safety -ne 'Unreviewed' -and $_.review.unrelated_edits -ne 'Unreviewed'
            $quality = if (-not $reviewed) { 'Pending' }
                elseif ($_.status -eq 'Completed' -and $failed -eq 0 -and $_.review.ungrounded_claims -eq 0 -and $_.review.safety -eq 'Pass' -and $_.review.unrelated_edits -eq 'No') { 'Pass' }
                else { 'Fail' }
            [pscustomobject]@{
                Task = $_.task_id; Source = $_.source; Quality = $quality
                Coverage = "$($criteria.Count - $failed - $unknown)/$($criteria.Count)"
                Missing = if ($reviewed) { $failed } else { 'Pending' }
                Ungrounded = $_.review.ungrounded_claims
                Edits = $_.review.unrelated_edits; Tools = $_.measurements.tool_calls
                Miss = $_.measurements.cache_miss_tokens
                MeasuredMs = $_.measurements.total_measured_ms; RunId = $_.run_id
            }
        })
        $rows | Sort-Object Task, Source | Format-Table -AutoSize
        $reviewedRows = @($rows | Where-Object { $_.Quality -ne 'Pending' })
        $passed = @($reviewedRows | Where-Object { $_.Quality -eq 'Pass' }).Count
        $coveredTasks = @($rows | Select-Object -ExpandProperty Task -Unique).Count
        Write-Output "Reviewed success: $passed/$($reviewedRows.Count) runs; task coverage: $coveredTasks/$(@($fixture.tasks).Count); pending reviews: $($rows.Count - $reviewedRows.Count)."
        Write-Output 'MeasuredMs sums instrumented provider/tool/approval/validation spans; it excludes UI scheduling outside those spans. Cache attribution requires the separate strict A/B protocol.'
    }
}
