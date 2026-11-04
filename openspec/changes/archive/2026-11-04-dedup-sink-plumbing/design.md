# Design

## Context

All four copy-groups (composite fan-out ×3, file-sink fd plumbing ×4, settings loader remnants ×3, `nexigen-run` report tails) were confirmed against source with per-line reads; exact duplication boundaries line in `proposal.md` → Why. Constraints that shape the design:

- The codebase keeps **exactly-one-implementation precedents** in `nexigen::core` (`fixed_writer.hpp`, `settings_io.hpp`) with explicit placement rationale.
- **Placement constraint** — `core::fixed_writer.hpp` avoids `nexigen::core::detail` "so `using namespace nexigen::core` scopes never learn a second bare `detail::` candidate name" (module TUs use `using namespace nexigen::core` plus their own `detail` namespaces).
- C++23, `-fno-exceptions -fno-rtti`, `core::Status` error handling, no nlohmann/heap on hot paths (project context).
- See proposal.md — Why.

## Goals / Non-Goals

**Goals:**

- One variadic fan-out scaffold used by the three composite sinks (and available to future module consumers, e.g. a future composite_logger migration).
- One file-descriptor write/open/fsync path used by the four file-backed sinks.
- One settings-loader vocabulary for the three `*_settings.cpp` loaders (finally incl. chrono).
- One CLI report path in `nexigen-run`; externally byte-identical CLI and `runner.jsonl` output.

**Non-Goals:**

- No public API changes anywhere: composite `get<I>`/`get<T>`/`size`, composite constructors, file-sink constructors/`flush()`/`to_string`, CLI output text, `runner.jsonl` event shape all stay as-is.
- No migration of `logging/composite_logger.hpp` (stateful `set_level` tree walk + initialize orchestration — a later change; see Deferrals).
- No `HttpServer::Impl` split, no `run_instance_supervisor` decomposition, no `affinity.cpp` unification (documented intentional copy).
- No new concurrency, no hot-path code movement: every touched execution path is cold (construction, rotation, flush) or compile-time.
- No behavioral hardening rides along: if a migration touches a behavioral bug candidate, it is recorded as TODO instead of being fixed in the same commit as the copy-paste move.

## Decisions

### D1 — Placement: `nexigen::core::sink_pack` and `nexigen::core::sink_io` (named sub-namespaces, not `core::detail`)

Two new flat headers `include/nexigen/core/sink_pack.hpp` and `include/nexigen/core/sink_io.hpp`, each exposing one named namespace: `nexigen::core::sink_pack` (fan-out pack) and `nexigen::core::sink_io` (fd plumbing). Rationale: `settings_io` already established "dedicated name, never a second bare `detail` candidate" for shared cold-path plumbing; the same rule applies here since composite headers and settings loaders are exactly the TU types that mix module namespaces with `using namespace nexigen::core` in tests.

Alternatives: `core::detail::…` (rejected — re-introduces the "second bare `detail::`" hazard the `FixedWriter` header documents); sinking into each module (rejected — the copies would remain copies).

### D2 — Fanout shape: public non-virtual base `Fanout<Sinks...>`, moved verbatim

`sink_pack::Fanout<Sinks...>` holds `std::tuple<Sinks...> members_` and the compile-time surface copied **verbatim** from the current composite headers (single source of truth, no rewrite):

- `static constexpr std::size_t size = sizeof...(Sinks)` — keeps `CompositeMetricSink<Sinks...>::size` (used by those headers' `to_json`) resolving via public inheritance.
- `template <std::size_t I> get()`, `template <class T> get<T>()` with the existing `find_index<T>()` + `exactly_one<T>()` static_assert pair and its exact message text (compile-time contract preserved).
- `for_each(Fn&&)` as the existing `std::apply([&fn](Sinks&... sink) { (fn(sink), ...) })` — comma-fold evaluates left-to-right, preserving documented pack-order delivery.
- `combine(core::Status, core::Status)` as an inline free function in `sink_pack` (first failure wins; used via fold in every composite body).
- Default constructor constrained by `requires std::is_default_constructible_v<std::tuple<Sinks...>>`; member-injection constructor moves the pack into the tuple (the D3 member-injection construction semantics the thread sinks rely on).

Each composite becomes `template <class... Sinks> class CompositeXSink : public nexigen::core::sink_pack::Fanout<Sinks...>` and keeps only: its payload delivery method (`on_snapshot`/`on_batch`), its `to_json` line, its module `static_assert(concepts::XSink<...>)` gate, and its constructor delegating to the base. Inheritance (vs. member-holder + forwarding wrappers) was chosen because it preserves the exercised API (`composite.get<0>()`, `&composite.get<ConsoleMetricSink>()` in `metrics_test.cpp:1242–1243`, the `logging_test.cpp:441` pattern, examples in `examples/logging/ex_composite_logger.cpp`) with zero new forwarding code and no behavioral delta. No virtual functions, no additional alignment — the tuple layout is unchanged from today's composites, so no `alignas` decisions arise.

Alternatives: CRTPbase (rejected — nothing needs the derived view); a free helper over externally held tuples (rejected — drops the constructor constraints and moves lookup complexity into every header).

### D3 — `sink_io`: three small free functions, sinks keep their public shapes

`include/nexigen/core/sink_io.hpp`, namespace `nexigen::core::sink_io`, all `noexcept`, all cold-path:

```cpp
[[nodiscard]] int   open_output(std::string_view path, bool append) noexcept; // O_WRONLY|O_CREAT|O_CLOEXEC|(append?O_APPEND:O_TRUNC), 0644
[[nodiscard]] core::Status write_all(int fd, const char* data, std::size_t bytes) noexcept; // EINTR+partial loop, SystemError on failure
[[nodiscard]] core::Status fsync_fd(int fd) noexcept; // ::fsync(fd), SystemError on failure
```

Call-site mapping (behavior-preserving):

- `json_file_metric_sink.hpp` — ctor `::open` → `open_output(path, config.append)`; private `write_all(size_t)` bool loop → deleted; body becomes `return core::sink_io::write_all(fd_, scratch_, bytes)` + the existing `if (!...) return unexpected(SystemError)` mapping; `flush()` `::fsync` → `fsync_fd`.
- `json_file_chrono_sink.hpp` — same swap; its no-arg `write_all()` maps to `write_all(fd_, scratch_, len_)` Boolean mapping (unchanged at the call site).
- `json_file_trace_sink.hpp` — `open_for_window` keeps its window-filename logic, calls `open_output(std::string_view name, /*append=*/true)`; the `NOLINT(bugprone-suspicious-stringview-data-usage)` guard disappears because `open_output` now takes the path via `std::string` copy (one cold-rate allocation at rotation frequency — none in the span path); `write_all(std::size_t)` returns the shared `Status` directly (its current Status contract is exactly the shared one).
- `chrome_trace_sink.hpp` — `start()` truncating open → `open_output(config_.path, /*append=*/false)`; `write_bytes` keeps its bool shape for its callers and its body becomes `if (!sink_io::write_all(fd_, data, bytes).has_value()) return false;` — the byte-identity map (`ok` → `true`, failure → `false`) stays at the call site, exactly as it does today.

Per-sink move/attribute/destructor state machines stay untouched (their moved members and the deleted move-assignment rules differ per class; unifying them would change contracts, see Non-Goals).

Alternatives: a full `FileSink` RAII base class absorbing fd lifecycle (rejected — the four sinks' move/destructor semantics are deliberately different: metric+chrono move-assign, trace/chrome delete it; merging would either weaken contracts or fork the base; the loop itself is the only genuinely-duplicated code); stdcall wrappers returning bool (rejected — project idiom is `core::Status`, and two of the four call sites already use it).

### D4 — `settings_io` additions: the driver vocabulary, exact signatures and messages pinned

Add to `settings_io` (all current local copies live at `metrics_settings.cpp:48–96 / 100–113`, `tracing_settings.cpp:47–… / 230–…`, `chrono_settings.cpp:64–130`):

```cpp
void read_uint(...);                 // identical ×3  ("expected an integer in range")
std::string sink_field(const char*); // identical ×3  ("sink.<key>")
const json* find_sink(...);          // identical ×3  ("expected a JSON object")
void read_sink_string(...);          // identical ×3  ("expected a string")
void read_sink_bool(...);            // metrics + chrono ("expected a boolean")
void read_sink_uint(...);            // tracing + symmetry ("expected a non-negative integer")
```

Signature rule: hoisted helpers keep the parameter lists and diagnostic strings byte-identical. `read_sink_uint` goes even though tracing is its only user — the alternates caused the naming divergence noted in the proposal, and the helper is 6 lines with zero extra surface.

`chrono_settings.cpp` migration: delete `FileRead`, `read_settings_file`, `add_diagnostic`, `unsigned_value`, `parse_u64` (all core-owned today) plus the driver-trio helpers hoisted above, and add the same using-declaration block metrics/tracing already carry (mirrors `metrics_settings.cpp:41–45`). Metrics/tracing lose only their local definitions of the hoisted six (their help stays in the anonymous namespace; the env-var loops and per-field wiring remain module code).

Diagnostic text verification: `grep` asserts that every string in the old local table is reproduced from the shared body; the module settings suites (chrono/settings, metrics/settings, tracing/settings) already pin the texts — they must pass unchanged.

### D5 — CLI: file-local helpers in one TU, byte-identical output

`tools/nexigen-run/main.cpp` gains two static helpers (same TU; nothing leaves the tool namespace — `nexigen-hub` needs no reporting and `tools/detail/cli.hpp` stays parse-only):

```cpp
namespace { // file-local
[[nodiscard]] int cli_fail(const char* command, const char* mode, const std::string& target,
                           std::string_view diagnostic, const char* stderr_message);
[[nodiscard]] int cli_ok(const char* command, const char* mode, const std::string& target,
                         const char* verb); // "started: %s\n" / "stopped: %s\n"
[[nodiscard]] bool suite_target_options_conflict(const std::string& config,
                                                 const std::string& suite_dir, bool have_dir, int* exit); // mutual exclusivity block ×2
}
```

`cli_fail` emits exactly the current 3-line tail (`log_command(...,"error", diag)` → `finish_logging()` → `fprintf(stderr, "nexigen-run: %s: %s\n", stderr_message, diag)`) and returns `exit_failure`. Rewired call sites in `cmd_start`/`cmd_stop`/`cmd_status`: 12 failure tails and 4 success tails go through the helpers; two paths stay bespoke because byte-identity rules them out of the helper (design decision recorded on task 6.2 rather than a `cli_fail_plain` overload: the `suite metadata unreadable` tail prints the suite DIR as the stderr detail — not a message string — and the `cmd_status` suite-ok tail prints a state listing rather than a plain ok line). The `--config`/`--suite-dir` argument fetch blocks and the exclusivity validation collapse into the helpers; `--grace`/`--json` stay command-specific.

Byte-identity is enforced by existing `runner_test`/`hub` CLI suites plus the `process-supervision` CLI assertions (`start failed`, `stopped: <dir>`).

### D6 — Verification per module, in migration order

Compile-and-test gates run **at each step**, not only at the end — matrix in `tasks.md` (Debug, Release `-O3 -march=native -flto` with latency gates, TSan, ASan, plus the clang-tidy check target). Core tests extend `tests/core/core_test.cpp` (single TU there; no CMake list changes needed beyond what exists).

### D7 — Serialization mandate scope (the rules question the spec raises explicitly)

No new enum/struct/class takes `to_string`/`to_json`/`formatter` surface: `Fanout` and `sink_io` types are plumbing with no identifying state of their own beyond their disposable members; every serializable concrete type granularity stays at the composite/sink level, where it already exists. This mirrors `fixed_writer.hpp` (a core class with no `to_string`, whose per-module `json_writer` aliases carry the contracts). The spec's "Plumbing serialization scope" requirement documents the exemption; existing composite `to_json` and file-sink `to_string` contracts are regression-guarded by untouched module tests.

## Concurrency / memory layout

No new atomics, mutexes, or threads; no memory-layout decisions change. `Fanout` stores `std::tuple<Sinks...> members_` — the identical layout composed sinks have today (no `alignas` on plumbing; it never shares cache lines across threads). `sink_io` stateless functions only. The rules-required `alignas`/shard sizing/`memory_order` fields therefore: **not applicable** — explicitly no concurrent/atomic component is added.

## Migration order (implementation sequence)

1. `sink_pack.hpp` + core unit tests (additive; nothing else builds on it yet)
2. `sink_io.hpp` + core unit tests (additive)
3. Migrate the three composite headers (their modules only)
4. Migrate the four file sinks (with `fsync`/`open` swaps)
5. `settings_io` + loader migrations (chrono → core helpers; metrics/tracing drop locals)
6. `nexigen-run` helpers rewiring
7. Full verification matrix per D6

Each step keeps the repo green; a step's commit diff is reviewable in isolation.

## Risks / Trade-offs

- **Fold/iteration-order or lookup drift during the copy**: mitigated by moving implementations verbatim into `sink_pack` (not rewriting), keeping per-module `static_assert`s and all module suites. The compile-time surface (find_index sentinels, exact static_assert message) is pinned by tests ported into `core_test.cpp`.
- **`CompositeX<Sinks...>::size` dependent-name regression in `to_json`**: inherited public static member resolves through the derived template as today; the migration keeps the exact expression; traced by existing tracing/metrics snapshot tests asserting member counts.
- **`read_sink_uint` hoisting is a small speculative API**: accepted because the per-module copies caused the naming divergence recorded in the proposal; cost is a 6-line function in an existing vocabulary namespace.
- **EINTR/partial coverage on real fds**: the partial-write scenario is provable with a non-blocking pipe; triggering real `EINTR` requires a `sigaction` probe without `SA_RESTART` during a blocking write (flaky-free design: the handler is installed per test). If the EINTR probe proves flaky in CI, the fallback (documented in tasks) is keeping the retry-loop test as a partial-write test plus a review-gate comment in `sink_io.hpp` — the loop body is identical code moved from four verified copies.
- **Trade-off: two small namespaces instead of one sink module**: deliberate — `sink_pack` (template-pack) has a different include profile (no syscall headers) than `sink_io` (POSIX fd), and per-consumer includes stay minimal for compile-time fashionable composite headers included by examples.
