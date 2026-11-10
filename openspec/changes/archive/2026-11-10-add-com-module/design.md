# Design

## Context

`nexigen::com` is the platform's first inter-process communication layer. The transport is Eclipse iceoryx2 pinned at **v0.10.0** (crates.io stable, 2026-09-18; `main` is at `0.10.999` dev). The C++ bindings live in the same monorepo under `iceoryx2-cxx/` (`iox2::` namespace, C++ over `iceoryx2-ffi-c` over Rust) and expose everything the patterns need: `Publisher`/`Subscriber`/`Sample`, `Notifier`/`Listener`/`EventId`, `Client`/`Server`/`Request`/`Response`, and the blackboard pattern for Fields. The build chain is cargo → `iceoryx2-ffi-c` → `iceoryx2-bb-cxx` → `iceoryx2-cxx`, so the dependency needs the Rust toolchain (rust 1.89) — hence the feature gate.

The existing module conventions apply: file-per-entity, PascalCase types / snake_case functions, `Result`/`Status` without `.value()`, serialization mandates (member+free `to_string`, enum conversion set, `std::formatter`), and the zero-dependency test harness.

## Goals / Non-Goals

**Goals:**
- One thin `nexigen::com` layer over iceoryx2 whose public surface contains no `iox2::` symbol.
- The three patterns (publish-subscribe + event, request-response, field) with a compile-time trivially-copyable payload contract.
- The module builds behind `NEXIGEN_ENABLE_COM`; every other target builds byte-identically without Rust.

**Non-Goals:**
- No multi-transport abstraction (no DDS/SOME-IP backends now); the boundary is clean enough to add one later, but nothing is abstracted today.
- No Rust-facing API yet (C++ only; Rust comes after the C++ module works).
- No integration with `sm`/`phm`/`exec::StateClient` in this change — those consume `com` later.

## Decisions

**D1 — Thin wrapper, no iox2 leak.** `nexigen::com` owns its type surface: `ServiceName`, `Node`, `Publisher<T>`, `Subscriber<T>`, `Sample<T>`, `SampleMut<T>`, `EventNotifier`/`EventListener`, `Client<Req,Resp>`/`Server<Req,Resp>`, `Field<T>`. Each wraps the corresponding `iox2::` handle; `iox2/*.hpp` includes stay inside `include/nexigen/com/` and `src/com/` only. Templates mean wrapper definitions live in headers (the iox2 include is transitive), but the application never names `iox2::` — that satisfies the protocol-independence boundary without building a transport abstraction.

**D2 — Own error vocabulary.** `com::Error` is a separate `uint8_t` scoped enum (not `core::ErrorCode`) with the full conversion set (constexpr `to_string`/`from_string` over `core::EnumTraits`, ADL JSON via `core::mandate`, generic formatter). `com::Result<T> = std::expected<T, com::Error>`; `com::Status = std::expected<void, com::Error>`. A small `src/com/error_mapping.cpp` translates the `iox2::*_error` enums into `com::Error`; unmapped variants collapse to `Unknown`.

**D3 — Message contract.** `com::concepts::Message<T>` is satisfied exactly when `std::is_trivially_copyable_v<T>`; every port template constrains on it. A dedicated negative-compile probe TU (like `tests/core/neg/`) pins that `std::string`/`std::vector`/smart pointers/virtuals fail to compile.

**D4 — Dependency and gate.** `NEXIGEN_ENABLE_COM` (default OFF) guards the `FetchContent` of iceoryx2 and the `nexigen_com` library target. The repo has NO tags/releases, so the pin is commit `b0660b5b3574d3cc18911f3c115e270630120b75` (`main` at `0.10.999` dev, immediately after the crates.io v0.10.0 release; recorded in the CMake comment). `NEXIGEN_ENABLE_COM=OFF` builds no com target and pulls nothing Rust-related.

**D5 — Zero-copy API shape.** Publish: `loan_uninit() -> Result<SampleMut<T>>`, write in place, `send(SampleMut<T>) -> Status` (dropping the loan returns the slot — iceoryx2's loan contract). Receive: `receive() -> Result<Sample<T>>` where `Sample<T>` is a read-only RAII view; drop returns the slot. Type-layout mismatch (publisher/subscriber compiled against different `T`) surfaces iceoryx2's header verification as `Error::IncompatibleTypeLayout`.

**D6 — Pattern mapping.** Publish-Subscribe + Event ride `iox2::Publisher`/`Subscriber` + `Notifier`/`Listener` (REQ_COM_001); Method rides `Client`/`Server` with a client-generated `correlation_id` (REQ_COM_002); Field rides the blackboard pattern with `history_size >= 1` (REQ_COM_003). Overflow policy is `UnableToDeliverStrategy::DiscardSample` for subscribers (REQ_COM_007). The pinned iceoryx2 commit (D4) renamed the mechanism and gates it behind a service flag: the publisher sets `publisher_builder().backpressure_strategy(iox2::BackpressureStrategy::DiscardData)` and BOTH sides set `.enable_safe_overflow(false)` on the service builder (safe overflow would replace the oldest sample instead — no eviction would be observable). With safe overflow off, DiscardData drops the new sample for an overflowed receiver and `send()` reports the number of delivered receivers, which feeds the exact eviction count (D7).

**D7 — Telemetry layout.** The lag diagnostics live in a per-publisher `Telemetry` cell: `alignas(64)` relaxed atomics `queued_samples_`, `lost_samples_`, `send_count_` (+ `peak_recipients_` bookkeeping) — updated with `std::memory_order_relaxed` on the send path (one writer; telemetry has no ordering requirements), read by `to_string()`/the future PHM bridge. `send_count_` = successful sends; `lost_samples_` = evicted samples, derived per send from `delivered < peak_recipients` under the DiscardData policy (exact eviction count); `queued_samples_` = observed fill events (a receiver's queue was full). The `alignas(64)` cell keeps the counters off the payload pool's hot cache lines.

**D8 — Spike first.** The first task proves the dependency: `FetchContent` of the pinned iceoryx2 + cargo chain + a trivial two-process round-trip outside the com API, before any wrapper code is written. If the tag lacks `iceoryx2-cxx/`, the spike switches the pin to the SHA that has it and records it.

**D9 — RTTI deviation (scoped to com).** iceoryx2-cxx derives payload type names via `typeid` (its default type-name mechanism, exercised only at port creation — cold path). The com target therefore opts back into RTTI via `-frtti` on the vendored `iceoryx2-cxx` target's INTERFACE flags, linked after `nexigen_core` so it wins over the inherited global `-fno-rtti` (GCC: last flag wins). Every other nexigen module keeps `-fno-rtti`. Additionally, `IOX2_BB_CXX_CONFIG_USE_STD_EXPECTED=ON` switches the iox2 binding from its own expected to `std::expected` (project mandate), and the vendored dependency is declared `SYSTEM` so its headers do not trip the project's `-Werror -Wshadow`.

## Risks / Trade-offs

- [Cargo-in-CMake vendoring may not compose with the project's strict flags] → the spike is task 1; the com target compiles with its own relaxed warning scope if needed (the module headers are templates over a third-party ABI, not hot-path numerics).
- [Template wrappers pull iox2 headers into every consumer TU] → accepted for the thin-wrapper approach (D1); `NEXIGEN_ENABLE_COM` keeps non-com targets unaffected; if compile time becomes a problem later, pimpl the non-template parts.
- [iceoryx2 API drift between the pin and `main`] → the tag/SHA pin is immutable; upgrading is a later change with its own diff.
- [DiscardSample hides data loss] → the telemetry counters (D7) make eviction observable; the PHM bridge consumes them in the next change.
- [Vendor semantics vs spec wording] → the pinned iceoryx2 offers exactly two backpressure policies; `DiscardData` is the only non-blocking one and drops the NEW sample for an overflowed receiver, whereas the spec text says "discard the oldest unread sample". The observable contract that matters (publisher never blocks, every eviction counted) is preserved; the exact eviction order is a vendor property recorded here.

## Migration Plan

1. Spike (D8) → gate + dependency land.
2. Vocabulary (`Error`, `ServiceName`, `Node`, `Message<T>`) → publish-subscribe + event → method → field → manifest + telemetry, each with tests, compiling behind the gate.
3. Rollback = revert the change; `NEXIGEN_ENABLE_COM` stays OFF by default so no other module is affected either way.

## Open Questions

None blocking. The exact commit SHA for the iceoryx2 pin is resolved inside the spike (D8); the `sm`/`phm` consumers are separate later changes.
