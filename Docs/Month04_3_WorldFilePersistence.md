# Month 04.3 World File Persistence

This milestone adds the durable file boundary around the validated World asset
format and transactional runtime reconstruction.

## Public Flow

The complete persistence path is:

```text
PWorld
  -> CaptureWorld
  -> FWorldAssetData
  -> FMemoryWriter
  -> .pworld
  -> FMemoryReader
  -> FWorldAssetData
  -> CreateWorldFromAssetData
  -> new PWorld
```

`SaveWorldToFile` captures and serializes the complete World before opening the
destination. It writes to a sibling `.tmp` file, flushes and closes that file,
then replaces the destination. Replacement cleanup removes `.tmp` and `.bak`
files after success.

`LoadWorldFromFile` reads at most 256 MiB into memory. It rejects malformed,
unsupported, truncated, oversized, and trailing data before reconstructing
runtime objects.

## Error Surface

World persistence reports:

- `FileOpenFailed`
- `FileReadFailed`
- `FileWriteFailed`
- `FileTooLarge`
- `TrailingData`

Archive, validation, construction, property, relationship, and `PostLoad`
errors continue to retain their more specific existing values.

## Safety Properties

- Saving identical Worlds produces identical file bytes.
- Replacing an existing file preserves the latest complete save.
- Failed parsing creates no runtime objects.
- Failed reconstruction destroys the complete temporary World.
- A top-level World name conflict leaves the existing World untouched.
- Successful replacement leaves no temporary or backup file.

## Deferred Work

EngineLoop active-World replacement is implemented by
`Month04_4_EngineWorldReplacement.md`.

The editor still holds its own World handle. Editor Save/Open commands,
selection reset, and renderer refresh remain the next milestone.
