# Proposal

## Why

`RS_SAF_10002` asks for verification of software and configuration data. Executables
and manifests are already digest-gated, but runtime settings files
(`logging.json`, `metrics.json`, `tracing.json`, `chrono.json` under the global
and local settings directories) are read without any integrity check. The
traceability matrix records that gap on `RS_SAF_00002` and `RS_SAF_10002`.

## What Changes

- The suite manifest gains `settings_digests`: a list of
  `{path, sha256}` entries for settings files. Paths are absolute; digests are
  lowercase or uppercase SHA-256 hex.
- Manifest validation rejects a relative path or a malformed digest.
- The detached suite supervisor verifies every listed file with the existing
  single-read `read_verified` helper before it launches any application. A
  mismatch reports `SecurityError` through the start pipe and no process starts.
- `RS_SAF_10002` becomes `supported`; `RS_SAF_00002` keeps `partial` because the
  configuration is verified at launch, not re-verified during operation.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `exec/app-suite`: a new manifest section and a pre-launch verification step.

## Impact

- **Code**: `SettingsDigest` value type, manifest field/validation/serialization,
  `verify_settings_digests`, one check in `start_suite_and_report`.
- **Performance**: cold path only — one file read and one SHA-256 per listed
  file at suite start. Nothing on a tick, producer, or sink path.
- **Memory**: the digests list and the read buffers, freed after the check.
- **Compatibility**: additive. A manifest without the section behaves exactly as
  before. Verification happens once, before the fork; the application still
  reads the files itself, the same trust model as the executable digest gate.
