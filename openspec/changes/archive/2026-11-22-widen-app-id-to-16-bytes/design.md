# Design

## Context

See `proposal.md` — Why. The decision record is `docs/adr/0004-app-id-identity.md`. The
constraints that shape the approach:

- Identity today has three independent sources: the manifest `app` name, the `--app-name` argv
  entry injected by `ExecutionManager`, and the compile-time `Iceoryx2Policy<A,B,C,D>` tag. Only
  the last one reaches the iceoryx2 frames; the policy receives `app_name` in its hooks and ignores
  it (`include/nexigen/app/policies/iceoryx2_policy.hpp:81,93,106`).
- `Settings::app_name` is populated from `-an/--app-name` (`src/app/command_line.cpp:452`) and is
  passed to every policy hook from `Infrastructure` (`include/nexigen/app/infrastructure.hpp:760,
  788,815`).
- The five wire types pin their layouts: `LogFrame` 256 B / `alignas(64)`, `SpanFrame` 384 B /
  `alignas(64)`, `EventHeader` 72 B / `alignas(8)`, `TransportHeader` 24 B, and `MetricFrame`
  unpinned but containing exactly one `MetricRecord`.
- Manifest parsing is non-throwing and leaves the target unchanged on hostile input; validation runs
  afterwards and returns `InvalidArgument`. The schema is `version: 1` and ignores unknown fields,
  so an additive field preserves compatibility.
- The platform forbids exceptions/RTTI (`-fno-exceptions`, `-fno-rtti`), heap allocation on hot
  paths, and `std::mutex` on hot paths. Cold-path control work may use file I/O and `std::string`.
- The suite manifest is the unit of coordination: one document describes one suite, and the
  ExecutionManager is not expected to know about other suites running on the same platform.

## Goals / Non-Goals

**Goals:**

- One authoritative identity per logical application, derived from the manifest and visible in
  every telemetry stream.
- A fixed 16-byte representation with no truncation and no runtime allocation on the formation path.
- A collision policy that rejects duplicates when the manifest is loaded, before any process starts.
- A staged migration that keeps old recorder sessions readable and makes mixed wire versions fail
  loudly.

**Non-Goals:**

- Cross-suite uniqueness enforcement. Two suites may declare the same identifier; the change does
  not add a registry, lock files, or an admission-time check across suites.
- Dynamic identity changes at runtime. The identifier is fixed for the suite lifetime, like the
  application name.
- Replacing the PHM identity claim or adding authentication to `app_id`.
- Changing service names, channel naming rules, or the hub read model.

## Decisions

### D1 — `core::AppId` is a 16-byte value type

Add `include/nexigen/core/app_id.hpp`:

```cpp
struct AppId {
    char bytes[16]{};   // zero-filled tail; a 16-byte id has no NUL
    // size(): index of the first NUL, 0..16
    // text(): string_view(bytes, size())
};
static_assert(sizeof(AppId) == 16);
static_assert(std::is_trivially_copyable_v<AppId>);
```

- Construction from text is fallible (`core::Result<AppId>` or a `bool`-returning validator used by
  the manifest and CLI); it rejects empty input, >16 bytes, embedded NUL, and any byte outside
  `[A-Za-z0-9_-]`. It never truncates.
- No length field: `size()` scans at most 16 bytes and `text()` returns a view. This keeps the type
  exactly 16 bytes and `memcpy`-compatible with the wire field.
- A `consteval` `from_literal` factory lets a compile-time literal (the legacy policy tag) become
  an `AppId` without allocation. It checks the alphabet during constant evaluation and reaches a
  non-constant `std::abort()` on an invalid literal, so such a call fails to compile instead of
  producing a malformed value.
- `to_string`, `std::formatter`, and non-throwing `to_json`/`from_json` mirror the other core value
  types. JSON carries the rendered string.
- Placement in `core` avoids a dependency cycle: `com`, `logging`, `metrics`, and `tracing` all need
  the type, and none of them may depend on `app`.

Alternative considered: `std::string` everywhere. Rejected: heap allocation and unbounded width,
and the wire field is fixed anyway. Alternative: a 17-byte struct with an explicit length.
Rejected: breaks `sizeof == 16` and the direct `memcpy` into frames.

### D2 — Text at the edges, `AppId` inside

Raw manifest and CLI input stays textual so the existing non-throwing parsers can report a
diagnostic and leave the target unchanged. The value becomes `core::AppId` only after validation:

| Layer | Representation |
|---|---|
| `AppSpec::app_id` (parsed manifest) | `std::string`; empty = absent |
| `CommandLineOptions::app_id` | `std::string`; empty = absent |
| `SuiteLaunchPlan` | `core::AppId` after resolution |
| `Settings::app_id` | `core::AppId` |
| `*SinkConfig::app_id` | `core::AppId` |
| Wire frames | `char[16]` |

Resolution order for one manifest entry: explicit `app_id`, otherwise `app`, otherwise failure.
A name longer than 16 bytes or containing a forbidden byte fails validation; it is never truncated.
The schema version stays 1 because the field is additive.

Alternative considered: require `app_id` unconditionally. Rejected: it would break every existing
manifest and example even when the application name already fits and is unique.

### D3 — Uniqueness is enforced only inside the suite

- `validate_suite_manifest` resolves every entry's identifier (explicit `app_id`, otherwise `app`)
  and rejects duplicates, empty values, values longer than 16 bytes, and forbidden characters with
  `InvalidArgument`. Validation is pure: it reads one manifest and touches no shared state.
- Suite admission does not consult other suites, does not reserve identifiers, and does not fail
  because another running suite uses the same code. Two suites MAY reuse an identifier.
- Rationale: the manifest is the only document the ExecutionManager is given, and a suite is the
  unit of coordination. Enforcing cross-suite uniqueness would require a host-wide registry, lock
  directory, permissions policy, and crash recovery — all for a property the operator can check
  before launching a second suite.
- Trade-off: if two suites reuse a code and publish to the same services, daemons merge their
  streams under one `app` label. That is an explicit operator choice; the documentation recommends
  distinct codes when suites share a platform.
- No new files, locks, or atomics are introduced. Validation remains cold path and allocation
  behaviour is unchanged.

Alternatives considered: platform-wide reservation with `flock` per identifier. Rejected by scope:
it adds a runtime dependency, a permissions surface, and crash-recovery semantics for a guarantee
the suite boundary does not need. A shared JSON registry. Rejected for the same reason, plus its own
locking and stale-entry reaper. Requiring `app_id` in every manifest. Rejected: see D2.

### D4 — Runtime delivery uses argv, not a new environment variable

`ExecutionManager` already appends `--app-name <app>` to the child argv. It appends
`--app-id <id>` in the same place (`build_argv`). `CommandLineOptions::app_id` populates
`Settings::app_id` in `apply_to`.

- No `NEXIGEN_APP_ID` is introduced. One authoritative channel avoids the argv/environment
  precedence question that already exists for the application name, and `exec/process-supervision`
  explicitly makes argv the delivery path.
- A standalone application can pass `--app-id` itself or set `Settings::app_id` programmatically.
- `RunIdentity` is deliberately unchanged: it models run/instance paths, and `app_name` was already
  excluded from it.

### D5 — Policies receive a `TelemetryIdentity`, not another string

`InfrastructurePolicy` hooks change from `std::string_view app_name` to a small borrowed value:

```cpp
struct TelemetryIdentity {
    std::string_view app_name;
    core::AppId app_id;   // unset = no runtime identity
};
```

`Infrastructure` builds it from `Settings` and passes it to `emplace_logger`,
`emplace_metrics_sink`, and `emplace_trace_sink`. This is a **BREAKING** API change for custom
policies, but it is mechanical and keeps both identity fields together instead of growing the
parameter list. `Iceoryx2Policy` computes the effective id once per hook:

```cpp
const core::AppId id = !identity.app_id.empty() ? identity.app_id : core::AppId(k_tag);
```

`ConsolePolicy` and `NullPolicy` ignore it; `FilePolicy` keeps stamping `app_name` and ignores
`app_id` (file records already carry the long name). Because `AppId` construction is fallible, the
policy validates `A..D` with a `static_assert` against the permitted charset: a policy instantiated
with an invalid tag fails to compile instead of emitting a malformed frame.

Alternative considered: read `NEXIGEN_APP_ID` inside the policy. Rejected: hidden dependency,
untestable precedence, and a second source of truth.

### D6 — Wire layouts

`LogFrame` stays 256 B / `alignas(64)`; `format_version` becomes 2:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | `timestamp_ns` |
| 8 | 16 | `app_id` |
| 24 | 4 | `context_id` |
| 28 | 4 | `thread_id` |
| 32 | 4 | `pid` |
| 36 | 4 | `dropped_counter` |
| 40 | 2 | `payload_size` |
| 42 | 2 | `format_version` |
| 44 | 1 | `log_level` |
| 45 | 211 | `payload` |

`MetricFrame` grows by 16 bytes (12 data + 4 alignment padding):

| Offset | Size | Field |
|---|---|---|
| 0 | 16 | `app_id` |
| 16 | 4 | `pid` |
| 20 | 4 | padding |
| 24 | 8 | `timestamp_ns` |
| 32 | `sizeof(MetricRecord)` | `record` |

`SpanFrame` stays 384 B / `alignas(64)`; `format_version` becomes 2:

| Offset | Size | Field |
|---|---|---|
| 312 | 16 | `app_id` |
| 328 | 4 | `pid` |
| 332 | 4 | padding |
| 336 | 8 | `sequence` |
| 344 | 8 | `publish_ts_ns` |
| 352 | 2 | `version` |
| 354 | 30 | `reserved` |

`EventHeader` becomes 80 B / `alignas(8)`: offsets 0..63 are unchanged; `app_id[16]` occupies
64..79, and the former 4-byte tail padding disappears.

`TransportHeader` becomes 40 B: `app_id[16]` 0..15, `pid` 16..19, padding 20..23, `publisher_id`
24..31, `sequence` 32..39.

`LogFrame` and `SpanFrame` keep their existing sizes, so the transport's payload type-layout check
cannot detect a stale peer; their explicit `format_version` is the guard. `MetricFrame`,
`EventHeader`, and `TransportHeader` change size, so the existing layout check rejects a mismatch.

### D7 — Readers and recorder schemas

- `app_id_text()` on every wire type trims at the first NUL and bounds the view to 16 bytes; a
  16-byte id with no NUL still renders whole.
- `nexigen-logd` already formats with `%.*s`; the charset makes the JSON field safe without an
  escaping change. `nexigen-metricsd` uses `app_id_text()` as the Prometheus `app` label;
  no code change beyond the width.
- `nexigen-recd` built-in schemas change `app_id` from `char[4]` to `char[16]` and shift the
  following offsets (`LogFrame.payload` 31 → 45, `EventEnvelope` payload after offset 80, etc.).
  Channel names stay `log.<id>`, `metric.<id>`, `trace.<id>`, `event.<id>`.
- Sessions are self-describing: the schema is embedded per session, so recordings made before this
  change remain readable by the new exporter.

### D8 — Validation charset is a wire concern, not cosmetics

`[A-Za-z0-9_-]` is the intersection of what is safe in a JSON string, a recorder channel topic, and
a Prometheus label. `.` is deliberately excluded because the recorder uses it as the topic
separator; a dot in the id would make `log.<id>` ambiguous. Quotes and backslashes would break the
daemon's `%.*s` JSON rendering. The limit is exactly 16 bytes, never a rounded or truncated value.

## Risks / Trade-offs

- **Log payload shrinks from 225 to 211 bytes.** → Messages are clipped at the frame boundary and
  `payload_size` reports the stored length. Most records are far below 211 bytes; the alternative
  was a 320-byte frame (+25% ring memory).
- **Two suites can reuse an identifier and merge in daemons.** → Accepted: uniqueness is scoped to
  the suite by decision. The manifest reference recommends distinct codes when suites publish to the
  same services; no admission-time check is added.
- **Defaulting an absent `app_id` to `app` can duplicate ids inside a suite** only if two entries
  share an application name; validation rejects that case with `InvalidArgument`.
- **Wire break with same-size frames.** `LogFrame`/`SpanFrame` layout changes are invisible to the
  size check. → `format_version` 2 is mandatory on both sides; consumers reject anything else.
- **`TelemetryIdentity` breaks custom policies.** → It is a compile-time break with a one-line
  mechanical fix, and the built-in policies cover the common cases.
- **`app_id` can be spoofed.** → It is an observability label; PHM's claim and credentials remain
  the authorization path. Recorded in `core/app-id` and the ADR.

## Migration Plan

1. Land `core::AppId` with its validation and serialization tests; no behavior changes yet.
2. Add the manifest field, in-document validation, and `--app-id` argv delivery. Suites without
   explicit ids keep working through the `app` default; no cross-suite registry is added.
3. Add `Settings::app_id`, `-ai/--app-id`, `TelemetryIdentity`, and the runtime policy resolution.
4. Widen the five wire types, bump the two version fields, and update every reader, schema, and
   test in the same commit. Producers and consumers are rebuilt together; the version fields make a
   partial rollout fail loudly instead of misreading.
5. Update examples, suite manifests, glossary, module docs, and the ADR.

Rollback: revert the commit series. A rolled-back producer/consumer pair sees either a size
mismatch (metric/event/transport) or `format_version` 2 (log/span) and refuses the frame; recorder
sessions written by the new version carry their own schema and stay readable by the old exporter
only if the old exporter understands `char[16]` — otherwise export them with the new build before
rolling back.

## Open Questions

None. The suite-scoped uniqueness rule, the manifest default, the charset, and the wire widths are
decided above.
