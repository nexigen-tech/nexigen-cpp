# Design

## Context

See `proposal.md` for motivation. `nexigen-run` is a cold-path CLI plus a detached suite supervisor; application instances already have their own logs and JSONL streams. The runner's current diagnostics are `fprintf` to stderr, exit codes, and the suite state files.

`nexigen::logging::FileLogger` is an asynchronous, non-movable logger with a background `std::jthread`, per-thread SPSC producer shards, append-only file opens (`O_APPEND`), JSON-lines formatting, and optional rotation. `nexigen-run` currently forks in two places: `Runner::start` creates a per-instance supervisor, and `start_suite` creates the suite supervisor. A logger worker thread must not survive a fork.

The suite manifest already carries the suite-level `log_level` and `global_settings_path`; `logging.json` in that directory defines the application logging pipeline's level, affinity, size limit, and throttle. Per-application `log_level` applies only to the generated application settings. The suite manifest does not yet carry runner-thread affinity, and `runner.json`/`suite.json` do not record the runner CPU lists.

## Goals / Non-Goals

**Goals:**

- Durable `runner.jsonl` for CLI commands and the detached suite supervisor.
- Deterministic path and level selection with a working-directory fallback.
- Operational logger settings from the suite global `logging.json`, with the manifest level winning.
- Runner control-thread and logger-worker affinity without affecting application affinity.
- Fork-safe logger lifecycle around every runner-initiated fork.
- Logging failures and affinity rejections never affect command results or supervision policy.
- Minimal, bounded memory and no application hot-path work.

**Non-Goals:**

- No change to application logging, generated application settings, suite layout, or CLI syntax.
- No scheduled rotation, compression, retention policy, or remote/network shipping; size segmentation from `logging.json` is allowed.
- No structured event schema beyond the `FileLogger` JSON line.
- No logging for `--help` or option-parsing failures that happen before a location is resolved.
- No logging from the per-instance supervisors; the suite supervisor owns policy events.

## Decisions

### D1. Free-function logging facade, no new public type

`include/nexigen/runner/runner_logging.hpp` exposes a small control surface over a process-local logger:

```cpp
namespace nexigen::runner {
core::Status init_runner_logging(std::string_view primary_dir,
                                 const logging::Logging_settings& settings,
                                 logging::LogLevel level) noexcept;
void suspend_runner_logging() noexcept;
core::Status resume_runner_logging() noexcept;
core::Status flush_runner_logging() noexcept;
void log_runner(logging::LogLevel level, std::string_view message) noexcept;
[[nodiscard]] bool runner_logging_active() noexcept;
void shutdown_runner_logging() noexcept;

core::Status apply_runner_affinity(std::span<const std::uint32_t> cpus,
                                   std::string_view context) noexcept;
}
```

The implementation keeps one `std::unique_ptr<FileLogger<64, 256, 4>>` plus the chosen base directory, level, resolved logger mask, size limit, and throttle in `runner_logging.cpp`. No public class or struct is introduced, so there is no new serialization surface. Four producer shards are enough for the CLI and single-threaded supervisor; the worker thread is additional. The fixed 64-record shards and 256-byte message capacity bound memory to tens of KiB.

`init_runner_logging` configures:

- `app_name = "nexigen-run"`;
- `file_name = "runner.jsonl"`;
- `Rotation_config{.kind = Rotation_schedule::None}`;
- `initial_level = level`;
- `max_file_size_bytes = settings.global_loaded ? settings.options.max_file_size_bytes : 0`;
- `core_mask` from the resolved runner-logger CPU list, else from `settings.options.flush_cpu_affinity`;
- throttle from `settings.options.spam_protection` when the file was loaded.

It tries the primary directory first and then `<cwd>/.nexigen-run`. A failed initialization leaves logging inactive; callers continue.

`log_runner` is a no-op when inactive. Formatting and file I/O happen on the FileLogger worker, not on the caller.

`apply_runner_affinity` validates and applies a CPU list to the calling thread with `pthread_setaffinity_np`, returning success only when the kernel accepted the full set. A duplicate, out-of-range, or rejected set returns `InvalidArgument` or `SystemError`; the caller turns that into a warning and leaves the thread unconstrained.

### D2. Path and level resolution

The CLI resolves a log target before initializing logging:

- `--config` with a valid manifest: primary `<artifacts_path>/<suite_id>`, global settings file `<global_settings_path>/logging.json`, level `manifest.log_level` when present, otherwise the file's `initial_log_level`, otherwise `Info`.
- `--suite-dir` with readable `suite.json`: primary from the document's `artifacts_path` and `suite_id`, global settings from its recorded `global_settings_path`, control/logger CPU lists from its recorded runner fields, level from the file's `initial_log_level` (no manifest level), otherwise `Info`.
- Anything else (single-instance commands, missing/invalid manifest, unreadable suite metadata): primary `<cwd>/.nexigen-run`, no global settings file, level `Info`, no affinity.

The supervisor always has the validated manifest and therefore uses `<artifacts_path>/<suite_id>`, `<global_settings_path>/logging.json`, the suite level, and the manifest CPU lists directly.

`suite.json` therefore gains `runner_control_cpus` and `runner_logger_cpus` in addition to the existing `global_settings_path`, so `--suite-dir` can reproduce the same affinity and settings behavior without the manifest. It still does not persist `log_level`, because the file is the no-manifest source and the manifest level is only available with `--config`.

### D3. Global logging settings

The runner loads `<global_settings_path>/logging.json` through `logging::load_logging_settings` with only the global file path populated; there is no local settings layer for the runner. The logging pipeline's environment overrides still apply as defined by `logging/logging-settings`.

Mapping into the runner logger:

- **Level fallback:** `initial_log_level`; the manifest suite `log_level` overrides it.
- **Logger affinity fallback:** `flush_cpu_affinity`; manifest `runner_logger_cpus` overrides it.
- **Size segmentation:** `max_file_size_bytes` only when the file was actually loaded. With no file the runner uses `0`, not the `LoggerOptions` default of 100 MiB.
- **Throttle:** `spam_protection` maps to the FileLogger per-second budget when the file was loaded.
- **Fixed runner fields:** `app_name = "nexigen-run"`, `file_name = "runner.jsonl"`, scheduled rotation `None`, and the runner log directory are never taken from the file.
- **Compile-time capacities:** the `FileLogger<64, 256, 4>` instantiation is fixed; `spsc_ring_buffer_capacity`, `message_capacity`, and `max_producer_threads` from the file are not applied. This is why the spec only claims level, affinity, maximum size, and throttle.

`load_logging_settings` is best-effort for diagnostics: its status may be `InvalidArgument` while `out` is still populated. An unreadable or invalid file produces a warning and the runner falls back to the manifest level, no logger pin, zero size limit, and no throttle without failing the command.

### D4. Runner thread affinity

The suite manifest gains two additive fields:

```json
{
  "runner_control_cpus": [0, 1],
  "runner_logger_cpus": [2]
}
```

- `runner_control_cpus` pins the CLI control thread for `stop`/`status` (and `start` before its fork, see below) and the suite-supervisor main thread.
- `runner_logger_cpus` pins the FileLogger worker through `FileLogger_config.core_mask`. It falls back to `flush_cpu_affinity` from the global logging settings when the manifest list is empty.
- Both lists are soft configuration: duplicates, out-of-range indices, values above the FileLogger's 0–63 mask limit, or a kernel rejection produce a warning and disable that pin. They do not invalidate the manifest.

`apply_runner_affinity` builds a `cpu_set_t`, checks indices against `_SC_NPROCESSORS_ONLN` and `CPU_SETSIZE`, and calls `pthread_setaffinity_np` on the calling thread. The logger mask is a separate 64-bit value because `FileLogger` accepts only cores 0–63.

Fork inheritance is the critical constraint:

- For `start`, the CLI applies control affinity before `Runner::start`/`start_suite` because no logger worker exists yet. The forked suite supervisor resets itself to all online CPUs before applying its own control pin; per-instance supervisors and application children also reset to all online CPUs, and an application with explicit `cpus` then applies that list.
- `stop` and `status` apply control affinity after resolving the manifest/`suite.json`, before executing the command.
- The suite supervisor applies control affinity only after its own reset; every `Runner::start` child resets to the full online set before doing registry/resource work, so neither a per-instance supervisor nor an application inherits the runner's control pin.
- The logger worker pin is owned by `FileLogger` and is re-applied on every `init_runner_logging` after a fork suspension.

Warnings are written to stderr while logging is inactive and to `runner.jsonl` once it is active.

### D5. CLI command ordering

`start` must not initialize a logger before `Runner::start` or `Runner::start_suite` forks. It therefore resolves the manifest, applies control affinity (no logger worker exists yet), executes the start, then loads logging settings, initializes logging with the already-resolved target, records the result, and flushes. If the manifest is invalid, it initializes the fallback target with `info`, no affinity, and records the parse failure.

`stop` and `status` do not fork, so they resolve the target and its recorded settings, initialize logging, apply control affinity to the CLI thread, record the command, execute it, record the result, and flush. `--help` and usage failures before target resolution remain stderr-only.

This ordering guarantees that no CLI process has a logger worker thread at the moment it forks, while still giving the short-lived CLI the requested control affinity.

### D6. Suite supervisor integration and fork suspension

`run_suite_supervisor` first resets its inherited affinity to all online CPUs, then applies `runner_control_cpus` (warning on failure). It launches the initial application attempts before initializing logging: the initial launches are the only forks that happen before a logger exists. It then loads the global logging settings and initializes logging with `runner_logger_cpus` or the file fallback, records `suite_started`, and records each application's initial state.

After initialization, every path that forks calls `suspend_runner_logging()` first. Suspension flushes the producer rings, destroys the `FileLogger`, and joins its worker thread; the parent then forks, and `resume_runner_logging()` creates a new logger with the stored directory, level, size limit, throttle, and logger mask. This covers:

- per-application retries (`launch_app` for one application);
- whole-suite relaunches (`launch_all`);
- any future call to `Runner::start` from the supervisor.

`Runner::stop` and `stop_all` do not fork and keep logging active. Before `_exit`, the supervisor records the final event, calls `shutdown_runner_logging()` (flush plus join), and only then exits. This matters because `_exit` bypasses destructors.

### D7. Event vocabulary

Messages are short `key=value` strings so the FileLogger JSON line stays the single structured envelope. The minimum vocabulary:

| Event | Emitted when | Keys |
|---|---|---|
| `command` | CLI command completes | `command`, `mode`, `target`, `result`, optional `detail` |
| `suite_started` | supervisor has launched first attempts | `suite`, `apps` |
| `app_launch` | attempt started | `app`, `instance`, `attempt`, `pid` |
| `app_launch_failed` | launch returned an error | `app`, `instance`, `attempt`, `error` |
| `app_exit` | an `exit.json` is observed | `app`, `instance`, `attempt`, `exit_code` or `signal` |
| `retry_scheduled` | per-app retry scheduled | `app`, `instance`, `attempt`, `impact`, `delay_s` |
| `app_failed` | retries exhausted | `app`, `instance`, `impact`, `suite_state` |
| `suite_restart` | whole-suite restart scheduled or started | `trigger`, `used`, `limit`, `delay_s` |
| `stop_requested` | `stop.request` observed | `suite` |
| `suite_stopped` | normal or forced stop recorded | `suite`, `reason` |
| `suite_failed` | restart budget exhausted | `suite`, `reason` |
| `affinity_warning` | a runner pin is disabled | `thread`, `cpus`, `reason` |

Messages are bounded; the 256-byte message capacity is sufficient for these keys with normal identifiers. The FileLogger app field distinguishes runner records from application records.

### D8. Concurrency, memory, and atomicity

No new concurrent component is introduced. The FileLogger already defines its producer shards, worker thread, memory orders, and fixed buffers; the facade calls it only from the single control thread of the CLI or supervisor. The chosen instantiation is `FileLogger<64, 256, 4>`.

Control affinity uses a per-thread `cpu_set_t` only; it shares no atomic state with the logger. The logger worker affinity is `FileLogger_config.core_mask` (cores 0–63), while the control thread supports the full `CPU_SETSIZE` range.

The CLI parent and the suite supervisor can both append to the same `runner.jsonl`. File opens use `O_APPEND`, and each event is one short line, so interleaving cannot split a line in normal operation. Ordering between the two processes is not guaranteed and is not required; the FileLogger timestamp orders records.

`alignas(64)` and `std::memory_order` choices are owned by the existing FileLogger implementation; this change adds no atomic state of its own. The only shared mutable state is the process-local logger pointer, accessed by one control thread.

One implementation prerequisite surfaced during design: the logging producer's thread-local slot cache is keyed by producer address. A logger destroyed and re-created at the same heap address could otherwise return the old cached slot without publishing the new producer's registration, leaving the worker with nothing to drain. The implementation clears a stale cache entry when its slot is not published and re-registers; this makes repeated initialize/suspend/resume cycles correct without changing the steady-state hot path.

### D9. Failure and fallback behavior

- Primary initialization failure: retry with `<cwd>/.nexigen-run`.
- Fallback initialization failure: logging stays inactive; one stderr warning is printed and the command proceeds.
- Invalid global logging settings: warning, keep the manifest level, no logger pin, zero size limit, no throttle.
- Invalid or rejected affinity: warning, disable that pin, continue; never fail the command or supervision.
- Runtime write/flush failure: ignored for command and policy outcomes; FileLogger's own drop/error accounting remains available to the process.
- `suspend_runner_logging` destroys the logger even when `flush()` reports an error, because keeping a worker thread across a fork is worse than losing a record.
- `shutdown_runner_logging` is idempotent.

### D10. Migration and rollback

Additive only. Existing commands keep their output and exit codes; a new `runner.jsonl` appears in the suite artifact directory or the fallback. Rollback removes the logging calls; existing log files remain as inert data.

## Risks / Trade-offs

- **Fork with an asynchronous logger can deadlock the child** → Logging is explicitly suspended and joined around every fork after initialization; the CLI never initializes before its start fork.
- **A pinned control thread is inherited by forked children** → Suite-supervisor, per-instance-supervisor, and application children reset to all online CPUs; only explicit application `cpus` narrows an application.
- **Two processes append to one file** → `O_APPEND` plus one short line per event; ordering is timestamp-driven, not guaranteed.
- **No scheduled rotation, but logging.json may set a size limit** → Size segmentation can create numbered files; without a file limit the log grows without bound, and retention is an operator concern.
- **Fixed logger capacities ignore some logging.json fields** → The spec claims only level, affinity, maximum size, and throttle; `spsc_ring_buffer_capacity`, `message_capacity`, and `max_producer_threads` are runner-specific and fixed by the template instantiation.
- **Dropped records under bursts** → FileLogger's bounded rings may drop; runner events are low-rate and the FIFO guarantee per producer is sufficient. A drop summary can appear in the file.
- **Fallback logs are scattered in the working directory** → The fallback is deterministic (`<cwd>/.nexigen-run`) and only used when the manifest cannot provide an artifact path.
- **`--suite-dir` cannot recover the manifest level** → It uses the recorded global `logging.json` level, then `info`; use `--config` when the manifest `log_level` must win.
- **Invalid affinity silently loses determinism** → A warning is emitted and the thread stays unconstrained; a valid manifest plus available cores restores the pin.
- **Logging failure hides diagnostics** → Commands still use stderr, exit codes, and state files; the logger is additive, not the sole control surface.

## Open Questions

None. Rotation, retention, and structured event schemas are explicit non-goals and do not affect the task breakdown.
