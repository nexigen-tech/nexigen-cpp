# logging/spsc-shard-pool delta spec

## REMOVED Requirements

### Requirement: Pre-allocated shard pool

**Reason**: Dead code beside the live engine (analysis 2026-11-03 #7):
`nexigen::core::ShardPool` already fulfills this role for the logging producer
with verified wait-free invariants; `SpscShardPool` was never wired into any
sink or test and drifts from the real contract (Free-slot recycling untested,
`mark_draining`/`release_shard` unreachable).
**Migration**: None external. The live pool semantics (allocation, counting,
TLS registration) are owned by `logging/multi-producer` and the shared
`nexigen::core::ShardPool` header contract.

### Requirement: Thread-local slot registration and pool exhaustion

**Reason**: Same dead-code removal as "Pre-allocated shard pool" above; the
spec contradicts the shipped live behavior (`core::ShardPool::claim_slot`).
**Migration**: None.

### Requirement: Shard Draining and pool return

**Reason**: Same dead-code removal; the draining/return lifecycle never mediates
a real record path today.
**Migration**: None.
