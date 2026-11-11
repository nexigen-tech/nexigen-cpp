# Design

## Context

See `proposal.md` — Why. Current state that shapes the approach:

- The gate already exists and is a CI job: `cmake/check_complexity.py`, `cmake/Complexity.cmake` (target `complexity-check`), `cmake/RunComplexityCheck.cmake`, `.github/workflows/ci.yml` job `complexity`. Running it today reports 12 functions over CCN 15 and exits non-zero:
  - production: `com::from_json` 22 (`src/com/service_manifest.cpp:54`), `logging::from_json` 19 (`include/nexigen/logging/iceoryx2_logger_config.hpp:68`), `logging::from_json` 17 (`include/nexigen/logging/log_frame.hpp:94`), `exec::from_json` 17 (`src/exec/child_process.cpp:84`), `phm::from_json` 17 (`src/phm/phm_manifest.cpp:162`);
  - tests: `test_producer` 34, `test_file_logger` 28, `test_latency` 27 ×2, `test_stress` 23, `test_composite` 17, `test_method` 16.
- The gate's default directories are `include/ src/ tests/`; `tools/` is not scanned. Four entry points exceed the threshold there: `run_sm` 34, `run_phmd` 33, `nexigen-logd::main` 33, `nexigen-metricsd::main` 32.
- `nexigen::core` is an INTERFACE (header-only) target (`CMakeLists.txt:156`); every other module is STATIC (`chrono` 201, `logging` 227, `metrics` 250, `tracing` 289, `diagnostics` 328, `app` 350, `exec` 371, `hub` 387, …), so their cold-path bodies can be compiled once.
- Existing precedents to follow rather than invent:
  - out-of-line config serialization — `src/logging/{console_logger_config,file_logger_config,logger_options,spam_protection_config}.cpp`, `src/metrics/snapshot_format.cpp`, `src/com/service_manifest.cpp`;
  - decomposed sub-parsers — `src/tracing/record_format.cpp` (`from_json_header`, `from_json_location`, `from_json_attributes`);
  - daemon staging — `tools/nexigen-sm` and `tools/nexigen-phmd` already have `parse_args` + `run_*`;
  - test registration — `tests/<module>/main.cpp` calls `suite_*()`, each suite calls `run_suite("<name>", fn)`.
- Every translation unit already compiles nlohmann/json once via `include/nexigen/core/pch.hpp`; hot headers (`log_record.hpp`, `spsc_ring_buffer.hpp`, `thread_management.hpp`) do not include it. Cold-path placement therefore affects emitted code, not parser cost.
- Constraints: `-fno-exceptions`, `-fno-rtti`, `JSON_NOEXCEPTION=1`, CCN ≤ 15, file-per-entity naming, `cmake/check_naming.py` and `check_result_value.py` gates.

## Goals / Non-Goals

**Goals:**

- The `Complexity` CI job passes with `tools/` included, and the same verdict is reachable from `ctest`.
- One cold-path JSON field vocabulary replaces ~55 hand-written probe sites when their type is touched.
- Five deserializers, four daemon entry points and seven test functions are decomposed without any observable behavior change.
- Cold-path serialization of compiled modules is emitted once per module, with a bounded, documented audit of what remains.
- Verification is mechanical: lizard, `nm` symbol parity, CLI golden capture, test-name inventory, full sanitizer matrix.

**Non-Goals:**

- No new module, no new public type, no change to any JSON schema, CLI flag, exit code, log line or CMake option.
- No change to `examples/`, which the gate intentionally excludes.
- No change to hot-path code, atomics, `alignas(64)` layouts, shard sizing or `std::memory_order` (none of the touched code is concurrent).
- Not fixing `analyzer.py`/`cloc` build-directory measurement (separate concern found during the audit).
- No wholesale rewrite of every header definition: only cold-path config/manifest serialization touched by this change plus the audited, clearly eligible remainder.

## Decisions

### D1 — Shared field vocabulary as free functions in `include/nexigen/core/detail/json_field.hpp`

Namespace `nexigen::core::detail`, `inline` free functions, `noexcept`, header-only:

- `[[nodiscard]] bool read_string(const nlohmann::json&, const char* key, std::string& out) noexcept`
- `[[nodiscard]] bool read_bool(const nlohmann::json&, const char* key, bool& out) noexcept`
- `[[nodiscard]] bool read_signed(const nlohmann::json&, const char* key, std::int64_t& out) noexcept`
- `template <class T> [[nodiscard]] bool read_unsigned(const nlohmann::json&, const char* key, T& out) noexcept` — range-checked with `std::numeric_limits<T>`
- `template <class Fn> bool read_object(const nlohmann::json&, const char* key, Fn&&) noexcept`
- `template <class Fn> bool read_object_array(const nlohmann::json&, const char* key, Fn&&) noexcept` — all-or-nothing: non-object element rejects the whole collection

Rationale: a free-function vocabulary adds no public type, so the mandate-9/10/11 obligations (`to_string`, `std::formatter`, `to_json`/`from_json`) do not apply, and `detail/` matches the project's rule that internal types live under `nexigen::<module>::detail`. Return-`bool` "field present and accepted" keeps the existing "leave destination untouched" semantics explicit at the call site.

Alternatives considered: a stateful `JsonReader` class (rejected — new public type with formatter/JSON obligations, and error state obscures per-field semantics); a table-driven descriptor list (rejected — extra allocation and indirection on a cold path, and it cannot express type-specific rules such as the 4-byte `app_id` and 225-byte payload truncation in `LogFrame`).

### D2 — Range-checked unsigned reads replace unchecked `get<T>()`

Today `it->get<std::uint32_t>()` runs after `is_number_unsigned()`, which accepts out-of-range values; under `JSON_NOEXCEPTION=1` the conversion's `out_of_range` escalates to `std::abort`. `read_unsigned` accepts a value only when `value <= std::numeric_limits<T>::max()` and otherwise leaves the destination untouched and returns `false` (spec: `core/serialization`, "Out-of-range unsigned value is rejected without terminating").

Rationale: terminates a crash path without inventing clamping semantics (clamping would silently reinterpret a hostile document as a valid configuration). Alternative — clamp and continue (rejected, changes config meaning). Alternative — keep `get<T>()` and rely on tests (rejected, the abort is unreachable-but-fatal by definition).

### D3 — Decomposition shape: flat `from_json` + per-type sub-parsers in the defining unit

Top-level `from_json` becomes a flat sequence of helper calls (one or two statements per field / per nested collection). Anything more complex becomes a file-local `parse_*` function in an anonymous namespace, capped at CCN 15, following `src/tracing/record_format.cpp`. Collections are parsed into a local container and moved into the destination only on full success, preserving today's all-or-nothing behavior.

Applied targets and the reason each was over threshold:

| Target | Today | Shape after |
|---|---|---|
| `src/com/service_manifest.cpp` `com::from_json` | 22, 53 NLOC | `parse_provided_service`, `parse_required_service`, two `read_object_array` call sites |
| `include/nexigen/logging/iceoryx2_logger_config.hpp` `logging::from_json` | 19, 50 NLOC | move to `src/logging/iceoryx2_logger_config.cpp`; `parse_worker`/`parse_throttle` sub-parsers |
| `include/nexigen/logging/log_frame.hpp` `logging::from_json` | 17, 57 NLOC | move to `src/logging/log_frame.cpp`; `read_app_id` + `read_payload` bound-checking helpers, enum read via `core::from_string` |
| `src/exec/child_process.cpp` `exec::from_json` | 17, 36 NLOC | `parse_env_entry`, `parse_affinity` sub-parsers |
| `src/phm/phm_manifest.cpp` `phm::from_json` | 17, 57 NLOC | `parse_supervised_entity`, `parse_recovery_policy` sub-parsers |

Alternative: one generic template-driven reader for all manifests (rejected — the manifests differ in validation rules and the shared layer would need callbacks that cost more complexity than they remove).

### D4 — Placement rule for P2, with an explicit exemption list

In compiled modules, cold-path `to_string` / `to_json` / `from_json` bodies of configuration and manifest types move to `src/<module>/<entity>.cpp` and lose their `inline` keyword. Anything that must stay in a header stays:

- templates (165 of 267 headers contain `template<`), `constexpr` code;
- hot-path inline functions — `LogFrame::app_id_text`, `LogFrame::payload_text`, ring-buffer and `alignas(64)` accessors;
- `std::formatter` specializations (they are class templates);
- all of `nexigen::core` (INTERFACE target).

Bounded scope for this change: the five P1 targets above plus the daemon configuration types the tools load (`logging/log_daemon_config.hpp`, `metrics/metric_daemon_config.hpp`, `logging/iceoryx2_logger_config.hpp`). The remaining eligible definitions are inventoried in `docs/header-compilation-audit.md` as follow-up work rather than moved in one unreviewable sweep.

Rationale: the repo already splits exactly this way for `console_logger_config`, `file_logger_config`, `logger_options`, `spam_protection_config` and `snapshot_format`; extending the established pattern keeps review focused. Static linking plus the absence of visibility/export macros means moving a definition is not an ABI event — verified by symbol parity.

Alternative: move every eligible definition now (rejected — hundreds of files, no behavioral benefit to this change, and it would swamp the gate fix).

### D5 — Daemon entry points: per-tool staging, no new shared runner

Each tool gains `struct Options`, `parse_args(argc, argv, Options&) -> std::optional<int>` (return value = early exit code, as in `nexigen-sm`) and `run_<daemon>(const Options&) -> int`, with `main` reduced to three statements. `run_*` is split into stages: `load_and_validate_config`, `open_services`, `run_loop`, `shutdown`. Printed text, usage lines and exit codes are copied verbatim from the current code; `--help` keeps returning 0 and a missing `--config` keeps returning 1 with the same stderr line.

`nexigen-logd` and `nexigen-metricsd` currently keep argument scanning and the whole daemon inside `main`, so they get the full split; `nexigen-sm` and `nexigen-phmd` already have the outer shape and only need their runner bodies staged.

Alternative: a shared `tools/common/daemon_runner.hpp` (rejected for this change — the four tools differ in config type, service topology, loop shape and shutdown semantics; a shared runner would need callbacks and a per-tool output prefix, adding coupling for ~10 saved lines. Recorded as a possible later consolidation.)

### D6 — Gate widening and CTest wiring

`cmake/check_complexity.py`:

- `DEFAULT_DIRS` becomes `include/ src/ tests/ tools/`;
- the `alignas` filter is kept and applied to the reported identifier, not the whole line (a file path containing the word must not silently hide a real violation);
- the exit summary stops double-counting (the current `n_warnings` heuristic prints "passed (N filtered)" for lines it already filtered);
- exits `77` (CTest skip) when the `lizard` executable cannot be found, so a machine without lizard reports *skipped* rather than passing.

`CMakeLists.txt` / `cmake/Complexity.cmake`: register `nexigen_complexity_gate` as a test invoking the same script over the same four directories with `SKIP_RETURN_CODE 77`, next to the existing `complexity-check` target. The target stays the CI entry point (`.github/workflows/ci.yml` job `complexity` already builds it), so CI needs no structural change and automatically inherits `tools/`.

Rationale for two entry points: the target is what CI calls today; the CTest entry makes the contract locally visible and enforces the same verdict. Alternative: CTest-only (rejected — would require changing the CI job, and the target is a documented developer command).

### D7 — Verification mechanics

- **Gate**: `python3 cmake/check_complexity.py` and `ctest -R complexity` both green; `lizard -l cpp -C 15 -w include src tests tools` shows only filtered `alignas` lines.
- **Symbol parity**: capture `nm -C --defined-only --extern-only` for `libnexigen_{logging,metrics,com,exec,phm,sm}.a` before decomposition, diff after; only added internal (`t`/`W`) symbols are acceptable, no removed or added public (`T`) symbol.
- **CLI golden capture**: run each tool with `--help`, `--config <valid>`, no args, and an unknown flag; capture stdout, stderr, exit code before and after; require identical output.
- **Test inventory**: `run_suite("<name>", …)` names from `tests/<module>/main.cpp` and the per-suite registrations are compared before/after; `NX_CHECK` counts per file must not decrease.
- **JSON semantics**: for each touched `from_json`, a table-driven hostile-input test (wrong type, missing key, out-of-range number, non-object array element) asserts "destination unchanged, no abort"; existing round-trip tests stay as-is.

### D8 — Memory layout and concurrency (explicit non-change)

This change introduces no concurrent or atomic component. `alignas(64)` layouts are untouched: `LogFrame` stays exactly 256 bytes / 64-aligned (`src/logging/log_frame.cpp` only receives the moved `to_json`/`from_json` bodies, the `static_assert`s remain in the header), `ExponentialBank`/`ExponentialSlot` and all cache-line-aligned atomics keep their declarations. No per-thread shard sizing and no `std::memory_order` is modified. The new field helpers hold no state, take no locks and are called only from cold-path configuration and diagnostic code.

## Risks / Trade-offs

- **Moving a definition breaks a header that used it inline** (for example a header calling `logging::from_json` directly). → Before each move, grep header usages; if a header depends on the definition, keep it inline or forward through a declaration in the header plus the moved definition in the `.cpp`. Compile the full matrix.
- **`get<T>()` under `JSON_NOEXCEPTION=1` aborts on mismatch, not throws.** → All reads go through the centralized helpers, which check the type first; hostile-input tests cover wrong type, missing key, and out-of-range numbers.
- **Out-of-range unsigned behavior changes** from process abort to "field rejected". → Documented in `core/serialization`; existing tests that relied on the abort (none expected) will fail loudly in the matrix.
- **`nm` symbol diff is noisy** because anonymous-namespace helpers appear as local symbols. → Compare only external defined symbols and ignore `t`/`W` additions.
- **Widening the gate to `tools/` before the tools refactor turns CI red mid-change.** → Order the tasks: tools decomposition first, gate widening last, and verify locally with the widened script at the end.
- **Splitting latency-sensitive tests can perturb measurements** (`test_latency` ×2, `test_stress`). → Only split setup/assert blocks; keep measured loops and warm-up phases byte-identical, and compare latency-gate numbers on `build-verify-lat`.
- **Header moves can change inlining of `to_string` in hot diagnostics** → `to_string` is cold-path by mandate; no call site sits on a producer or worker hot path (verified by inspection of the four call sites per type).
- **Review size.** → Four independent phases (P1 → P2 → P3 → P4) with the gate green after each; each phase is a separate commit that can be reverted alone.

## Migration Plan

1. Land the shared vocabulary header plus its unit tests (no call-site changes yet) — builds and tests stay green.
2. Migrate and decompose the five production deserializers, moving the two header-resident ones to `src/logging/` in the same step (P1 + the bounded part of P2).
3. Decompose the four daemon entry points while keeping CLI output byte-identical (P3).
4. Split the seven test functions (P4), keeping registered suite names and assertion counts.
5. Widen the gate to `tools/` and register the CTest entry; verify the full sanitizer matrix; record the remaining header inventory in `docs/header-compilation-audit.md`.

Rollback: each phase is an independent commit; reverting any single phase restores the previous state without cross-phase dependencies. No data or on-disk format migration is involved.
