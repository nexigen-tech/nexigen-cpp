# Design

## Context

See `proposal.md` — Why. Constraints and precedents:

- The project already splits cold-path serialization this way: `src/logging/console_logger_config.cpp`, `logger_options.cpp`, `spam_protection_config.cpp`, `src/logging/log_frame.cpp`, `src/logging/log_daemon_config.cpp`, `src/metrics/metric_daemon_config.cpp`. All of them keep the trivial free `to_string` inline in the header.
- `chrono` and `metrics` build from `file(GLOB ... CONFIGURE_DEPENDS "src/<module>/*.cpp")`, so a new `.cpp` needs no CMake edit.
- These translation units include `<nlohmann/json.hpp>` first with the `-Wnull-dereference` pragma push/pop used by every existing serialization TU (GCC warns inside the upstream header at `-O3`).
- `JsonFileMetricSink::from_json` does not exist by design ("rebinding opens a file"), and `ConsoleSink::from_json` assigns a fresh sink object from `fd` — both keep their current semantics.

## Goals / Non-Goals

**Goals:**

- Each moved pair is defined exactly once, in the module's compiled library.
- Headers keep only declarations, so consumers stop recompiling the bodies.
- Zero semantic change; the existing tests (round-trip, hostile input, formatter agreement) prove it.

**Non-Goals:**

- Moving the 24 short enum pairs or any template/`constexpr` code.
- Changing `.clang-format`, CMake targets or the audit document beyond its status table.
- Splitting `to_string` out of line.

## Decisions

### D1 — Move only `to_json`/`from_json`, keep free `to_string` inline

The free helper is a single delegation line; the established modules keep it in the header, and moving it would add a symbol for no compile-time gain. The audit's own rule targets the serialization bodies.

### D2 — One `.cpp` per entity, named after its header

Matches the file-per-entity layout mandate and the existing `src/<module>/` naming. `json_file_metric_sink.cpp` hosts both the config pair and the sink's `to_json`, because they belong to the header that declares them; splitting them across files would contradict the layout rule.

### D3 — Declarations in the header carry the attributes; definitions are plain

`[[nodiscard]]` where it exists today stays on the declaration. Definitions in the `.cpp` omit it, following `src/logging/logger_options.cpp`.

### D4 — Symbol check expects weak-to-strong promotion, not new API

Before the move, a consumer TU that used the serializer emitted a weak `W` copy; after it, the library owns one strong `T`. The verification compares the *declared* surface (header declarations unchanged) and checks that every removed symbol is a `W` duplicate of a symbol that now exists as `T` in the library — not that the `nm` output is byte-identical.

## Risks / Trade-offs

- **A header still calls a moved function inline** (for example another inline function calling `to_json`) → a translation unit that does not link the library fails. Mitigation: the callers are tests and tools that already link these libraries; the full build proves it.
- **ODR break if a stale inline definition remains somewhere** → the definition is removed from the header in the same change; the compiler would report a redefinition if it did not.
- **`ConsoleSink::from_json` assigns a temporary sink, which needs move assignment from the library TU** → the type is a small value class; the build validates it.
- **`nm` diff looks noisy** → classify symbols by type and only require "every removed `W` has a matching `T` now", as in D4.
