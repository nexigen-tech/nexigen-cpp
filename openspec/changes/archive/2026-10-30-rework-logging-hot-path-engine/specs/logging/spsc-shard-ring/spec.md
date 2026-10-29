# Spec Delta

## Purpose

Defines the `SpscShardRing` wait-free ring buffer with `DropNewest` and `OverwriteOldest` overflow policies, per-slot sequence counters for TSan-clean producer/consumer handoff, and `dropped_count_` accounting, without modifying the existing `core::SpscRingBuffer` contract.

## ADDED Requirements

### Requirement: SpscShardRing overflow policies

The system SHALL provide `SpscShardRing<T, Capacity>` supporting two overflow policies: `DropNewest` (incoming record is dropped and counted when the ring is full) and `OverwriteOldest` (oldest unread record is overwritten atomically and the consumer index advances, `dropped_count_` incremented).

#### Scenario: DropNewest never blocks the producer

- **WHEN** the calling thread's shard ring is full and `overflow_policy == DropNewest`
- **THEN** the call returns immediately, previously enqueued records are intact, `dropped_count_` increases, and no thread blocked or retried in a loop

#### Scenario: OverwriteOldest is TSan-clean

- **WHEN** `overflow_policy == OverwriteOldest` and the producer overwrites the oldest unread record
- **THEN** there is no data race between the producer's write and the consumer's read (per-slot sequence counters or equivalent), and `dropped_count_` accurately reflects overwritten records

#### Scenario: Overflow policy is configurable at runtime

- **WHEN** `LoggerOptions::overflow_policy` is set before initialization
- **THEN** every shard ring in the pool uses the selected policy for all subsequent enqueues
