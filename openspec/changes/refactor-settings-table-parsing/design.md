# Design

## Context

See proposal.md - Why. Constraints:

- Diagnostic fidelity: `tests/logging/settings_test.cpp` and the module settings
  tests assert exact diagnostic strings and their order; the refactor must be
  string-for-string identical, field-for-field in current order.
- Loaders differ in sub-shapes: sink sub-object, duration fields
  (`std::chrono::nanoseconds` from ns integers), bool/string/u64 scalars,
  enum vocabulary fields (parsed via each enum's ADL `from_string` — kept intact
  by `refactor-enum-traits-helper`).
- Cold path only: initialization-time; `std::function`/lambda tables and
  `std::string` are permitted here (mandates 4/12 scope hot paths only).
- Non-concurrent change: no atomics or shared mutable state introduced, so the
  alignas/memory-order mandate does not apply.

## Goals / Non-Goals

**Goals:**
- One generic read/write driver in `core::settings_io`; per-module code becomes
  a declarative table of fields.
- Identical observable loader behavior (diagnostics text, order, defaults,
  `global_loaded`/`local_loaded` flags, Result error codes).

**Non-Goals:**
- No schema validation language, no file-format changes, no settings vocabulary
  additions.
- Not forcing `src/logging/settings.cpp` (multi-sink shape) into the table —
  evaluate separately, skip if it distorts.

## Decisions

1. **Field = key + typed read callable + typed write callable**, stored as a
   `constexpr`-constructible span of small structs holding function pointers to
   generated lambdas (no `std::function` heap allocation: tables are static and
   targets are passed by reference into the driver). Rejected: pointer-to-member
   + `FieldKind` enum tag dispatch (works but re-introduces a switch — we are
   deleting switches, not centralizing them in a worse place).
2. **Driver owns diagnostics plumbing**: `read_fields` receives
   `(const json& doc, SettingsSource source, std::span<const FieldSpec>,
   std::vector<SettingsDiagnostic>&)` and produces the same messages the inline
   code produced (missing → default + no diagnostic; wrong type → "expected X";
   out-of-range → "expected an integer in range"). Sink sub-object is one nested
   `FieldSpec` whose read callable re-enters `read_fields` for the sink table.
3. **Order = table order**: entries are declared in the exact sequence current
   loaders read them, so diagnostic ordering is preserved by construction.
4. **`write_fields` mirrors the table** for `to_json`: each entry writes its
   `{"key", value}`; module `to_json` becomes `write_fields(j, fields, ...)`,
   killing the duplicated 336/314-token blocks.
5. **Loader skeleton lifted once `load_settings_document`** (implementation
   finding, task 3.1): after D1-D4 the largest surviving settings↔settings
   clone (349 tokens) was the per-module `load_*_settings` body — the
   reset/global/local/env/error-precedence flow is byte-identical across the
   three modules, so the driver takes `merge` + `apply_env` function pointers
   and the module loaders became one-liners.
6. **`format_settings_summary`**: the `Settings(global_loaded=.., ...)` render
   line is shared (245-token pair), modules pass their own options/sink
   `to_string` views.
7. **Environment overlay tables `EnvField` + `apply_env_fields`** with typed
   helpers (`read_env_duration_ns`, `read_env_core_mask`, `read_env_plain_u64`,
   `read_env_range_u32`): the three `apply_*_environment` bodies were
   structurally identical scaffolding around the same parse/diagnostic words;
   wording and per-field order preserved.

## Risks / Trade-offs

- **Callable tables hide types** from grep-based review → mitigated by keeping
  entries one-line and module-local in the loader .cpp.
- **Partial migration** (logging left behind) keeps some duplication → accepted:
  a distorted fit would cost more in clarity than the ~100 lines it saves.
- **Diagnostic text regressions** silently accepted by nobody: tests must pass
  unmodified (task 2.5), so any wording drift fails CI loudly.
