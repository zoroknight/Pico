param(
    [Parameter(Mandatory = $true)]
    [string]$SessionPath,
    [string]$RunId
)

$resolved = Resolve-Path -LiteralPath $SessionPath -ErrorAction Stop
$found = 0
$lineNumber = 0
foreach ($line in [System.IO.File]::ReadLines($resolved.Path)) {
    $lineNumber++
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    try {
        $event = $line | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "Invalid JSONL at line ${lineNumber}: $($_.Exception.Message)"
    }
    if ($event.type -ne 'TraceSpan' -or $event.span_name -ne 'Model.Generate') { continue }
    if ($RunId -and $event.run_id -ne $RunId) { continue }
    $profile = $event.payload.request_profile
    if ($null -eq $profile) { continue }
    $found++
    [pscustomobject]@{
        RunId = $event.run_id
        Step = $profile.agent_step
        SourceEventFirstSequence = $profile.source_event_first_sequence
        SourceEventSequence = $profile.source_event_sequence
        ProviderFamily = $profile.provider_family
        Model = $profile.provider_model
        BodyBytes = $profile.serialized_bytes
        BodyFingerprint = $profile.serialized_fingerprint
        ToolSchemaBytes = $profile.tool_schema.bytes
        ToolSchemaFingerprint = $profile.tool_schema.fingerprint
        HistoryBytes = $profile.history.bytes
        ToolResultBytes = $profile.tool_results.bytes
        KnowledgeBytes = $profile.knowledge.bytes
        SkillBytes = $profile.skill.bytes
        TaskStateBytes = $profile.task_state.bytes
        ProgressLedgerBytes = $profile.progress_ledger.bytes
        SystemPromptBytes = $profile.system_prompt.bytes
        ToolNames = @($profile.tool_names) -join ', '
        SelectionRevision = $profile.editor_snapshot.selection_revision
        WorldRevision = $profile.editor_snapshot.world_revision
        WorldContentFingerprint = $profile.editor_snapshot.world_content_fingerprint
        AssetContentFingerprint = $profile.editor_snapshot.asset_content_fingerprint
        AssetDescriptorCount = $profile.editor_snapshot.asset_descriptor_count
        SelectionContentFingerprint = $profile.editor_snapshot.selection_content_fingerprint
        EditorSnapshotFingerprint = $profile.editor_snapshot.fingerprint
        ExternalSnapshotPersisted = $profile.replay_contract.external_snapshot_persisted
        ProjectedReadEnabled = $profile.tool_result_projection.enabled
        ProjectedReadResults = $profile.tool_result_projection.results
        ProjectedReadBytes = $profile.tool_result_projection.saved_bytes
        HttpAttempts = $profile.http_attempts
        Compact = $profile.task_boundary_projection_enabled
        CacheHitTokens = $event.payload.provider_usage.cache_hit_tokens
        CacheMissTokens = $event.payload.provider_usage.cache_miss_tokens
    }
}
if ($found -eq 0) {
    throw "No Model.Generate request profiles found in '$($resolved.Path)' for RunId '$RunId'."
}
