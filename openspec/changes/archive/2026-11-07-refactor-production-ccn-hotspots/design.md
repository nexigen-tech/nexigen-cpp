# Design

## Context

See `proposal.md` — Why. All four functions are cold-path:
`Infrastructure::initialize` and `FileLogger::initialize` run once per
process, `consume_loop` runs the provider's background consumer thread, and
`safe_id` validates run/instance identifiers during identity loading. None is
on a producer hot path.

Project mandates that matter here: `core::Status`/`std::expected` errors (no
exceptions), no heap allocation introduced on hot paths, header-only templates
must stay ODR-safe, and every public signature stays unchanged.

## Goals / Non-Goals

**Goals:**

- Bring the four production functions to CCN ≤ 15.
- Preserve error codes, rollback order, worker ordering, and identifier
  grammar exactly.
- Keep the refactors local: private members / one file-local predicate only.

**Non-Goals:**

- No API, behaviour, or on-disk contract changes.
- No changes to tests, examples, Python helpers, or the `alignas` lizard
  false positives.
- No hot-path restructuring.

## Decisions

### 1. `safe_id` — extract the character predicate

Add a file-local `constexpr bool allowed_id_char(char) noexcept` next to
`safe_id` in `src/app/telemetry_layout.cpp` and call it from the loop.

The grammar (`A-Z`, `a-z`, `0-9`, `.`, `_`, `-`) and all rejection rules
(empty, >128, `.`, `..`, any `..` substring) stay byte-identical. `safe_id`
drops to a handful of decisions; the predicate itself stays well under 15.

### 2. `FileLogger::initialize` — validation predicates

Add private static member helpers to `FileLogger`:

- `valid_path(std::string_view, std::size_t max)`: non-empty, length ≤ max,
  no embedded NUL (for `base_dir` and `file_name`).
- `valid_optional_text(std::string_view, std::size_t max)`: length ≤ max, no
  embedded NUL (for `app_name`, which may be empty).
- `valid_file_logger_config(const FileLoggerConfig&)`: combines the two path
  checks, the app-name check, the throttle rule, and the custom-rotation rule.

`initialize` becomes: validate → build `FileSink::Settings` → copy →
configure producer → calibrate → start worker. The order and every
`core::ErrorCode` mapping are unchanged. Private static members keep the
header ODR-safe and do not change the object layout.

### 3. `Infrastructure::initialize` — construction phases

Add private member helpers to `Infrastructure`:

- `core::Status fail(core::Status)`: `rollback()` then return (replaces the
  current `fail` lambda).
- `core::Status emplace_checked(core::Status, bool constructed)`: maps a null
  component to `InvalidArgument` and rolls back (replaces the current lambda).
- `core::Status build_time_and_logging(const Settings&)`: chrono sink,
  chrono provider, logger, bridge, chrono start.
- `core::Status build_metrics(const Settings&)`: metrics sink, provider, and
  the `Required` startup-status check.
- `core::Status build_tracing(const Settings&)`: trace sink, provider,
  initialize.

`initialize` itself keeps:
1. `initialized_` / `shutdown_started_` guards.
2. signal-owner CAS + `SIGINT`/`SIGTERM` mask.
3. the three build phases in the same order.
4. `signals_.start(this, mask)`.
5. `initialized_ = true`.

Every failure still funnels through `rollback()`, restoring the previous mask,
releasing signal ownership, and resetting components. No member is added, so
the layout is unchanged.

### 4. `TracerProvider::consume_loop` — worker phases

Add private out-of-line helpers (declared in the class, defined next to
`consume_loop` after the class):

- `bool start_worker()`: self-pin, publish the mask, sink cold start, publish
  `start_.code`/`start_.started`; returns false on a `Required` thread-spec
  failure (no sink start, no drain loop).
- `std::size_t deliver_batch(Record&)`: pop/deliver up to 64 records.
- `void answer_flush()`: publish a pending flush request when the ring was
  momentarily empty.
- `void shutdown_drain(Record&)`: drain everything still queued, publish the
  final flush, then stop the sink.

`consume_loop` becomes start → tuning → bounded drain loop with idle backoff →
shutdown drain. The `Required` failure path, first-error precedence
(`attributes` before sink start), 64-record batch, `spin_budget` and
`backoff_us` tuning, `publish_sync` sequence, and sink-stop ordering are
unchanged. The helpers are template members, so `provider.cpp`'s explicit
instantiation still covers them.

## Risks / Trade-offs

- [Startup order accidentally changes and a test observes a different
  `worker_start_code`/mask] → Keep the exact statement order inside
  `start_worker`; the tracing provider tests assert the snapshot fields.
- [Rollback skipped on a new failure branch] → Every build helper returns
  through `fail`/`emplace_checked`; run the app-suite and hub tests.
- [Validation predicate loosens/tightens the identifier grammar] → Keep the
  comparison form verbatim; telemetry-layout tests cover accepted/rejected ids.
- [Worker helpers are template-only and not instantiated] → Declarations stay
  in the class and definitions stay in the header next to `consume_loop`;
  `provider.cpp` explicitly instantiates the default parameters.

## Migration Plan

No data or API migration. Land as one commit; rollback is a revert of the
four touched files. No configuration changes.

## Open Questions

None.
