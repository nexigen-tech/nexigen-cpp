# Proposal
## Why
The policy/span enum file holds multiple enums; split each into its own header.
## What Changes
Split enums into per-enum headers (forwarding header retained for compatibility).
## Capabilities
Pure refactor — skip_specs: true.
## Impact
Header-only; no behavior change.
