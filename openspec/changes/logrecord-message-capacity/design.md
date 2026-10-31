# Design

## Context

`LogRecord<MessageCapacity>` (include/nexigen/logging/log_record.hpp) is an `alignas(64)` trivially-copyable value whose inline argument blob is `std::array<char, MessageCapacity>`. `MessageCapacity` is compile-time only, defaulted via the `NEXIGEN_LOG_MESSAGE_CAPACITY` macro (128). `LoggerOptions` (logger_options.hpp) already carries runtime `spsc_ring_buffer_capacity` and `max_producer_threads` but no message capacity; `Logging_settings`/`check_capacity<ShardCapacity, MaxThreads>` (logging_settings.hpp) validate only those two against the loggers' template bounds, and `check_capacity` is called from `ConsoleLogger`/`FileLogger::initialize(const Logging_settings&)`. This mirrors exactly the existing "runtime value checked against compile-time bound" pattern, so message capacity is added the same way.

## Goals / Non-Goals

**Goals:**
- Add a runtime `message_capacity` field to `LoggerOptions` and thread it through the settings loader and capacity check, matching the existing `spsc_ring_buffer_capacity` flow.
- Reject (rather than truncate) a runtime value exceeding the logger's compile-time `MessageCapacity`.
- Keep the change cold-init only with zero hot-path or allocation impact.

**Non-Goals:**
- Making `LogRecord`'s inline blob itself dynamically sized (the `std::array<char, MessageCapacity>` layout stays fixed).
- Removing the `MessageCapacity` template parameter, the macro default, or the `message_capacity()` accessor.
- Changing producer enqueue semantics, drop counters, or rendering.

## Decisions

- **Field type and default**: `std::uint32_t message_capacity{128U}` on `LoggerOptions` — same width as `spsc_ring_buffer_capacity`, default matches `NEXIGEN_LOG_MESSAGE_CAPACITY`. No `bit_ceil` rounding (a byte capacity has no power-of-two requirement, unlike the SPSC ring). The runtime value is a *soft* cap bounded by the compile-time `MessageCapacity`; it is validated, never used to resize anything.
- **Presence tracking**: add `bool message_capacity_set{false}` to `Logging_settings`, set `true` only when the `message_capacity` JSON field or `NEXIGEN_LOG_MESSAGE_CAPACITY` environment variable supplies a value — identical to `ring_capacity_set`/`max_threads_set`, so defaults never reject a smaller logger.
- **Capacity check**: widen `check_capacity` to `check_capacity<ShardCapacity, MaxThreads, MessageCapacity>` and add a branch: if `message_capacity_set && options.message_capacity > MessageCapacity`, return `InvalidArgument` and push a `Computed` diagnostic naming both values. Update the two logger call sites to pass `MessageCapacity`.
- **Loader plumbing**: extend `sanitize_document` with a `message_capacity` unsigned field, and `apply_environment` with `NEXIGEN_LOG_MESSAGE_CAPACITY` (positive 32-bit integer, same parse path as the ring capacity), each setting `message_capacity_set`.
- **Serialization**: `message_capacity` participates in `LoggerOptions` `to_string`/`to_json`/`from_json`; `message_capacity_set` participates in `Logging_settings` `to_string`/`to_json` (no `from_json`, matching the existing contract).
- **Concurrency/memory-order**: none. `message_capacity` is read once at `initialize()` and is immutable afterward (no setters), so no atomics, no `std::memory_order`, and no cache-line layout changes. `LogRecord` keeps its existing `alignas(64)` layout and per-thread SPSC shard sizing; this change introduces no concurrent or atomic component.

## Risks / Trade-offs

- [Configuring less than the compile-time bound reduces the inline blob] → The smaller runtime value is the operator's explicit choice; it only affects how much serialized argument data survives before truncation, and is surfaced via the existing `args_truncated()` flag rather than silently.
- [Configuring more than the compile-time bound] → Rejected at `initialize()` with `InvalidArgument` + a `Computed` diagnostic naming both values, preventing any silent memory growth; the logger's fixed `MessageCapacity` layout is never exceeded.
- [Widening `check_capacity`'s template signature] → Compile-time-only source change at the two logger call sites; existing test instantiations (`check_capacity<128, 32>`) are updated to pass the third parameter, so no runtime behavior changes for non-message-capacity checks.

## Migration Plan

1. Defaults are unchanged (`message_capacity` = 128), so existing configs and loggers initialize identically.
2. `message_capacity` is optional: absent from JSON/env, the field keeps its default and is never checked. Configs that set it below the compile-time bound apply immediately; those above the bound fail initialization with a diagnostic (operator lowers it or raises the logger's `Message_capacity` template parameter and rebuilds).
3. Rollback is drop-in: removing the `message_capacity` field/override restores prior behavior with no data or schema migration.