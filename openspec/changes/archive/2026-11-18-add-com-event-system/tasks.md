# Tasks

## 1. COM service options

- [x] 1.1 Add `PublishSubscribeOptions` and `EventOptions` headers with `to_string`, formatter, and non-throwing JSON; verify round-trip and hostile-JSON unit tests pass.
- [x] 1.2 Add `Publisher`/`Subscriber` option overloads and apply `max_subscribers`, `max_publishers`, `max_nodes`, `subscriber_max_buffer_size`, `history_size`, and `enable_safe_overflow`; verify default-behaviour, safe-overflow/history-configuration, limit, and incompatible-options tests pass.
- [x] 1.3 Add `EventNotifier`/`EventListener` option overloads and apply `max_listeners`, `max_notifiers`, and `max_nodes`; verify listener/notifier limit and default-behaviour tests pass.
- [x] 1.4 Document the changed `com` headers with Doxygen (`@brief`, `@param`, `@return`, `@tparam`); verify `doxygen Doxyfile` reports no warnings for the changed files.

## 2. Event-system vocabulary

- [x] 2.1 Add the public `EventPayload` concept as a named `com::concepts::Message<T>` contract and the `EmptyPayload` type; verify positive and negative compile tests cover POD, `EmptyPayload`, `std::string`, containers, and references.
- [x] 2.2 Add `EventHeader` with the 64-byte layout, flags, `TimestampDomain`, and `static_assert` size/alignment/trivial-copy checks; verify a layout test fails on field drift and covers all presence flags.
- [x] 2.3 Add `ChannelConfig`, `EventTime`, and `HardwareMetadata` with validation for the 16-consumer limit, queue/history bounds, and optional hardware fields; verify defaults, invalid limits, and hardware round-trip tests pass.
- [x] 2.4 Add `LossReason`, `LossInfo`, `PublishOutcome`, `WaitOutcome`, `EventProducerStats`, and `EventConsumerStats`; verify enum conversion/JSON/formatter and value-struct serialization tests pass under `JSON_NOEXCEPTION=1`.
- [x] 2.5 Register the new headers/TUs in the COM-gated build and document them with Doxygen; verify a clean `NEXIGEN_ENABLE_COM=ON` configure/build succeeds and the warning check is clean.

## 3. Event producer

- [x] 3.1 Implement `EventProducer<T>::create` over `Publisher<T, EventHeader>` and `EventNotifier`, including `publish_now` and explicit-time `publish`; verify a published event carries the expected payload, transport sequence, event code, and host ingest timestamp.
- [x] 3.2 Implement `PublishOutcome`, `notify()` retry, and producer statistics; verify `Notified` and `NotifyFailed` paths are distinguishable and a failed send does not consume a sequence.
- [x] 3.3 Implement `producer_epoch`, `on_source_reset()`, and the single-publisher service limit; verify reset changes the epoch, restarts sequence at zero, and a second publisher is rejected.
- [x] 3.4 Preserve `hardware_sequence`, `hardware_timestamp_ns`, and `hardware_event_code` with presence flags, and support `EmptyPayload`; verify hardware metadata and `has_payload = false` reach the consumer unchanged.
- [x] 3.5 Document `EventProducer` and its value types with Doxygen; verify the header has no `@param`/`@return` warnings.

## 4. Event consumer

- [x] 4.1 Implement `IEventConsumer` and `EventConsumer<T, OnEvent, OnLoss>` over `Subscriber<T, EventHeader>` and `EventListener`, including `drain()` and blocking `wait()`; verify a single-consumer receive and full-queue drain.
- [x] 4.2 Implement transport sequence tracking with A1 baseline/history, B1 duplicate-drop, C2 unexpected-reset, and epoch-restart behaviour; verify each branch with focused unit tests and exact lost counts.
- [x] 4.3 Implement `LossInfo` callbacks and `EventConsumerStats`; verify first-sequence/history-skipped, transport-lost, duplicate, reset, and restart counters.
- [x] 4.4 Enforce standalone-vs-set attachment state so `wait()` and `EventConsumerSet::attach` are mutually exclusive; verify the rejected mode returns `InvalidState` and no event is lost.
- [x] 4.5 Document `EventConsumer` and the loss model with Doxygen; verify the header has no documentation warnings.

## 5. Consumer set and wake-up

- [x] 5.1 Implement `EventConsumerSet<MaxConsumers>` with fixed slot storage, the 16-consumer `static_assert`, constructor `Status`/`operator bool`, and capacity reporting; verify construction, invalid state, and 17th-attach `OutOfSamples`.
- [x] 5.2 Implement one `WaitSet`, the private wake pair, `wait`, `wait_for`, and `wake_up`; verify `Events`, `Timeout`, and `WokenUp`, including event+wake priority and no busy-spin while idle.
- [x] 5.3 Implement same-thread `attach`/`detach`, listener attachment, and the `WaitSet::capacity() >= MaxConsumers + 1` check; verify multiplexed consumers, detach lifecycle, and clean capacity failure.
- [x] 5.4 Document `EventConsumerSet` and `WaitOutcome` with Doxygen; verify the header has no documentation warnings.

## 6. Application-facing channel

- [x] 6.1 Implement `EventChannel<T>` as a factory with default and explicit `ChannelConfig`, validating configuration without creating a service in the constructor; verify default 16/512/16 and invalid-config paths.
- [x] 6.2 Implement `producer()` and `consumer(callbacks)` so only the requested endpoint is created and both endpoints use the same service options; verify no unused publisher/subscriber port, template callbacks, and `std::function` callables.
- [x] 6.3 Document `EventChannel` with Doxygen and add a usage example that compiles; verify the example runs one publish and one consumer callback.

## 7. Hardware gateway ingress

- [x] 7.1 Add the trivially-copyable `HardwareEvent<T>` ring element and a gateway test fixture using `core::SpscRingBuffer`; verify ISR-style push is allocation-free and the gateway publishes the descriptor payload.
- [x] 7.2 Verify hardware timestamp/sequence/event-code preservation, non-monotonic hardware sequence remaining trace-only, and `on_source_reset()` after ring quiescence; verify source-gap and transport-gap counters stay separate.
- [x] 7.3 Document the gateway contract and new hardware types with Doxygen; verify the headers have no documentation warnings.

## 8. Documentation and integration verification

- [x] 8.1 Add `docs/com-event-system.md` with the producer/consumer/gateway usage examples and update the Mermaid module/interaction diagrams in `docs/architecture.md`; verify the documented commands/snippets compile and `graphify update .` succeeds.
- [x] 8.2 Run `doxygen Doxyfile` and verify zero warnings for the new and changed `include/nexigen/com/` headers.
- [x] 8.3 Build and run the COM-enabled suites with `-j 64` in Debug, Release, ASan, and TSan (`build-com`, `build-release-com`, `build-asan-com`, `build-tsan-com`); verify 100% of `nexigen_com_tests` pass in every tree.
- [x] 8.4 Run `openspec validate add-com-event-system --strict` and verify the change is valid.

## Workflow follow-up

- Archive the change after review and verification pass.
- Verify the archived specs and the synced `com/event-system` capability.
