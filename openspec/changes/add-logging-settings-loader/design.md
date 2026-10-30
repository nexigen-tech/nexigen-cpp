# Design

## Context

See `proposal.md` — Why, and `specs/logging/logging-settings/spec.md` plus the `logging/file-logger` delta for requirements.

Current state that shapes this design:

- `LoggerOptions` (`include/nexigen/logging/logger_options.hpp`) is a plain aggregate with full `to_string` / `to_json` / `from_json` support, but no loader and no consumer.
- `ConsoleLogger<ShardCapacity, MessageCapacity, MaxThreads>::initialize(Config)` only takes an initial level; `FileLogger<...>::initialize(FileLogger_config)` takes app name, base dir, file name, level, throttle, rotation, and `core_mask`.
- Shard capacity, inline message capacity, and producer-slot count are **compile-time template parameters** of `detail::Producer` (`std::array<SpscRingBuffer<Record, ShardCapacity>, MaxThreads>`). `detail::SpscShardPool` is a runtime-sized placeholder that the producer does not use.
- Rotation is directory-per-period only (`open_period` in `src/logging/rotation.cpp`); the writer never looks at file size.
- Everything in the loader is cold path; the producer path must remain untouched.

## Goals / Non-Goals

**Goals:**

- One deterministic, explicit source pipeline: global JSON → local JSON → environment → `LoggerOptions`.
- Best-effort semantics with per-field diagnostics, testable through the existing zero-dependency logging test TU.
- Actually applied settings: level, app name, root directory, max file size, affinity, anti-spam, and explicit capacity checks.
- Size-based file rotation implemented on the writer thread without allocations.
- No change to the hot path, existing test expectations, or default `FileLogger_config` behaviour.

**Non-Goals:**

- Runtime-sized shard storage or a `Producer` rewrite (`SpscShardPool` integration) — separate change.
- Implicit config discovery (`/etc`, XDG, executable-adjacent scanning) — paths are caller-supplied only.
- Hot reload / file watching, remote config, or schema versioning.
- Removing the existing `Config` / `FileLogger_config` entry points.
- Applying `worker_poll_interval_us`, `sync_flush_on_critical`, or `overflow_policy` to the current engine — they are parsed and merged but remain inert until the producer rewrite; this is recorded as a diagnostic-free limitation in the docs rather than silently claiming support.

## Decisions

### D1 — Public surface: one loader, one result type, thin apply helpers

New header `include/nexigen/logging/logging_settings.hpp` (compiled unit `src/logging/logging_settings.cpp`):

```cpp
enum class Settings_source : std::uint8_t { Global, Local, Environment, Computed };

struct Settings_paths {
    std::string_view global_file{};  // empty = source disabled
    std::string_view local_file{};   // empty = source disabled
    std::string to_string() const;
};

struct Settings_diagnostic {
    Settings_source source{Settings_source::Global};
    std::string field;    // "app_name", "spam_protection.window_ms", ...
    std::string message;  // human-readable cause
    std::string to_string() const;
};

struct Logging_settings {
    LoggerOptions options{};
    bool global_loaded{false};
    bool local_loaded{false};
    bool ring_capacity_set{false};   // explicitly provided by any source
    bool max_threads_set{false};     // explicitly provided by any source
    std::vector<Settings_diagnostic> diagnostics{};
    std::string to_string() const;
};

[[nodiscard]] core::Status load_logging_settings(const Settings_paths& paths,
                                                 Logging_settings& out) noexcept;
```

`load_logging_settings` returns `Ok` when no diagnostic was recorded, `InvalidArgument` when any value/type/JSON problem was recorded, and `SystemError` when a supplied file exists but cannot be opened/read. The `out` parameter is always populated (best effort). Diagnostics are the detailed surface; the `Status` is the coarse gate so callers can log-and-continue.

Alternatives considered: returning `Result<Logging_settings>` (rejected because `std::expected` cannot carry both a best-effort value and an error); exposing a class per source (rejected: no state, only cold functions).

### D2 — JSON schema mirrors `LoggerOptions` keys

Both files use one flat object whose keys are the existing `LoggerOptions` field names (`log_root_directory`, `app_name`, `initial_log_level`, `max_file_size_bytes`, `spsc_ring_buffer_capacity`, `max_producer_threads`, `overflow_policy`, `flush_cpu_affinity`, `worker_poll_interval_us`, `sync_flush_on_critical`, `spam_protection`). This reuses the existing `from_json(LoggerOptions&)` that already ignores absent keys, which is what makes field-level merging fall out naturally.

- Unknown keys are ignored (forward compatibility, no diagnostic).
- `spam_protection` is a nested object; `flush_cpu_affinity` is an array of unsigned core ids.
- The global document's `app_name` key is erased before `from_json` runs, implementing the local-only rule without a second parser.
- Parsing uses `nlohmann::json::parse` under `JSON_NOEXCEPTION=1` and checks `is_discarded()`; no `try/catch` exists anywhere.
- Files are read with `::open`/`::read` into a `std::string` (cold path), missing file detected via `ENOENT` (not an error).

### D3 — Precedence by sequential overlay, with explicit presence tracking

`load_logging_settings`:

1. Copy default `LoggerOptions`.
2. If the global path is non-empty and readable: parse, strip `app_name`, `from_json` into `options`.
3. If the local path is non-empty and readable: parse, `from_json` into `options` (including `app_name`).
4. Apply environment overrides in a fixed order.
5. Run value validation (ranges, enum spellings, core ids) and record diagnostics.

Because `from_json` only touches keys present in the document, a missing local field cannot erase a global value. The two `*_set` flags are set whenever a key appears in any source (including a value that later fails validation); `apply_to` and the capacity check use them to avoid rejecting default values that exceed a logger's smaller template limits.

Environment variables (highest priority):

| Variable | Field | Grammar |
|---|---|---|
| `NEXIGEN_LOG_LEVEL` | `initial_log_level` | `from_string` names |
| `NEXIGEN_LOG_DIR` | `log_root_directory` | non-empty path |
| `NEXIGEN_LOG_APP_NAME` | `app_name` (local precedence) | non-empty, ≤ 63 bytes |
| `NEXIGEN_LOG_MAX_FILE_SIZE_BYTES` | `max_file_size_bytes` | decimal `uint64` |
| `NEXIGEN_LOG_CPU_AFFINITY` | `flush_cpu_affinity` | comma list of ids/ranges, e.g. `0,2-3`; ids 0–63 |
| `NEXIGEN_LOG_RING_CAPACITY` | `spsc_ring_buffer_capacity` | decimal `uint32` |
| `NEXIGEN_LOG_MAX_PRODUCER_THREADS` | `max_producer_threads` | decimal `uint32` |
| `NEXIGEN_LOG_OVERFLOW_POLICY` | `overflow_policy` | `from_string` names |

Parsing uses `std::from_chars` (no locale, no exceptions). Empty/unset variables are ignored.

### D4 — Diagnostics are data, not logs

Every problem becomes one `Settings_diagnostic` with the source, dotted field path, and a message. The loader never logs through the logging subsystem (chicken-and-egg: the logger is not initialized yet). The caller decides whether to fail startup, print to stderr, or continue. This keeps the loader usable by tests and by future CLI tooling.

### D5 — Applying settings is an explicit, lifetime-documented step

Apply helpers in the same header:

```cpp
[[nodiscard]] core::Status apply_to(const Logging_settings& settings,
                                    FileLogger_config& out) noexcept;

template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads>
[[nodiscard]] core::Status apply_to(const Logging_settings& settings,
                                    typename ConsoleLogger<ShardCapacity, MessageCapacity, MaxThreads>::Config& out) noexcept;

template <std::size_t ShardCapacity, std::size_t MaxThreads>
[[nodiscard]] core::Status check_capacity(const Logging_settings& settings,
                                          std::vector<Settings_diagnostic>& diagnostics) noexcept;
```

Applicability mapping:

- File: `initial_level`, `app_name`, `log_root_directory` → `base_dir`, `max_file_size_bytes`, `flush_cpu_affinity` → `core_mask` (bit *i* for core *i*; empty → 0), `spam_protection` → `Throttle_config` mapping as documented below.
- Console: `initial_log_level` only (the console writer is deliberately unpinned today).
- `apply_to(FileLogger_config&)` sets `std::string_view`s into `settings.options` storage; the contract states the settings object must outlive the config and the `initialize()` call. To remove the footgun for normal use, add `initialize(const Logging_settings&)` overloads on `FileLogger` and `ConsoleLogger` that keep the settings alive for the duration of the call and copy everything into fixed storage as today.
- The `check_capacity` result is used by the `initialize(const Logging_settings&)` overloads.

Spam/throttle mapping: `SpamProtectionConfig::max_messages_per_sec_per_shard` → `Throttle_config::max_records_per_window` with a 1 s window, only when the corresponding limit is non-zero (0 means unlimited in both models). Other spam fields (repeat suppression, byte budgets) are parsed and merged but stay inert until the producer rewrite; the docs list them explicitly as "parsed, not yet enforced" so the settings surface does not lie.

### D6 — Size-based rotation in the existing writer

`FileLogger_config` gains `std::uint64_t max_file_size_bytes{0}` (0 = disabled, default preserves current output). `detail::File_sink::Settings` gains the same field. `File_sink` gains two writer-only `std::uint64_t` fields: `segment_` and `bytes_written_`. No atomics are added — these are touched only by the background writer, exactly like `active_key_` and `pending_write_error_`.

Flow:

1. `start()` opens segment 0 as today, then `fstat(fd_)` seeds `bytes_written_ = st_size` (pre-existing content counts) and `segment_ = 0`.
2. A period-boundary switch resets `segment_ = 0` and seeds `bytes_written_` from the newly opened file (usually 0).
3. Before writing a rendered line of length `len`: if `max_file_size_bytes != 0 && bytes_written_ > 0 && bytes_written_ + len > max_file_size_bytes`, switch to segment `segment_ + 1`.
4. The switch opens the new segment *before* fsyncing and closing the old descriptor (same never-lose-a-line order as directory rotation), then seeds `bytes_written_` from the new file.
5. After a successful `write_all`, `bytes_written_ += len`.

A single line larger than the limit is written into an empty segment (checked by `bytes_written_ > 0`), so the writer cannot loop forever. The size check uses tracked counters only — no `fstat` per record, no allocation, no syscall unless a switch actually happens.

Segment naming is implemented in `detail::rotation` by extending the path builder: segment 0 keeps `<dir>/<file_name>`; segment *N* inserts `.<N>` before the last `.` of the file name (`app.jsonl` → `app.1.jsonl`), or appends `.<N>` when the name has no dot. The number is rendered into a 24-byte stack buffer with `std::to_chars`; all paths stay inside the existing pre-allocated `Rotation_scratch` (560-byte file buffer), and a name that would overflow returns `InvalidArgument` instead of truncating.

Alternatives considered: rotating after the write (rejected: overshoots the limit by one line); renaming the active file at the limit (rejected: breaks concurrent tail/read-back and complicates atomic switching); `fstat` per record (rejected: syscall on the writer hot loop).

### D7 — Mounting the loader without changing the hot path

`LoggerOptions` and the loader live only on the cold path. The producer path is untouched: no new fields in `detail::Producer`, no new atomics, no new branches in `emit()`. `File_sink::Settings` grows by 8 bytes and `File_sink` by 16 bytes of writer-private state, both inside the already `alignas(64)`-managed sink object; producer shards and cache-line separation are unchanged. There is no new shared mutable state, so no new memory orders are introduced; existing relaxed level/throttle snapshots keep their documented ordering.

### D8 — Tests and example

- Extend `tests/logging/logging_test.cpp` (single zero-dependency TU) with a `logging_settings` suite: missing files, malformed JSON, wrong types, field merge, app-name locality, env overrides, affinity grammar, diagnostics JSON/format, apply mapping, capacity checks, and size-segment rotation (start at limit, naming, pre-existing size, `0` disables, flush).
- Add `examples/logging/ex_logging_settings.cpp` and register it in `examples/CMakeLists.txt` + CTest smoke label. It writes a temporary global/local pair, loads with env override, applies to `FileLogger_config`, initializes, logs, flushes, verifies the segment/file contents, and exits 0.

## Risks / Trade-offs

- **Default `LoggerOptions` capacities exceed current logger template limits** → explicit-presence flags (`ring_capacity_set`, `max_threads_set`) make only user-provided values checked; defaults never reject a smaller logger.
- **`apply_to` returns views into the settings object** → documented lifetime plus `initialize(const Logging_settings&)` overloads that do not leak the views.
- **Settings surface promises fields the engine does not yet enforce** (`worker_poll_interval_us`, `sync_flush_on_critical`, `overflow_policy`, part of anti-spam) → parse and merge still happen, but README and the design explicitly mark them as not-yet-enforced; the producer rewrite is scoped as the follow-up change.
- **Size check can still be off by one writer batch** because bytes are tracked after a successful `write_all`; a partial-then-failed write under-counts until the next attempt → acceptable: the error is surfaced through `flush()` and the next successful write re-syncs the counter.
- **`mkdira`/`openat` failures on a segment switch** → the old descriptor stays active and later lines continue there; `flush()` reports `SystemError`, and rotation retries at the next size check, mirroring the existing directory-rotation policy.
- **Environment parsing without exceptions** → `std::from_chars` and manual range parsing; invalid text becomes a diagnostic, never a crash.

## Migration Plan

Additive: new header/source/example, new defaulted config fields, existing `initialize(...)` signatures unchanged. Existing users keep current behaviour (size limit 0, rotation unchanged). Rollback: delete the new files and revert the `FileLogger_config`/`File_sink`/rotation edits; no on-disk format changes occur when the size limit is 0.

## Open Questions

None blocking. The follow-up producer rewrite will decide whether `worker_poll_interval_us`, `sync_flush_on_critical`, `overflow_policy`, and the remaining spam quotas become enforced or are dropped from `LoggerOptions`.
