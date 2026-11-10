# Tasks

## 1. Dependency spike and build gate

- [x] 1.1 Spike: add the `NEXIGEN_ENABLE_COM` CMake option (default OFF) and a `FetchContent` block for iceoryx2 pinned at tag `v0.10.0` (SHA-verified); build with `-DNEXIGEN_ENABLE_COM=ON` on a machine with rust 1.89+ and confirm the cargo → `iceoryx2-ffi-c` → `iceoryx2-bb-cxx` → `iceoryx2-cxx` chain produces linkable libraries. If the tag lacks `iceoryx2-cxx/`, pin the commit SHA that has it and record it in the CMake comment + design D4. Verify: a trivial out-of-tree two-process round-trip using `iox2::` directly compiles and runs.
- [x] 1.2 Add the `nexigen_com` library target (guarded by the option), linked only when ON; confirm `NEXIGEN_ENABLE_COM=OFF` builds the whole existing project with zero Rust references. Verify: full Debug build with the gate OFF is byte-identical in behavior to before this change.

## 2. Module vocabulary

- [x] 2.1 Create `include/nexigen/com/error.hpp`: `com::Error` enum (per design D2), `EnumTraits` table, constexpr `to_string`/`from_string`, ADL `to_json`/`from_json`, `com::Result`/`com::Status` aliases. Verify: unit cases round-trip every code, garbage-cast renders `Unknown`, hostile JSON leaves the target unchanged, formatter equals `to_string`.
- [x] 2.2 Create `src/com/error_mapping.cpp`: map the `iox2::*_error` enums (publisher/subscriber/server/client/listener/notifier/port_factory/service_builder) to `com::Error`; unmapped variants collapse to `Unknown`. Verify: mapping unit cases for the exercised paths.
- [x] 2.3 Create `include/nexigen/com/service_name.hpp` and `include/nexigen/com/node.hpp`: `ServiceName` (value type, valid-name checks, `to_string`) and `Node` (owns one process's membership; `create()`; `to_string` diagnostics). Verify: value-semantics and diagnostics cases.

## 3. Publish-Subscribe + Event

- [x] 3.1 Create `include/nexigen/com/concepts/message_concept.hpp`: `concepts::Message<T>` = `is_trivially_copyable_v<T>`. Verify: positive static_assert for a POD struct; negative-compile probe TU (non-POD with `std::string`/virtual fails).
- [x] 3.2 Create `include/nexigen/com/publisher.hpp`, `subscriber.hpp`, `sample.hpp`, `sample_mut.hpp`: `Publisher<T>`/`Subscriber<T>`/`Sample<T>`/`SampleMut<T>` wrapping `iox2::` with `loan_uninit()`/`send()`/`receive()` per design D5; cold `to_string` + formatter. Verify: two-process loan-send-receive round-trip in `tests/com/`; dropped loan returns the slot; type-layout mismatch reports `IncompatibleTypeLayout`.
- [x] 3.3 Create `include/nexigen/com/event_id.hpp`, `event.hpp`: `EventId`, `EventNotifier`, `EventListener`. Verify: listener blocked in `wait()` wakes on publish, no polling.

## 4. Request-Response and Field

- [x] 4.1 Create `include/nexigen/com/client.hpp`, `server.hpp`, `pending_response.hpp`: `Client<Req,Resp>`/`Server<Req,Resp>` with `correlation_id`, synchronous `call()` with timeout (`Error::Timeout`), and asynchronous `PendingResponse` per the spec. Verify: round-trip, out-of-order correlation, timeout, and pending-response cases.
- [x] 4.2 Create `include/nexigen/com/field.hpp`: `Field<T>` (single-writer get/set/notify, `history_size >= 1`). Verify: set-then-get, update-notifies-listener, late-connect-reads-latest cases.

## 5. Manifest and telemetry

- [x] 5.1 Create `include/nexigen/com/service_manifest.hpp`: `ServiceManifest` (`provided_services`/`required_services`) with member+free `to_string`, `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, `std::formatter`. Verify: round-trip, formatter equality, hostile-input cases.
- [x] 5.2 Add the per-publisher telemetry cell per design D7 (`alignas(64)` relaxed atomics: `queued_samples_`, `lost_samples_`, `send_count_`) with `UnableToDeliverStrategy::DiscardSample`; expose the counters through the port diagnostics. Verify: fill/loss counters tick on a slow subscriber; send path stays non-blocking and allocation-free.

## 6. Full gate

- [x] 6.1 Debug build (gate ON and OFF): configure + ninja, 100% of unit tests pass (`ctest`), including the new `nexigen_com_tests`.
- [x] 6.2 Release build `-O3 -march=native -flto` (gate ON): 100% tests pass with existing latency gates unchanged; com send/receive paths show no heap allocation under the allocation-arena probe.
- [x] 6.3 ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`, gate ON): publish/subscribe, event, method, and field tests race-free.
- [x] 6.4 AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`, gate ON): no leaks/races in port creation/teardown and pool reuse.
- [x] 6.5 `openspec validate "add-com-module" --strict` passes; the change directory carries all four deltas (`com/publish-subscribe`, `com/request-response`, `com/field`, `com/service-management`).

## Workflow follow-up

- Archive after review; the archive creates the four `com/*` capabilities in the main specs.
- Follow-on changes (not here): `nexigen::sm`, `nexigen::phm`, and the `exec` FunctionGroup/StateClient reconciliation all consume `nexigen::com`.
