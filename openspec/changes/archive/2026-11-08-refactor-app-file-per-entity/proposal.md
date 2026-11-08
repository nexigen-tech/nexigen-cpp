# Proposal
## Why
`command_line.hpp`, `app_settings.hpp`, `telemetry_layout.hpp` hold multiple structs each. Split for file-per-entity.
## What Changes
- command_line → 3 entity headers + trimmed command_line.hpp.
- app_settings → app_settings_paths.hpp + trimmed app_settings.hpp.
- telemetry_layout → run_identity.hpp + telemetry_paths.hpp + trimmed telemetry_layout.hpp.
## Capabilities
Pure refactor — skip_specs: true.
## Impact
Header-only reorganization; no behavior change.
