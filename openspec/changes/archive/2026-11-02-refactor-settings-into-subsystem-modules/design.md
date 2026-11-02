# Design

## Context

See proposal.md — Why. The relevant current state:

- `nexigen::logging` and `nexigen::chrono` each define a byte-identical settings vocabulary (`SettingsSource`, `SettingsPaths`, `SettingsDiagnostic`) plus a `Settings` type and a `load_*_settings` loader. chrono duplicated the vocabulary solely to avoid depending on logging.
- `nexigen::app` owns `MetricsSettings` / `TracingSettings` and the `load_metrics_settings` / `load_tracing_settings` loaders, so the composition layer holds subsystem configuration semantics.
- The settings specs spell type names in snake_case (`App_settings`, `Metrics_settings`, `Settings_diagnostic`) while the code uses PascalCase (mandate 8).
- All of this is cold-path: no producer/tick/snapshot/sink code is involved, so the refactor cannot touch any hot-path, latency, or allocation guarantee.

## Goals / Non-Goals

**Goals:**

- One canonical copy of the settings vocabulary in `nexigen::core`.
- Each subsystem (`logging`, `chrono`, `metrics`, `tracing`) owns its `Settings` type and loader in its own module.
- `app` becomes a pure aggregator (`AppSettingsPaths` + `Settings` + `load_app_settings` delegating to four loaders).
- Settings specs use PascalCase type names matching the code.

**Non-Goals:**

- No behavioral change to any loader: precedence, environment overrides, best-effort diagnostics, and sink handling are moved verbatim, not redesigned.
- No change to non-settings snake_case drift elsewhere (e.g. `Command_line_options`, `Infrastructure_stop_reason` in other specs) — that is a separate cleanup, out of scope here.
- No new hot-path types, no concurrency, no new dependencies.

## Decisions

### D1 — The vocabulary lives in `nexigen::core`

`SettingsSource` (enum), `SettingsPaths` (struct), and `SettingsDiagnostic` (struct) move to `nexigen::core`, in one header `include/nexigen/core/settings_vocabulary.hpp`.

- **Why core over a new `settings` module**: `ErrorCode` already lives in `core` precisely because every module depends on it; the vocabulary is the same cross-cutting shape (five consumers). A dedicated module would add directories, a CMake target, and a spec for three small types. core is the neutral home all modules already include.
- **Why one header over three**: the three types are interdependent (`SettingsDiagnostic` has a `SettingsSource` field) and form one cohesive cluster, matching core's existing one-header-per-concern style (`result.hpp` holds `ErrorCode` + `Result` + `Status`).
- **Alternatives rejected**: a `nexigen::settings` module (heavier, no precedent); keeping the vocabulary in logging and having chrono/metrics/tracing depend on logging (reintroduces exactly the dependency chrono deliberately avoided).

The types keep their existing PascalCase names and their existing serialization (`to_string`/`from_string`, `std::formatter`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`); only the namespace changes.

### D2 — Logging and chrono switch to `core::` vocabulary

`logging_settings.hpp` and `chrono_settings.hpp` drop their local `SettingsSource`/`SettingsPaths`/`SettingsDiagnostic` definitions (and the corresponding `std::formatter` specializations and JSON declarations) and include `<nexigen/core/settings_vocabulary.hpp>`, referencing `core::SettingsSource`, `core::SettingsPaths`, `core::SettingsDiagnostic` explicitly. This is a strict deduplication: the two copies are byte-identical today.

- **Why explicit `core::` qualification over `using` aliases**: matches the existing `core::Status` / `core::ErrorCode` usage and avoids any ambiguity in the module namespaces.
- `logging::Settings` and `chrono::Settings` keep their own `Settings` type names (now wrapping `core::SettingsPaths`/`core::SettingsDiagnostic` where relevant); their loaders and `apply_to`/`check_capacity` are unchanged in behavior.

### D3 — Metrics and tracing get their own settings module headers

New `include/nexigen/metrics/metrics_settings.hpp` and `include/nexigen/tracing/tracing_settings.hpp`, each declaring:

```cpp
namespace nexigen::metrics {
struct Settings {
    MetricProviderConfig options{};
    JsonFileMetricSinkConfig sink{};
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<core::SettingsDiagnostic> diagnostics{};
    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] core::Status load_metrics_settings(const core::SettingsPaths&, Settings&) noexcept;
} // namespace nexigen::metrics
```

(analogous for `nexigen::tracing` with `TracerProviderConfig` / `JsonFileTraceSinkConfig`). Loader bodies move from `src/app/app_settings.cpp` into `src/metrics/metrics_settings.cpp` and `src/tracing/tracing_settings.cpp`, symmetric with `src/logging/logging_settings.cpp` and `src/chrono/chrono_settings.cpp`.

- **Why `Settings` (not `MetricsSettings`)**: matches `logging::Settings` / `chrono::Settings`; the namespace disambiguates. `app::MetricsSettings` / `app::TracingSettings` cease to exist.
- The `to_string()` text changes from `"TracingSettings(...)"`/`"MetricsSettings(...)"` to `"Settings(...)"`; tests asserting the exact string are updated accordingly.

### D4 — App becomes a pure aggregator

`app_settings.hpp` now declares:

```cpp
namespace nexigen::app {
struct AppSettingsPaths {          // was SettingsPaths
    core::SettingsPaths logging{};
    core::SettingsPaths metrics{};
    core::SettingsPaths tracing{};
    core::SettingsPaths chrono{};
    [[nodiscard]] std::string to_string() const;
};
struct Settings {                  // name unchanged
    logging::Settings logging{};
    metrics::Settings metrics{};
    tracing::Settings tracing{};
    chrono::Settings chrono{};
    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] core::Status load_app_settings(const AppSettingsPaths&, Settings&) noexcept;
} // namespace nexigen::app
```

`load_app_settings` now delegates to `logging::load_logging_settings`, `chrono::load_chrono_settings`, `metrics::load_metrics_settings`, and `tracing::load_tracing_settings`, and returns the worst status (`SystemError` > `InvalidArgument` > `Ok`).

- **Why rename only the paths aggregate**: `app::SettingsPaths` collides with the new `core::SettingsPaths`; `app::Settings` does not collide with the module-scoped `Settings` types. Renaming only the colliding name minimizes churn. `make_app_settings_paths` and callers follow.

### D5 — Serialization contract is preserved, not re-implemented

The moved enum and structs already satisfy mandates 9–11. The only change is their namespace and header. The `std::formatter` specializations for `core::SettingsSource` / `core::SettingsPaths` / `core::SettingsDiagnostic` move to core verbatim; logging/chrono drop their copies. Loader-produced `Settings` types (all four modules plus the app aggregate) keep `to_json` only, with `from_json` still deliberately absent (pinned at compile time in tests).

## Risks / Trade-offs

- **Large mechanical blast radius** (headers, src, three test files, examples, `command_line.hpp`, `infrastructure.hpp`, `telemetry_layout.hpp`) → [Risk: a missed reference] → mitigate with a grep-driven rename pass and a full compile under `-Werror` plus the existing test suites.
- **Source-level breaking change** for any consumer of `app::MetricsSettings`/`app::TracingSettings`/`app::SettingsPaths`/`logging::SettingsSource`-et-al → this is a source library with no external ABI guarantee; acceptable, documented in the proposal as **BREAKING**.
- **Exact-string `to_string` assertions** in tests will break on the `"TracingSettings"` → `"Settings"` rename → update those assertions in the same change.
- **No concurrency or atomic component** is introduced or altered, so the `alignas(64)` / `std::memory_order` design rules do not apply; this is a cold-path-only reorganization with zero latency, allocation, or lock impact.

## Migration Plan

This is a source-library refactor with no runtime deployment or data migration. The change is atomic within the repo:

1. Add `core/settings_vocabulary.hpp` (+ its `to_json`/`from_json`/formatter bodies if not inline) and a `core/settings-vocabulary` spec (already captured).
2. Switch `logging` and `chrono` to `core::` vocabulary and delete their local copies.
3. Add `metrics/metrics_settings.{hpp,cpp}` and `tracing/tracing_settings.{hpp,cpp}`, moving the loaders out of `app_settings.cpp`.
4. Rewrite `app_settings.hpp` to the aggregator shape and rename `SettingsPaths` → `AppSettingsPaths`.
5. Update `command_line.hpp`, `infrastructure.hpp`, `telemetry_layout.hpp`, tests, and examples to the new names.
6. Run the full suite (Debug, Release `-O3 -march=native -flto`, TSan, ASan) and confirm zero behavior change outside the renamed symbols.
