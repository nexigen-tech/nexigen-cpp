# Proposal

## Why

Three of the four module settings loaders — `src/chrono/settings.cpp`,
`src/metrics/metrics_settings.cpp`, `src/tracing/tracing_settings.cpp` — copy the
same per-field read + diagnostics scaffolding and the same `to_json` body. The
clone scan measured 336 structurally duplicated tokens (chrono ↔ metrics) and
314 (chrono ↔ tracing) in the same regions. The primitives themselves already
live in `core::settings_io` (`add_diagnostic`, `read_uint`, `read_sink_string`,
`read_sink_bool`, `read_sink_uint`, `parse_u64`, `unsigned_value`) — what is
duplicated is the per-field *invocation schema*: key name, target member,
range, diagnostic text, and the loop around the sink sub-object.

## What Changes

- Extend `include/nexigen/core/settings_io.hpp` with a declarative table:
  `FieldSpec` — one entry per JSON key: `{key, read(doc, target, diags, source),
  write(j, target)}` as static callable entries (function pointers over
  module-local lambdas — no `std::function` heap churn; cold path may allocate
  anyway per mandates 4/12, but the table stays static) — plus generic
  drivers `read_fields(...)` / `write_fields(...)` that iterate the table once
  and centralize the sink-subobject handling.
- Migrate the three loaders (chrono, metrics, tracing) to per-module field
  tables. Entry order = current read order, so diagnostic ordering, wording and
  defaults stay byte-identical (settings tests assert exact diagnostic strings).
- `src/logging/settings.cpp` has a genuinely different multi-sink shape: evaluate
  whether it fits the table; default is leave it (no forced fit), record the
  decision in the commit message.
- Implementation update (tasks 3.1): the acceptance required the whole
  settings↔settings clone cluster gone — after the field/to_json lifts the
  surviving pieces were the loader skeleton and the env-overlay scaffolding,
  so they were lifted the same way (design D5-D7). Scope grew from ~650 to
  ~1080 duplicate tokens eliminated; same cold-path-only guarantees.

## Capabilities

Pure refactor — loader results, diagnostics, defaults and JSON shape are
unchanged. `skip_specs: true`.

## Impact

- Init/cold path only: file I/O, JSON and `std::string` never reach a producer,
  snapshot or sink hot path; runtime behavior of running systems is bit-identical.
- Allocations: unchanged — the loaders already allocate on the cold path; the
  table itself is static storage.
- Removals: ~250–350 duplicated lines; a new settings field costs one table entry.
- Risk: diagnostic order or text drift. Mitigation: settings tests
  (`tests/logging/settings_test.cpp`, chrono/metrics/tracing settings tests)
  pass unmodified; re-run the clone scan to confirm the cluster is gone.

## Ordering note

Independent of `refactor-enum-traits-helper` (that change keeps
`from_string` signatures intact), but lands after it to avoid touching
`*_settings.cpp` while enum glue migrates.
