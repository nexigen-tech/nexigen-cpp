# Design

## Context

See `proposal.md` — Why. Today each subsystem owns a piece of the output contract but not the whole path:

- `app::Metrics_settings` and `app::Tracing_settings` wrap only provider config (`MetricProvider_config`, `TracerProvider_config`); `chrono::Chrono_settings` wraps only `ChronoProvider_config`.
- The JSONL sinks already have configuration values (`metrics::JsonFileMetricSink_config`, `tracing::JsonFileTraceSink_config`, `chrono::JsonFileChronoSink_config`), but an application's policy hook constructs them from hard-coded constants.
- `Infrastructure::initialize(const App_settings&)` passes the three settings values to `Policy::emplace_*`, so a policy hook can read new sink members without any change to `Infrastructure`.
- No application identity exists at runtime: metrics, traces, and chrono records contain no app/run/instance fields, and the only environment variables consumed are per-provider overrides.

Constraints that shape the approach:

- All new work is initialization-time. `nlohmann::json`, `std::string`, and filesystem queries are forbidden on producer, tick, snapshot, and sink hot paths.
- Storage stays file-based JSONL, one record per line; this change does not alter record layouts.
- `nexigen-cpp` is the first language implementation; the contract must be expressible in C# and Python without C++-specific types.
- The loader must stay best effort under `-fno-exceptions` / `JSON_NOEXCEPTION=1`: bad input becomes a diagnostic, never an abort or a partial state.

## Goals / Non-Goals

**Goals:**

- Make the three sink paths settable through the existing settings pipeline with the same merge and diagnostic behavior as provider fields.
- Define one environment identity contract and one resolver that turns identity plus settings into the four-stream output layout a local observatory can tail.
- Pin serialization so settings and resolved layout can be logged, diffed, and asserted in tests.
- Keep the existing `Infrastructure`/policy API shape intact: only settings values and policy hook bodies change.

**Non-Goals:**

- Implementing `nexigen-hub` or `nexigen-run`. Their layout, `resource.json`, `exit.json`, stdio capture, and UI are a follow-up change (see Follow-ups).
- Adding app/run/instance fields to metric, trace, log, or chrono records.
- Correlating logs with trace ids, metric labels, or service identity inside records.
- Network transport, remote collection, or a database schema.
- Cross-platform path semantics beyond the current Linux target; absolute-path validation is POSIX (`/`-rooted).
- Versioning or migrating the settings schema. New keys are additive and older readers ignore them.

## Decisions

### D1. Reuse the existing sink configuration types as settings members

`Metrics_settings` gains `metrics::JsonFileMetricSink_config sink{}`, `Tracing_settings` gains `tracing::JsonFileTraceSink_config sink{}`, and `Chrono_settings` gains `chrono::JsonFileChronoSink_config sink{}`. The members are placed directly after `options` and keep the existing default member initializers (`path` empty, `append` true, tracing rotation `None`, both trace limits zero).

Rationale: the policy already constructs these values; a parallel settings-only sink type would need a conversion and would drift. Reusing them means a policy hook becomes `out.emplace(settings.sink)`.

Alternative considered: a shared `Telemetry_sink_config` in `nexigen::app`. Rejected because metrics/tracing/chrono sinks have different fields (trace rotation and rate limits), and a common type would either flatten unused fields or force conversions.

Consequence: `app_settings.hpp` includes `json_file_metric_sink.hpp` and `json_file_trace_sink.hpp`; `chrono_settings.hpp` includes `json_file_chrono_sink.hpp`. These are cold-path headers with no hot-path state.

### D2. The `sink` object is field-merged; empty `path` is the unconfigured sentinel

New JSON shape, nested under each subsystem's existing document:

```json
// metrics.json
{
  "collect_interval_ns": 1000000000,
  "sink": { "path": "/var/nexigen/runs/r1/controller/i1/metrics.jsonl", "append": true }
}

// tracing.json
{
  "max_tracers": 4,
  "sink": {
    "path": "/var/nexigen/runs/r1/controller/i1/traces.jsonl",
    "rotation": "None",
    "max_spans_per_window": 0,
    "window_ns": 0
  }
}

// chrono.json
{
  "collect_interval_ns": 1000000000,
  "sink": { "path": "/var/nexigen/runs/r1/controller/i1/chrono.jsonl", "append": true }
}
```

Rules:

- A missing `sink` block or missing key inherits the current value; `sink` is merged field by field exactly like provider fields (defaults → global → local).
- An empty `path` means "this settings source does not configure a sink". It is not a diagnostic, and the resolver may still fill it from instance identity.
- `rotation` accepts the canonical `RotationPolicy` names already produced by `to_string`: `"None"`, `"Hourly"`, `"Daily"`.
- Unknown keys are ignored, matching the existing loader style.
- A non-object `sink`, wrong field type, or unknown rotation name records a diagnostic naming the source and field (`sink`, `sink.path`, `sink.append`, `sink.rotation`, ...) and keeps the previous value.
- Loading does not open, create, stat, or validate the path. A path to a missing directory loads successfully; failure surfaces when the sink is constructed.

Implementation: extend the existing anonymous-namespace readers in `src/app/app_settings.cpp` and `src/chrono/chrono_settings.cpp`; add one small `read_sink(...)` helper per module that mutates a config value only after every field validates. Do not add a shared settings-detail header: chrono deliberately keeps its own vocabulary, and the helper is a few dozen lines.

Serialization stays manual inside the settings writers: `to_json(Metrics_settings)` emits `{"path": ..., "append": ...}`, tracing emits all four fields, chrono emits two. The sink config types are not required to gain `from_json`, because loader-produced settings have no `from_json` and no other caller needs to parse a sink config document.

### D3. Sink paths are file-only; environment carries identity, not output paths

The A+B split is deliberate:

- **A (settings)** is the explicit, per-instance output contract. A launcher writes `settings/metrics.json`, `settings/tracing.json`, `settings/chrono.json` with absolute paths and the application reads them through the normal loader.
- **B (environment)** is the identity and fallback contract. It says *where the instance lives*, not *which file each signal uses*. `NEXIGEN_INSTANCE_DIR` gives the resolver a directory; the filenames are fixed.

Therefore no `NEXIGEN_METRICS_SINK_PATH`, `NEXIGEN_TRACING_SINK_PATH`, or `NEXIGEN_CHRONO_SINK_PATH` variable is introduced. The documented provider overrides (`NEXIGEN_METRICS_*`, `NEXIGEN_TRACING_*`, `NEXIGEN_CHRONO_*`) do not touch `sink`.

Alternative considered: an environment path override as a third priority level. Rejected because it creates two ways to express the same explicit choice, makes diagnostics ambiguous, and lets a stale shell variable silently redirect output away from the instance directory. The instance-directory fallback already covers launchers that cannot write settings.

### D4. Identity is a plain value loaded from five variables

New cold-path header `include/nexigen/app/telemetry_layout.hpp`:

```cpp
namespace nexigen::app {

struct Run_identity {
    std::string run_id;
    std::string run_dir;
    std::string app_name;
    std::string instance_id;
    std::string instance_dir;
    [[nodiscard]] std::string to_string() const;
};

struct Telemetry_paths {
    std::string metrics;
    std::string traces;
    std::string chrono;
    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] Run_identity load_run_identity() noexcept;

[[nodiscard]] core::Status validate_run_identity(const Run_identity& identity) noexcept;

// Pure resolver over already-extracted explicit paths.
[[nodiscard]] core::Status resolve_telemetry_paths(
    const Run_identity& identity,
    std::string_view explicit_metrics_path,
    std::string_view explicit_traces_path,
    std::string_view explicit_chrono_path,
    Telemetry_paths& out) noexcept;

// Convenience overload matching the settings pipeline.
[[nodiscard]] core::Status resolve_telemetry_paths(
    const Run_identity& identity,
    const Metrics_settings& metrics,
    const Tracing_settings& tracing,
    const chrono::Chrono_settings& chrono,
    Telemetry_paths& out) noexcept;

} // namespace nexigen::app
```

Variable mapping:

| Field | Variable |
|---|---|
| `run_id` | `NEXIGEN_RUN_ID` |
| `run_dir` | `NEXIGEN_RUN_DIR` |
| `app_name` | `NEXIGEN_APP_NAME` |
| `instance_id` | `NEXIGEN_INSTANCE_ID` |
| `instance_dir` | `NEXIGEN_INSTANCE_DIR` |

`load_run_identity()` reads only these variables. Unset and empty are equivalent (empty field). If `instance_dir` is empty but `run_dir`, `app_name`, and `instance_id` are all non-empty, it derives:

```
<run_dir with trailing '/' removed>/<app_name>/<instance_id>
```

No CWD, hostname, PID, executable path, or filesystem probing is consulted. Derivation is pure string concatenation; `std::filesystem` is avoided so no error-code ceremony or platform path normalization leaks into the contract.

The loader returns a value, not a status: an incomplete environment is a valid observed-app state, and diagnostics for identity belong to the caller (the future runner/hub), not to settings.

### D5. Validation is conservative but permits an empty identity

`validate_run_identity` returns `core::Status`:

- All fields empty → OK. This is the "application does not use the contract" case and must not break existing programs.
- For each present id (`run_id`, `app_name`, `instance_id`): length 1..128 bytes, characters in `[A-Za-z0-9._-]`, and not exactly `.` or `..`, and no `".."` substring. The `".."` rule is deliberately stricter than the character set so a later path join cannot escape the run directory. `/` and `\` are rejected by the character set.
- For each present directory (`run_dir`, `instance_dir`): non-empty and starting with `/` (POSIX absolute).
- A present directory has no length limit here; the operating system enforces `PATH_MAX` when the runner creates it.
- Errors return `ErrorCode::InvalidArgument`; validation does not allocate beyond what is already in the strings and does not touch the filesystem.

Rationale for rejecting a relative directory: the resolver's fallback is only useful if the hub and the application agree on the same location after `chdir`, process restarts, and `nexigen-run` working-directory choices. A relative `NEXIGEN_RUN_DIR` is treated as a producer bug rather than silently resolved against CWD.

### D6. Resolution precedence is per signal, with no side effects

For each signal independently:

1. A non-empty explicit settings path (`settings.<signal>.sink.path`) wins and is copied verbatim. It is not normalized or made absolute.
2. Otherwise, if the identity validates and `instance_dir` is non-empty, the path is `<instance_dir>/metrics.jsonl`, `<instance_dir>/traces.jsonl`, or `<instance_dir>/chrono.jsonl`.
3. Otherwise the path is empty, meaning the helper does not configure that sink and the application keeps its own policy.

Resolution validates the identity first. On an invalid identity it returns `InvalidArgument` and leaves `out` unmodified; it does not partially fill one signal. A valid but empty identity is not an error: it simply produces empty paths when no explicit paths are set. The resolver creates no directory, opens no file, writes no byte, and performs no `stat`.

Why per signal: an application may want metrics in the instance directory while tracing goes to a shared collector file. A single all-or-nothing path would force a second mechanism.

### D7. Serialization follows the project-wide trio, with settings still one-way

- `Run_identity` and `Telemetry_paths` get member and free `to_string`, `std::formatter`, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`.
- `from_json` accepts only objects; a field is assigned only when it is a JSON string; non-string values, missing fields, arrays, scalars, and extra keys leave the target unchanged. No exception escapes.
- `to_string` is deterministic and stable: `Run_identity(run_id=..., run_dir=..., app_name=..., instance_id=..., instance_dir=...)` and `Telemetry_paths(metrics=..., traces=..., chrono=...)`.
- `Metrics_settings`, `Tracing_settings`, and `Chrono_settings` keep `to_json` only. Their existing `to_string` output gains a `sink=` segment so diagnostics and tests show the effective path.
- JSON field names are exactly the struct member names (`run_id`, `run_dir`, `app_name`, `instance_id`, `instance_dir`; `metrics`, `traces`, `chrono`), so C# and Python implementations can share the document shape.

### D8. Integration point: resolve before `initialize`, then use the settings member

`Infrastructure::initialize` is not changed. A managed application that wants the fallback does this during initialization:

```cpp
app::App_settings loaded{};
if (const core::Status status = app::load_app_settings(paths, loaded); !status) {
    return status; // diagnostics remain in `loaded`
}

const app::Run_identity identity = app::load_run_identity();
app::Telemetry_paths resolved{};
if (const core::Status status = app::resolve_telemetry_paths(identity, loaded, resolved); !status) {
    return status;
}

loaded.metrics.sink.path = resolved.metrics;   // no-op when settings already set it
loaded.tracing.sink.path = resolved.traces;
loaded.chrono.sink.path  = resolved.chrono;
// empty strings remain empty: the policy decides console/null/app-specific output

return infra->initialize(loaded);
```

Policy hooks then construct sinks from `settings.<signal>.sink` when `path` is non-empty, and fall back to console/null otherwise. The four-stream example (`examples/app/ex_app_cli_settings.cpp`) is updated to write `sink` blocks instead of using `kMetricsPath`/`kTracesPath`/`kChronoPath` constants in the policy.

For apps that do not call the resolver, existing hard-coded policies continue to compile and work; the new sink member is simply empty and unused. That keeps the change additive.

### D9. Cold-path and concurrency impact

Everything added here runs before providers start: `getenv`, string building, JSON parsing, validation, and serialization. No member added to a hot-path type, no atomic, no lock, no allocation on producer/tick/snapshot/sink paths. The only header inclusion change is cold-path sink configuration into settings headers. A compile-time test pins that `Metrics_settings`, `Tracing_settings`, and `Chrono_settings` remain ordinary copyable, movable, non-polymorphic value types as before (they already contain `std::string`/`std::vector`; the new member adds no indirection to runtime code).

## Risks / Trade-offs

- **Settings schema drift across languages** → Publish the exact JSON shapes and key names from D2 in the README next to the existing settings tables; a C++ test asserts the serialized document keys, and the future C#/Python implementations should reuse the same fixtures.
- **Two ways to be "unconfigured"** (`sink` absent vs `path` empty) → Both mean the same sentinel and produce identical behavior; tests pin that neither records a diagnostic.
- **Explicit relative path defeats the observatory** → The resolver does not rewrite it (that would surprise a manual user). `nexigen-run` always writes absolute paths, and the docs state that relative paths are only for manual runs.
- **Long-running trace sink without rotation grows one file** → Instance directories are fresh per restart and the hub handles history; managed settings default `rotation` to `"None"` and can opt into `"Hourly"`/`"Daily"`. The hub must therefore treat a trace `path` as a filename prefix, not a single file.
- **Conservative id validation rejects some legitimate names** (spaces, Unicode, `..` inside a name) → The contract is designed for generated launcher identifiers; if a human-facing display name is needed later, it belongs in `resource.json`, not in the path.
- **Environment is inherited and can be stale** → `load_run_identity` reports what it sees; `validate_run_identity` catches malformed values, and the hub can classify a mismatched instance as observed/unknown instead of trusting it. `nexigen-run` is the single writer of the contract for managed apps.
- **Hostile `from_json` input under `-fno-exceptions`** → Assignment happens only after a type check through `get_ptr<const nlohmann::json::string_t*>()`; every other case leaves the target untouched. Tests cover non-object, wrong types, and extra fields.

## Migration Plan

1. Add the sink members and loaders while keeping defaults empty. Existing applications compile and behave identically because their policies ignore the member.
2. Add `telemetry_layout.hpp`/`.cpp` and unit tests for env loading, derivation, validation, and resolution.
3. Update the CLI settings example and its settings fixtures to set `sink` blocks; keep the existing artifact checks, so the example proves the settings path produces the same four files.
4. Update README/settings documentation with the new keys and the identity variables.
5. Rollback: revert the headers/sources; existing settings files with `sink` blocks are ignored by older readers (unknown keys are already ignored), so no data migration is needed. Already-written JSONL files remain valid because record layouts are untouched.

## Follow-ups

Implementation of this contract does not include the observatory itself. After this change lands, create a separate OpenSpec change (`nexigen-observatory`) for:

- `nexigen-run`: instance directory creation, atomic `resource.json`/`exit.json`, generated settings with absolute `sink.path` values, `setsid` process-group launch, stdio capture, PID + `/proc/<pid>/stat` start-time identity, and explicit start/stop/restart semantics.
- `nexigen-hub`: `inotify` discovery, JSONL tailing, hot ring buffer plus raw history, run/app/instance tree, managed-vs-observed state badges, and the live UI/API.

That change owns the `runs/<run>/<app>/<instance>/` layout, heartbeat thresholds, retention, and the runner's handling of rotated trace prefixes. It must not change this change's record formats or settings keys.

## Open Questions

None that would change the specs or task breakdown. The exact hub storage engine (raw JSONL + optional DuckDB/Parquet) and UI transport (HTTP/SSE vs embedded) are deferred to the follow-up change.
