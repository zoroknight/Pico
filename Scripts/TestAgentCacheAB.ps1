param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Init', 'Reset', 'Capture', 'Compare', 'Restore')]
    [string]$Action,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$CaseName = 'cache-ab',

    [ValidateSet('On', 'Off')]
    [string]$Variant,

    [string]$SeedSession,
    [string]$TestSessionId,
    [string]$Prompt
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sessionsDir = Join-Path $root 'Projects/PicoSandbox/Saved/Agent/Sessions'
$caseDir = Join-Path $root "Projects/PicoSandbox/Saved/Agent/ABTests/$CaseName"
$manifestPath = Join-Path $caseDir 'manifest.json'
$settingsPath = Join-Path $root 'Saved/Editor/Agent/ChatSettings.ini'
$worldPath = Join-Path $root 'Projects/PicoSandbox/Content/Maps/StarterWorld.pworld'
$utf8 = [Text.UTF8Encoding]::new($false)

function Assert-EditorClosed {
    if (Get-Process -Name PicoEditor -ErrorAction SilentlyContinue) {
        throw 'Close PicoEditor before changing or capturing the test session.'
    }
}

function Write-Text([string]$Path, [string]$Value) {
    [IO.File]::WriteAllText($Path, $Value, $utf8)
}

function ConvertTo-Dictionary($Value) {
    if ($Value -is [pscustomobject]) {
        $result = @{}
        foreach ($property in $Value.PSObject.Properties) {
            $result[$property.Name] = ConvertTo-Dictionary $property.Value
        }
        return $result
    }
    if ($Value -is [array]) {
        $result = @()
        foreach ($item in $Value) {
            $result += ,(ConvertTo-Dictionary $item)
        }
        return ,$result
    }
    return $Value
}

function Read-Manifest {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Test case not initialized: $manifestPath"
    }
    $parsed = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    return ConvertTo-Dictionary $parsed
}

function Save-Manifest([System.Collections.IDictionary]$Manifest) {
    Write-Text $manifestPath ($Manifest | ConvertTo-Json -Depth 10)
}

function Get-CompactValue {
    $text = [IO.File]::ReadAllText($settingsPath)
    $match = [regex]::Match($text, '(?m)^CompactPriorTaskHistory=(true|false)\r?$')
    if (-not $match.Success) { throw "Compact setting not found: $settingsPath" }
    return $match.Groups[1].Value
}

function Get-ChatIdentity {
    $text = [IO.File]::ReadAllText($settingsPath)
    $provider = [regex]::Match($text, '(?m)^LastProvider=([^\r\n]+)\r?$')
    if (-not $provider.Success) { throw 'LastProvider is missing from ChatSettings.ini.' }
    $modelPattern = '(?m)^' + [regex]::Escape($provider.Groups[1].Value) + '=([^\r\n]+)\r?$'
    $model = [regex]::Match($text, $modelPattern)
    if (-not $model.Success) { throw 'Selected model is missing from ChatSettings.ini.' }
    return [pscustomobject]@{
        provider = $provider.Groups[1].Value
        model = $model.Groups[1].Value
    }
}

function Get-WorldHash {
    if (-not (Test-Path -LiteralPath $worldPath -PathType Leaf)) {
        throw "Test world not found: $worldPath"
    }
    return (Get-FileHash -LiteralPath $worldPath -Algorithm SHA256).Hash
}

function Set-CompactValue([string]$Value) {
    $text = [IO.File]::ReadAllText($settingsPath)
    $pattern = '(?m)^CompactPriorTaskHistory=(true|false)(\r?)$'
    if (-not [regex]::IsMatch($text, $pattern)) {
        throw "Compact setting not found: $settingsPath"
    }
    $updated = [regex]::Replace($text, $pattern,
        "CompactPriorTaskHistory=$Value`$2")
    Write-Text $settingsPath $updated
}

function Get-TestPath([System.Collections.IDictionary]$Manifest) {
    return Join-Path $sessionsDir ($Manifest.test_session_id + '.jsonl')
}

function Get-SeedProjection([System.Collections.IDictionary]$Manifest) {
    $seed = [IO.File]::ReadAllText((Join-Path $caseDir 'seed.jsonl'))
    $old = '"session_id":"' + $Manifest.seed_session_id + '"'
    $new = '"session_id":"' + $Manifest.test_session_id + '"'
    if (-not $seed.Contains($old)) { throw 'Seed session ID is missing from the event log.' }
    return $seed.Replace($old, $new)
}

function Get-AddedEvents([string]$Path, [long]$BaselineSequence) {
    foreach ($line in [IO.File]::ReadLines($Path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $event = $line | ConvertFrom-Json
        if ([long]$event.sequence -gt $BaselineSequence) { $event }
    }
}

function Get-RunSummary([string]$Label, [System.Collections.IDictionary]$Capture) {
    $metricsPath = Join-Path $caseDir ($Label + '-metrics.json')
    $metrics = Get-Content -LiteralPath $metricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $usage = $metrics.provider_usage
    $denominator = [double]($usage.cache_hit_tokens + $usage.cache_miss_tokens)
    $rate = if ($denominator -gt 0) {
        [math]::Round(100 * $usage.cache_hit_tokens / $denominator, 1)
    } else { 0 }
    $recordedCompact = $metrics.context_assembly.PSObject.Properties[
        'task_boundary_projection_enabled']
    $compact = if ($null -eq $recordedCompact) { 'Legacy' }
        elseif ([bool]$recordedCompact.Value) { 'On' } else { 'Off' }
    return [pscustomobject]@{
        Variant = $Label
        Compact = $compact
        Status = $metrics.status
        Steps = $metrics.counts.turns
        Tools = $metrics.counts.tool_calls
        Input = $usage.prompt_tokens
        Hit = $usage.cache_hit_tokens
        Miss = $usage.cache_miss_tokens
        HitRate = "$rate%"
        ProviderMs = [math]::Round($metrics.latency.provider.total_us / 1000, 1)
        ToolNames = ($Capture.tool_names -join ', ')
        RunId = $Capture.run_id
    }
}

switch ($Action) {
    'Init' {
        Assert-EditorClosed
        if (-not $SeedSession -or -not $Prompt) {
            throw 'Init requires -SeedSession and -Prompt.'
        }
        if (Test-Path -LiteralPath $manifestPath) {
            throw "Case already initialized: $manifestPath"
        }
        $source = (Resolve-Path -LiteralPath $SeedSession).Path
        if ([IO.Path]::GetExtension($source) -ne '.jsonl') {
            throw 'Seed session must be a JSONL file.'
        }
        $first = [IO.File]::ReadLines($source) | Select-Object -First 1 | ConvertFrom-Json
        $last = Get-Content -LiteralPath $source -Tail 1 -Encoding UTF8 | ConvertFrom-Json
        $seedId = [string]$first.session_id
        if (-not $seedId -or $seedId -ne [IO.Path]::GetFileNameWithoutExtension($source)) {
            throw 'Seed filename and event session ID do not match.'
        }
        if (-not $TestSessionId) { $TestSessionId = "$seedId-ab-$CaseName" }
        if ($TestSessionId -notmatch '^editor-chat-[A-Za-z0-9_-]+$') {
            throw 'Test session ID must use the editor-chat- prefix.'
        }
        if ($TestSessionId -eq $seedId) { throw 'Test session ID must differ from seed ID.' }
        New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination (Join-Path $caseDir 'seed.jsonl')
        $manifest = [ordered]@{
            seed_session_id = $seedId
            test_session_id = $TestSessionId
            baseline_sequence = [long]$last.sequence
            original_compact = Get-CompactValue
            chat_identity = Get-ChatIdentity
            world_sha256 = Get-WorldHash
            prompt = $Prompt
            captures = @{}
        }
        $testPath = Get-TestPath $manifest
        $projection = Get-SeedProjection $manifest
        if ((Test-Path -LiteralPath $testPath) -and
            [IO.File]::ReadAllText($testPath) -cne $projection) {
            throw "Existing test session has different content: $testPath"
        }
        Write-Text $testPath $projection
        Save-Manifest $manifest
        Write-Output "Prepared: $testPath"
        Write-Output "Prompt: $Prompt"
    }
    'Reset' {
        Assert-EditorClosed
        if (-not $Variant) { throw 'Reset requires -Variant On or Off.' }
        $manifest = Read-Manifest
        if (-not $manifest.Contains('world_sha256')) {
            $manifest.world_sha256 = Get-WorldHash
            Save-Manifest $manifest
        }
        if (-not $manifest.Contains('chat_identity')) {
            $manifest.chat_identity = Get-ChatIdentity
            Save-Manifest $manifest
        }
        $identity = Get-ChatIdentity
        if ($identity.provider -ne $manifest.chat_identity.provider -or
            $identity.model -ne $manifest.chat_identity.model) {
            throw 'Provider or model changed since Init; use the original selection.'
        }
        if ((Get-WorldHash) -ne $manifest.world_sha256) {
            throw 'StarterWorld.pworld changed since Init; do not compare this pair.'
        }
        $label = $Variant.ToLowerInvariant()
        if (Test-Path -LiteralPath (Join-Path $caseDir "$label.jsonl")) {
            throw "$Variant was already captured; use a new case for another pair."
        }
        $testPath = Get-TestPath $manifest
        $projection = Get-SeedProjection $manifest
        if ((Test-Path -LiteralPath $testPath) -and
            [IO.File]::ReadAllText($testPath) -cne $projection) {
            $hash = (Get-FileHash -LiteralPath $testPath -Algorithm SHA256).Hash
            $saved = @($manifest.captures.Values | Where-Object { $_.sha256 -eq $hash })
            if ($saved.Count -eq 0) {
                throw 'The test session has uncaptured changes. Capture it before Reset.'
            }
        }
        Write-Text $testPath $projection
        Set-CompactValue $(if ($Variant -eq 'On') { 'true' } else { 'false' })
        $manifest.active_variant = $label
        Save-Manifest $manifest
        Write-Output "Ready: $Variant, session $($manifest.test_session_id)"
        Write-Output "Prompt: $($manifest.prompt)"
        Write-Output 'Open PicoEditor, select the test conversation, verify Compact, send the prompt once, wait for completion, then close PicoEditor.'
    }
    'Capture' {
        Assert-EditorClosed
        if (-not $Variant) { throw 'Capture requires -Variant On or Off.' }
        $manifest = Read-Manifest
        $label = $Variant.ToLowerInvariant()
        if ($manifest.active_variant -ne $label) {
            throw "Active variant is $($manifest.active_variant), not $label."
        }
        $identity = Get-ChatIdentity
        if ($identity.provider -ne $manifest.chat_identity.provider -or
            $identity.model -ne $manifest.chat_identity.model) {
            throw 'Provider or model changed during the run; do not compare it.'
        }
        $snapshot = Join-Path $caseDir "$label.jsonl"
        if (Test-Path -LiteralPath $snapshot) { throw "Already captured: $snapshot" }
        $testPath = Get-TestPath $manifest
        $events = @(Get-AddedEvents $testPath ([long]$manifest.baseline_sequence))
        $userMessages = @($events | Where-Object {
            $_.type -eq 'Message' -and $_.role -eq 'User'
        })
        if ($userMessages.Count -ne 1) {
            throw "Expected exactly one new user prompt; found $($userMessages.Count)."
        }
        if ($userMessages[0].content.Trim() -cne $manifest.prompt.Trim()) {
            throw 'New prompt does not match the frozen prompt in manifest.json.'
        }
        $runId = [string]$userMessages[0].run_id
        $metricsPath = Join-Path $sessionsDir "Metrics/$runId.json"
        if (-not (Test-Path -LiteralPath $metricsPath)) {
            throw "Run metrics not found: $metricsPath"
        }
        $metrics = Get-Content -LiteralPath $metricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($metrics.status -ne 'Completed') {
            throw "Run did not complete: $($metrics.status)"
        }
        $recordedCompact = $metrics.context_assembly.PSObject.Properties[
            'task_boundary_projection_enabled']
        if ($null -eq $recordedCompact) {
            throw 'Run metrics do not record Compact. Rebuild and launch the updated Debug PicoEditor.'
        }
        if ([bool]$recordedCompact.Value -ne ($Variant -eq 'On')) {
            throw "Run recorded Compact=$($recordedCompact.Value), but capture is labeled $Variant."
        }
        $projectedMessages = [long]$metrics.history_projection.old_messages
        $tools = @($events | Where-Object {
            $_.run_id -eq $runId -and $_.type -eq 'ToolCall'
        } | ForEach-Object { [string]$_.tool_name })
        $toolCalls = @($events | Where-Object {
            $_.run_id -eq $runId -and $_.type -eq 'ToolCall'
        } | ForEach-Object {
            $_.tool_name + '(' + ($_.payload | ConvertTo-Json -Depth 20 -Compress) + ')'
        })
        Copy-Item -LiteralPath $testPath -Destination $snapshot
        Copy-Item -LiteralPath $metricsPath -Destination (Join-Path $caseDir "$label-metrics.json")
        $hash = (Get-FileHash -LiteralPath $snapshot -Algorithm SHA256).Hash
        $manifest.captures[$label] = [ordered]@{
            run_id = $runId
            tool_names = $tools
            tool_calls = $toolCalls
            sha256 = $hash
            compact_enabled = [bool]$recordedCompact.Value
            projected_old_messages = $projectedMessages
            world_unchanged = ((Get-WorldHash) -eq $manifest.world_sha256)
        }
        Save-Manifest $manifest
        Get-RunSummary $label $manifest.captures[$label] | Format-List
        if (-not $manifest.captures[$label].world_unchanged) {
            Write-Warning 'The world file changed. Treat this pair as invalid.'
        }
    }
    'Compare' {
        $manifest = Read-Manifest
        if (-not $manifest.captures.Contains('on') -or
            -not $manifest.captures.Contains('off')) {
            throw 'Capture both On and Off before comparing.'
        }
        $on = Get-RunSummary 'on' $manifest.captures.on
        $off = Get-RunSummary 'off' $manifest.captures.off
        @($on, $off) | Select-Object Variant, Compact, Status, Steps, Tools, Input,
            Hit, Miss, HitRate, ProviderMs, RunId | Format-Table -AutoSize
        Write-Output "On tools:  $($on.ToolNames)"
        Write-Output "Off tools: $($off.ToolNames)"
        if ($on.Tools -ne $off.Tools -or $on.ToolNames -cne $off.ToolNames) {
            Write-Warning 'Tool paths differ. This pair is not a matched quality comparison.'
        } elseif (($manifest.captures.on.tool_calls -join '|') -cne
            ($manifest.captures.off.tool_calls -join '|')) {
            Write-Warning 'Tool names match, but arguments differ. Review task depth before comparing.'
        }
        if (-not $manifest.captures.on.world_unchanged -or
            -not $manifest.captures.off.world_unchanged) {
            Write-Warning 'The world file changed during a run. Treat this pair as invalid.'
        }
        if ($off.Miss -gt 0) {
            $reduction = [math]::Round(100 * ($off.Miss - $on.Miss) / $off.Miss, 1)
            Write-Output "Miss-token reduction (On vs Off): $reduction%"
        }
        Write-Output 'Check answer evidence and scene state manually. Provider cache is best-effort; repeat pairs with alternating order before claiming a causal improvement.'
    }
    'Restore' {
        Assert-EditorClosed
        $manifest = Read-Manifest
        $testPath = Get-TestPath $manifest
        if (Test-Path -LiteralPath $testPath) {
            $projection = Get-SeedProjection $manifest
            if ([IO.File]::ReadAllText($testPath) -cne $projection) {
                $hash = (Get-FileHash -LiteralPath $testPath -Algorithm SHA256).Hash
                $saved = @($manifest.captures.Values | Where-Object { $_.sha256 -eq $hash })
                if ($saved.Count -eq 0) {
                    throw 'The test session has uncaptured changes. Capture it first.'
                }
            }
            Move-Item -LiteralPath $testPath -Destination (Join-Path $caseDir 'final.jsonl')
        }
        Set-CompactValue $manifest.original_compact
        Write-Output "Restored Compact=$($manifest.original_compact). Test files: $caseDir"
    }
}
