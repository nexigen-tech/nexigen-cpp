# Proposal

## Why

Every asynchronous logger and tracer runs a background `std::jthread` that busy-polls its shard rings and, when idle, sleeps 200 µs between sweeps. On this machine that is ~4 000 wake-ups/s per idle worker, and a deploy that pins many applications' loggers/tracers to one or two shared cores saturates those cores with context switches while nothing is being logged: measured 8% of a core at 16 idle loggers and 24% at 48, with user CPU under 1% (all the cost is wake-ups, not work). The poll interval is already in `LoggerOptions` (`worker_poll_interval_us`, default 1000 µs) but the engine never reads it, and the tracing consumer has no such knob at all.

## What Changes

- Add `include/nexigen/core/worker_tuning.hpp` (`nexigen::core::WorkerTuning`) — one shared, header-only policy for the idle backoff of background consumers: a short spin budget, a geometric sleep (100 µs, 200 µs, …) up to a `poll_interval_us` ceiling, and a widened per-worker timer slack so co-located idle workers coalesce.
- Replace the hard-coded idle behaviour in the logging `detail::AsyncWorker` (64 failed sweeps then `sleep_for(200 µs)`) and the tracing `TracerProvider::consume_loop` (same constants) with `WorkerTuning`, driven by a per-consumer ceiling.
- Add `worker_poll_interval_us` (default 1000, clamped to 50–100 000) to `FileLoggerConfig`, `ConsoleLoggerConfig`, and `TracerProviderConfig`; wire it from `LoggerOptions` / `logging::Settings` and from the tracing settings file + config serialization.

## Capabilities

### New Capabilities
- `core/worker-tuning`: the shared idle-backoff policy (`WorkerTuning`) for background consumers — spin budget, geometric sleep to a poll ceiling, and timer-slack widening — with the documented defaults and clamp contract.

### Modified Capabilities
- `logging/file-logger`: the background writer's idle poll uses the shared tuning policy with a configurable ceiling; the config vocabulary renders the new field.
- `logging/console-logger`: the background writer's idle poll uses the shared tuning policy; the config vocabulary renders the new field.
- `logging/logger-options`: `worker_poll_interval_us` moves from a parsed-but-inert field to one the logger actually applies.
- `logging/logging-settings`: the apply helpers transfer the poll interval into the logger configs and the loader accepts an environment override.
- `tracing/tracer-provider`: the background consumer's idle poll uses the shared tuning policy with a configurable ceiling; config serialization round-trips the field.
- `tracing/tracing-settings`: the settings loader reads `worker_poll_interval_us` from the file.

## Impact

- **Code**: new `include/nexigen/core/worker_tuning.hpp` (~60 loc); `detail/async_logger.hpp` (idle loop + `set_tuning`); `file_logger.hpp` / `console_logger.hpp` (config field + plumbing); `tracer_provider.hpp` (consume_loop + config field); `src/logging/logging_settings.cpp` (apply); `src/tracing/record_format.cpp` (to_string/JSON); `src/tracing/tracing_settings.cpp` (file field).
- **API**: additive — new config fields default to 1000 µs, so existing callers are unchanged. `WorkerTuning` is header-only; no new link dependency.
- **Performance / latency / allocation impact**: zero allocation anywhere on the path (constants + `prctl` only). Producer hot path is untouched (the "never wait" guarantee is unchanged). The only behavioural change is consumer-side idle wake-ups: from ~4 000/s to ~700/s at the default 1 ms ceiling, and fewer still at larger ceilings. First-record-after-idle latency becomes the backoff floor (100 µs) and flush latency stays bounded by the poll ceiling.
- **Tests**: extend `tests/logging/logging_test.cpp` and `tests/tracing/tracing_test.cpp` with idle-wake-up counters (via `/proc/self/task/<tid>/status`), config round-trips, and a flush-under-long-poll check.
- **Docs**: README logging note (drop the "parsed but not enforced" caveat, document the ceiling), tracing settings + tracer-provider specs.
