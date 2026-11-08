# Proposal
## Why
`app_suite.hpp` holds 3 enums + 7 structs; `instance_lifecycle.hpp` holds 1 enum + 2 structs.
## What Changes
Split the standalone enums: ImpactOnAppSuiteOperability, SuiteState, AppState, TerminatedBy into their own headers.
## Capabilities
Pure refactor — skip_specs: true.
## Impact
Header-only; no behavior change.
