# Design

## Context

Ten P1 defects after the P0 change (files/lines verified against working tree;
the P0 change already moved the flush/ handshake/ containment items):

- `Runner::stop`/`status` kill or classify by recorded pid only
  (`runner.cpp` 851–958) — PID-recycling can SIGKILL an unrelated group.
- `supervise_child()` breaks out of `waitpid` retries on `errno != EINTR`
  (`runner.cpp:266`) and the post-SIGKILL leg sets `reaped = true`
  unconditionally (`runner.cpp:287–293`): an unreapable child presents a stale
  status that attribution may stamp into exit.json as `terminated_by: self`
  with a foreign exit code.
- The launch caller attempts one `WNOHANG` waitpid on its supervisor after a
  failed handshake (`runner.cpp:818–821`, same pattern
  `app_suite.cpp:1427–1431`): the supervisor can outlive that single attempt
  as a zombie.
- CLI-path env validation checks NUL in keys only (`runner.cpp:103–109`);
  suite manifests already reject NUL in values — asymmetric precondition.
- `SpamProtection::set_config` stores a plain struct (`spam_protection.hpp:41`)
  while producers load its fields on the hot path (`producer.hpp:306–309`
  reachable through `set_spam_protection_config`): the spec promised
  "wait-free reconfiguration"; a foreign write is a data race.
- Two dead duplicate headers `logging::detail::SpscShardRing` /
  `SpscShardPool` (analysis #7): no seq counters, unrouted everywhere.
- `core::ShardPool::claim_slot()` TLS cache holds 4 entries
  (`shard_pool.hpp:142`); the overflow comment says "keeps producing" but each
  overflow call burns a global registration slot — a thread with >4 pools
  exhausts the pool and drops everything (analysis #10).
- `MetricProvider::get_meter` (`provider.cpp:174–223`) mutates the meter arena
  and `meters_[]` outside `harvest_mutex_` while the harvester resets pools
  under it (analysis #8).
- `TracerProvider::this_thread_slot()` truncates int32 → int8
  (`tracer_provider.hpp:271–273`) with no invariant pinning MaxThreads ≤ 127
  (analysis #11).
- `ChronoProvider::start(Logger&)` (`chrono_provider.hpp:168–175`) writes
  `log_binding_` while a running worker reads it through `log()` (analysis
  #12).
- Hub binds only `AF_INET` (`http_server.cpp:187–240`) and the CLI loopback
  check truncates `::1` to `::` (`tools/nexigen-hub/main.cpp:115–124`,
  analysis #13).

Constraints: C++23 `-fno-exceptions -fno-rtti`, `-Werror` GCC+Clang, Result/
Status (never `.value()`), zero hot-path allocations and lock-free hot paths,
TSan/ASan CI gates, cold paths may mutex (metrics harvester precedent).

## Goals / Non-Goals

**Goals:**

- Kill-by-identity: no signal to an unverified process group; keep the recorded
  pid as a strong default with a machine-checkable identity anchor.
- Never fabricate a "reaped" outcome from a waitpid error; failed starts reap
  their supervisor.
- Make every runtime-facing reconfiguration/config/data path either single-
  writer or explicitly field-atomic; thread sanitizer clean on a new test
  matrix.
- Delete the dead SPSC duplicate together with its specs.
- IPv6 listening with the same operator-warning discipline as IPv4.

**Non-Goals:**

- P2 residuals: LTO wiring, hex formatting, batch-drop counters, Options↔engine
  drift, Gauge semantics, Composite flush symmetry, SignalWatcher guard,
  SIGPIPE, JSON-writer unification.
- Linux-only machinery beyond `/proc`-based identity (the project already
  targets Linux; `signal()`/`waitpid`/`/proc` are the norm).
- General process-manager hardening (cgroups, subreapers, PIDFD redesign).

## Decisions

### D1 — Identity anchor is `proc_start_ticks`, recorded at spawn, compared from `/proc`

`InstanceMetadata` gains `proc_start_ticks` (`uint64_t`, 0 = unknown): the
supervisor reads `/proc/<child>/stat` field 22 (starttime, clock ticks since
boot via `sysconf(_SC_CLK_TCK)`) while it still owns the just-forked child and
writes it into resource.json next to `start_time_mono_ns`. At stop/status the
runner re-derives the live process's next pid/system-wide tick value and
compares; only equality proceeds to signals.

Representation choice: raw kernel ticks instead of converting boot-time↔
CLOCK_MONOTONIC_RAW at both ends (would need `CLOCK_BOOTTIME` anchoring and a
suspend-tolerance window). Ticks-to-ticks comparison is exact, cheap, and
survives both a recycled PID and a different boot. A missing/unreadable
`/proc/<pid>/stat` (foreign containers, hardening) or a `0`/absent field keeps
the legacy single-check behavior (old files keep working; new failures are not
introduced).

`stop()` applies the gate once before the first `kill(-pid, …)`; a failed gate
follows the existing "dead instance" fallback (identity-guaranteed residual
risk is gone, but the instance is finalized like today's dead case). `status()`
classifies `running` only on identity match. Composition with suite
supervision: `app_suite` gets the same helper through shared
`detail::sys_helpers` but its stop-safety story is unchanged this change.

### D2 — `waitpid` outcome model: `status_valid`

`SuperviseResult` gains `status_valid` (set only on a true waitpid success).
Every error leg (≠EINTR, including ECHILD after SIGKILL) leaves
`reaped=false, status_valid=false`; attribution maps that to
`TerminatedBy::Unknown`, and the exit record writes `terminated_by: "unknown"`,
`exit_code: null`, `signal: null`. The SIGKILL blocking leg retries on EINTR
only and reports the ECHILD/error result honestly instead of pretending reaped.
The idle-timeout loss of exit notice degrades to the same unknown record rather
than a foreign status.

### D3 — Failed starts reap the supervisor: bounded blocking wait

After the failed-handshake branch (EOF/no report/invalid pid) the launch caller
runs `waitpid` retrying EINTR in a bounded loop (poll deadline ≈ 5 s; the
supervisor path exits self-terminally after reporting) and then one blocking
waitpid as the last resort; if all that fails it logs `unknown` child state
(patently abnormal — fault injection may keep the supervisor alive
deliberately only under tests). The same helper is reused by `start_suite`.
A fault-injection-configured supervisor refusing to exit keeps the command's
existing failure semantics (SystemError) — no new hang class.

### D4 — Env values are NUL-screened on the CLI path

`Runner::validate_options` (the pre-fork check) extends the existing
key/NUL-entry check with `value.find('\0')`; suite manifests already validate
the same precondition, so both launch paths converge.

### D5 — Spam config: per-field atomics (honest "race-free", not "wait-free-linearized")

`SpamProtectionConfig` hot-path fields consumed by producers —
`enable_repeat_suppression`, `repeat_suppression_window_ms`,
`bypass_min_level`, `max_messages_per_sec_per_shard` — are stored as
`std::atomic<T>` members inside `SpamProtection` (relaxed load on the hot
path, plain-size writes in `set_config`). Cross-field skew for one in-flight
window is acceptable and documented; no torn field value is observable. The
`SpamProtectionConfig` struct type and its serialization stay intact as the
transfer type; `set_config` is the only writer (runtime reconfiguration path),
the constructor initializes atomics from its argument. Hot-path cost: identical
"one relaxed load" class as today per taken branch; zero allocations; no
fences.

### D6 — Delete `SpscShardRing`/`SpscShardPool` headers and their specs

The live engine is `core::ShardPool`; the unwired pair misleads readers and
the spec. Files deleted; `tests/logging` referenced only their bound-free
contracts — nothing to port (verified by grep). Spec deltas REMOVE the three
requirements that described the pair so `openspec/specs/logging/spsc-*` is
archived at change-archive time. The producer's stale
"wiring in the follow-up change" comment (`producer.hpp:318–319`) is rewritten
to reference the live pool's fixed DropNewest policy.

### D7 — `claim_slot()` TLS cache 4→8 with no-burn overflow

TLS map widens to 8 (5–8 covers logging·metrics·tracing·chrono·diagnostics +
headroom; 8·16 B = 128 B/thread stack-resident). Steady path (hit) unchanged:
zero shared atomics. When no entry exists and the map is full, the claim
returns -1 and counts `slot_exhausted` (no `fetch_add` on
`registration_.next_slot` — the per-record global-slot burn disappears). Header
comment corrects the previous "keeps producing" wording; the
`logging/multi-producer` requirement carries the same clause.

### D8 — `get_meter` under the harvest mutex

`get_meter` takes `harvest_mutex_` for lookup+create+publish (same lock scope
the harvester/reset paths already use), release before meter use. The id-do-
lookup loop, arena bump, and `meters_[slot] = meter` all sit inside one
critical section; the meter constructor runs inside it as well (arena bump
must be atomic with publication). Cold path only (creation code path); hot
instrument paths never touch it — unchanged.

### D9 — Tracing slot invariant pinned by static_assert

`TracerProvider` derives its span-record slot field as int8;
`static_assert(MaxThreads <= 127, "slot fits int8 span records")` at the
provider scope (matching the in-flight `MaxThreads` param). Keeps headers'
templated-instantiation honest; TSan surfaces nothing new.

### D10 — Chrono worker logger start guard: reject with `Conflict`

`start(Logger&)` checks an int-atomics-based `started_` flag (0 = never
started, 1 = running). If the provider's worker is already joinable, the call
returns `Conflict` and does not touch `log_binding_` — workers only read the
binding, and either zero, one, or the first writer existed before the worker's
thread-creation happens-before edge published the worker. A second
`start(Logger&)` on a stopped provider is acceptable (rebinding) but out of
this change's scope.

## Risks / Trade-offs

- [stop refuses to signal with a recycled PID] → A legacy resource.json
  without the anchor keeps legacy behavior; identity gate failure now
  finalizes through the dead-instance path (same as a dead pid) — a signable
  mismatch is by definition an unverified group.
- [/proc field-parity] → Kernel-side starttime ticks are stable across the
  versions the project targets; a strict static reading (documented) avoids
  setting a tolerance window that weakens the gate.
- [configure-in-place spam fields] → The transfer type keeps `bool`/scalar
  layout via atomics — same sizes; `to_json`/`from_json` on the config struct
  continue defaulting (P2-19 fields cleanup lands separately).
- [TLS cache 8] → 128 B/thread thread-local footprint; >8-pool threads become
  counted drops rather than infinite per-record registration burn.
- [IPv6 dual-stack `::`] → `IPV6_V6ONLY=0` is a single-socket dual listen;
  some hardened kernels default `net.ipv6.bindv6only=1`, we set it explicitly
  per bind and document the operator-visible result; the loopback warning
  continues to classify correctly.
- [CMD-line IPv6 without brackets] → A bare `<addr>:<port>` form requires the
  address to contain ≤ 1 colon (e.g. `::1:8080`); full-form addresses beyond
  one colon must use the bracket notation; parse rejects ambiguity with
  InvalidArgument and stays honest in the usage text.

## Migration Plan

No external API change: `resource.json` gains one additive optional field that
older readers ignore, and a `proc_start_ticks`-absent file keeps the legacy
trust path, so pre-anchor instances remain operational (documented residual
risk). No on-disk formats change beyond that. Rollback = revert the change;
the SPSC-shard-ring/pool spec removals take effect at change-archive time.

## Open Questions

(none — all decisions verified against the working tree before drafting)
