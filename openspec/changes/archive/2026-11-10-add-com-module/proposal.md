# Proposal

## Why

The SDV platform's modules (State Manager, Platform Health Management, the Execution Manager's StateClient interface) are all specified to talk through a service-oriented IPC layer, but the codebase has none: the `exec` module communicates through files (`resource.json`/`exit.json`/`suite.json`), and no publish/subscribe, request/response, or event transport exists. This change introduces `nexigen::com` — the platform's service-oriented communication layer over Eclipse iceoryx2 (zero-copy shared memory, daemon-less discovery) — as the foundation the `sm`, `phm`, and `exec` state interfaces will build on.

## What Changes

- New module `nexigen::com` (`include/nexigen/com/`, `src/com/`, `tests/com/`) — a thin wrapper over iceoryx2 that keeps `iox2::` types out of the business-code surface.
- **BREAKING (new dependency)**: iceoryx2 pinned at **v0.10.0** (crates.io stable), vendored via `FetchContent` behind a new CMake option `NEXIGEN_ENABLE_COM` (default OFF) so every other module still builds without the Rust toolchain.
- The three communication patterns: **Publish-Subscribe** (`Publisher<T>`/`Subscriber<T>`/`Sample<T>`, true zero-copy loan/send), **Event** notification (`EventNotifier`/`EventListener`), **Request-Response** (`Client<Req,Resp>`/`Server<Req,Resp>` with correlation and sync-with-timeout/async), and **Field** (`Field<T>`, Get-Set-Notify with last-value-on-connect).
- The module vocabulary: `com::Error` (own error enum with the project conversion set), `com::ServiceName`, `com::Node`, `com::Result`/`com::Status` (`std::expected` over `com::Error`), and the `com::concepts::Message<T>` contract (`is_trivially_copyable_v<T>`) that makes non-POD payloads a compile error.
- Compile-time type safety: payload size/alignment computed from `T` at compile time, with publisher/subscriber type-layout mismatch surfaced as `com::Error::IncompatibleTypeLayout`.
- Non-blocking delivery (`UnableToDeliverStrategy::DiscardSample`), subscriber lag/overflow telemetry (counters surfaced to PHM later), and a minimal `provided_services`/`required_services` manifest.
- No changes to existing modules (the gate keeps them byte-identical); no behavioral change to `exec`/`hub`/`app`.

## Capabilities

### New Capabilities

- `com/publish-subscribe`: zero-copy Publisher/Subscriber with `Sample<T>`, Event notification, the `Message<T>` trivially-copyable contract, compile-time sizing and type-layout checking, non-blocking overflow, and subscriber-lag telemetry.
- `com/request-response`: the Method pattern — `Client<Req,Resp>`/`Server<Req,Resp>`, correlation ids, synchronous call with timeout and asynchronous pending responses.
- `com/field`: the Field pattern — Get-Set-Notify shared variables with history size ≥ 1 so a late subscriber reads the latest value on connect.
- `com/service-management`: the module foundation — `Node`/`ServiceName` daemon-less discovery, the `com::Error` vocabulary with the full conversion set, the protocol-independent boundary (no `iox2::` in business code), background-thread CPU isolation, and the minimal service manifest.

### Modified Capabilities

None.

## Impact

- **Code**: new `include/nexigen/com/` (≈15 file-per-entity headers), `src/com/` (cold paths: error mapping, manifest serialization), `tests/com/` (zero-dependency harness like the other modules), `examples/com/` (one round-trip demo); `CMakeLists.txt` gains the `NEXIGEN_ENABLE_COM` option, the `nexigen_com` library target, and the iceoryx2 `FetchContent` block.
- **Dependencies**: iceoryx2 v0.10.0 via `FetchContent` — the build chain is cargo (Rust) → `iceoryx2-ffi-c` → `iceoryx2-bb-cxx` → `iceoryx2-cxx`; requires the Rust toolchain only when `NEXIGEN_ENABLE_COM=ON`.
- **APIs**: all new; `nexigen::com::*` is the only surface business code sees (`iox2::` stays inside the module).
- **Performance / latency / allocation impact**: publish path is zero-copy (loan writes in place into shared memory; no serialization, no intermediate copies) and non-blocking `O(1)` even when subscribers overflow (discard policy); subscriber reads are a read-only mapping of the same page. No heap allocation on send/receive hot paths. The module's own infrastructure (manifest parse, telemetry counters) is cold/init-time; lag telemetry uses relaxed `std::atomic` counters updated on the send path (one relaxed store) and read by a background thread. Event notification wakes listeners via iceoryx2's kernel-level event mechanism (no busy polling).
