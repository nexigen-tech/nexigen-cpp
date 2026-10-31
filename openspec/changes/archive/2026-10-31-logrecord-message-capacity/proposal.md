# Proposal

## Why
The async logging engine already migrated `ShardCapacity` and `MaxThreads` from compile-time template parameters to runtime `LoggerOptions` fields, but `MessageCapacity` — the inline argument blob size on `LogRecord` — remains compile-time only, pinned by the `LogRecord<MessageCapacity>` template parameter and the `NEXIGEN_LOG_MESSAGE_CAPACITY` macro. Operators therefore cannot tune per-record argument capacity without a rebuild, unlike `spsc_ring_buffer_capacity` and `max_producer_threads`.

## What Changes
- Add a runtime `message_capacity` field to `LoggerOptions` (default `128`, matching the current `NEXIGEN_LOG_MESSAGE_CAPACITY` default).
- Add a `message_capacity_set` explicit-presence flag to `Logging_settings` so only explicitly configured values participate in the compile-time capacity check.
- Extend `check_capacity` to validate an explicitly configured `message_capacity` against the logger's compile-time `MessageCapacity` bound, rejecting over-limit values with `InvalidArgument` and a diagnostic (no silent truncation).
- Add the `NEXIGEN_LOG_MESSAGE_CAPACITY` environment override and the `message_capacity` JSON field to the settings loader.
- No breaking changes: the `MessageCapacity` template parameter and macro remain as the hard upper bound; the runtime value is validated against it.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `logging/logger-options`: add a runtime-configurable `message_capacity` field to `LoggerOptions`.
- `logging/logging-settings`: add the `message_capacity_set` presence flag, the `NEXIGEN_LOG_MESSAGE_CAPACITY` override, and message-capacity validation to `check_capacity`.

## Impact
- **Performance/latency**: cold-init path only — `message_capacity` is read once at `initialize()` and never on a producer path, so the ≤ 20 ns wait-free enqueue budget and the ≈ 1 ns filtered-call latency are unchanged.
- **Memory allocation**: zero new allocations; capacity is bounded by the compile-time `MessageCapacity`, and a runtime value above that bound is rejected rather than allocated.
- **Code/APIs**: `include/nexigen/logging/logger_options.hpp`, `include/nexigen/logging/logging_settings.hpp`, `src/logging/logger_options_format.cpp`, `src/logging/logging_settings.cpp`, and the `check_capacity` call sites in `console_logger.hpp` / `file_logger.hpp`.