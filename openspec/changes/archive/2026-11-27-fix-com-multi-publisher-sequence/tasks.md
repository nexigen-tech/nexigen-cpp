# Tasks

## 1. Sequence tracker

- [x] 1.1 Add a fixed 16-slot per-`app_id` publisher table and the overflow state to `EventSequenceTracker`; keep the single-publisher path on slot 0 unchanged.
- [x] 1.2 Baseline each new application silently, report `ProducerRestart` only on that application's epoch change, and keep gap/duplicate/reset detection per publisher.
- [x] 1.3 Construct the tracker with `ChannelConfig::max_publishers` from `EventConsumer::create()`.

## 2. Status endpoints

- [x] 2.1 Add `status_channel_config()` with `max_publishers=2` and use it in `nexigen-canrx`, `nexigen-cantx`, `CanClient::subscribe_status()`, `tools/detail/can_tool_common.hpp`, and the `nexigen-canview` status consumer.

## 3. Tests

- [x] 3.1 Add a pure tracker test for interleaved `CANRX`/`CANTX` samples, a per-publisher gap, and a single-publisher restart.
- [x] 3.2 Add a live two-producer `EventChannel<Pod>` test proving no loss on alternation and one `ProducerRestart` after `on_source_reset()`.
- [x] 3.3 Add a canview capture test with two status producers that produces no synthetic markers on alternation and exactly one `TransportRestart` marker after one producer restarts.
- [x] 3.4 Run the existing COM, event-system, HW CAN, canview, and full CTest suites in the Debug tree.

## 4. Documentation and specification

- [x] 4.1 Record the behaviour as the `com/event-system` delta in this change; main specs are updated by `openspec archive`.
- [x] 4.2 Add `/// @brief`, `@param`, and `@return` Doxygen comments to the tracker, `EventConsumer`, and `status_channel_config()` declarations.
- [x] 4.3 Mermaid diagrams in `docs/architecture.md` are not applicable: the transport, service topology, and memory ledger are unchanged; only the consumer's classification of an existing multi-publisher channel changes.

## 5. Verification

- [x] 5.1 Debug build and the full canview/COM test suites pass (143/143 CTest).
- [x] 5.2 Release build (`build-logview-release`) and the canview/COM suites pass.
- [x] 5.3 AddressSanitizer build (`build-asan-com`) and the canview/COM suites pass.
- [x] 5.4 ThreadSanitizer build (`build-tsan-com`) and the canview/COM suites pass.
- [x] 5.5 Run `graphify update .` after the final code edit.

## 6. Workflow follow-up

- [x] 6.1 Run `openspec validate fix-com-multi-publisher-sequence` (valid).
- [ ] 6.2 Archive this change after review.
