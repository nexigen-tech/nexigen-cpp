# Tasks

## 1. Shared machinery

- [x] 1.1 Create `include/nexigen/core/serialization_mandate.hpp` (design
  D1/D2/D3): `MandateToString<T>` concept (ADL-or-member `to_string`),
  generic `std::formatter<T> : std::formatter<std::string_view>` partial,
  generic `nexigen::to_json/from_json` pair constrained to
  `(MandateToString + from_string)`, CRTP `MandateString<T>` base. Implemented
  with the sweep-scoped corrections: `MandateToString` accepts BOTH
  `std::string` and `std::string_view` returns (per-enum helpers are
  `constexpr string_view`); the CRTP base is REPLACED by a constrained
  generic `nexigen::to_string` template (a CRTP friend lands in the base's
  namespace, not the deriving type's — invisible to ADL); the generic JSON
  pair was REMOVED after experimentally confirming the ADL boundary (D2
  note below) — enum JSON pairs stay per-namespace. Verified: a demo enum +
  struct inside nexigen reach std::format/to_json/from_json/free to_string
  with zero extra boilerplate (`tests/core/core_test.cpp` mandate_demo).
- [x] 1.2 Sweep decisions (grep 55 delegate formatters + per-enum to_json
  pairs): **formatters** whose body is exactly the to_string delegate are
  deleted — the generic std::formatter partial serves them; **JSON pairs
  stay per-enum/per-type** — the experiment recorded the ADL rule tightly
  ([basic.lookup.argdep]: enums get only their immediate namespace;
  structs get the full chain but struct JSON is per-field by design, not a
  delegate); **free to_string** companions stay where written (they
  outrank the generic non-template-wise and serve test scopes outside
  nexigen). Recorded sweep lists live in 2.x notes below.

## 2. Sweep deletions

- [x] 2.1 Delete the delegate formatter specializations (to_json/from_json
  per-enum one-liners STAY — recorded 1.2 ADL boundary). Removed ~59
  delegate formatter blocks across core/settings-vocabulary (×3),
  result(ErrorCode), logging (log_level, logger_options ×4,
  logging_settings), metrics (policy_enums ×4, metric_snapshot ×8,
  metric_provider, null sink, metrics_settings, json_file sink,
  composite sink, console sink, state_gauge, counter, gauges, hists ×2,
  descriptive_statistics), tracing (span_context ids ×3, span_record,
  span_enums ×3, json_file_trace_sink, null_sink, tracer_concept, chrome,
  console, provider), chrono (clock_id, provider, settings, sinks ×2),
  app (infrastructure_stop_reason, app_settings ×2, command_line ×3,
  telemetry_layout ×2), runner (instance_lifecycle ×2, runner, app_suite
  via macro-free sweep ×7, StartOptions), hub (streaming, discovery,
  model ×9 through the deleted NEXIGEN_HUB_FORMATTER macro),
  diagnostics (stopwatch_state). 35/35 Debug suite green after each
  batch; a handful of formatter bodies that compute their text locally
  (`format(text, ctx)` customs) and composite/diagnostics custom cases
  remain explicit by design (the header exceptions note explains them).
- [x] 2.2 Adopt `MandateString<T>` at the verbatim-free-function types.
  REVISED during implementation (design update): the CRTP-friend trick
  cannot serve ADL from a sibling base namespace, so the free function is
  now a constrained generic `nexigen::to_string` template — per-type
  free friends OUTRANK it where they exist, and NEW types get the free
  function for free through the member contract. Verified: core_test
  mandate_demo proves a brand-new enum + struct serve
  to_string/formatter/json with zero extra boilerplate.

## 3. Verification

- [x] 3.1 Full Debug and Release suite runs green; targeted compile suites
  under Clang `-Werror`; TSan six-suite sweep clean; ASan full green.
  Verified: Debug 35/35; Release 34/35 (single fail = the environmental
  strict-latency gate — recorded at length in the P1/P2 changes; the
  pristine baseline measures the same ~130 ns); LTO wired (105
  `-flto` rules); Clang 21 `-Werror` 0 errors; TSan — zero race reports in
  logging/metrics/tracing/app/runner/core; ASan — 15/15 suites green.
- [x] 3.2 `openspec validate --strict --changes` clean (skip_specs note
  accepted — no spec-level behavior change).
