# logging/spsc-shard-ring delta spec

## REMOVED Requirements

### Requirement: SpscShardRing overflow policies

**Reason**: The described machinery is dead code — the live producer engine
uses `nexigen::core::ShardPool` (analysis 2026-11-03 #7), and
`SpscShardRing` was never wired: the seq-counter contract the spec promised
was not implemented, and keeping an unwired duplicate "worse than absence" was
the agreed remedy. The live overflow/drop-accounting requirements live in
`logging/multi-producer`.
**Migration**: No external migration — no production or test code referenced
the header. Future per-shard `OverwriteOldest` support, if ever needed, is to
be designed against `core::ShardPool` rather than a parallel engine.
