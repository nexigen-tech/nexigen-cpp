# Tasks

## 1. Shared capacity contract

- [x] 1.1 Add `include/nexigen/logging/log_service_capacity.hpp` with the six COM-free constants
      (`kLogServiceMaxSubscribers=4`, `kLogServiceMaxPublishers=64`, `kLogServiceMaxNodes=64`,
      `kLogServiceBufferDepth=16384`, `kLogServiceHistoryDepth=4`,
      `kLogServiceSafeOverflow=true`) and Doxygen comments; verify a new
      `tests/logging/log_service_capacity_test.cpp` asserts every value and that the header compiles
      with `NEXIGEN_ENABLE_COM=OFF`.
- [x] 1.2 Add `include/nexigen/logging/log_service_options.hpp` with
      `log_service_options()` returning `com::PublishSubscribeOptions`; verify a unit test compares
      every returned field with the capacity constants and that the COM-enabled logging target
      compiles.

## 2. Logger publisher path

- [x] 2.1 Change `detail::Iceoryx2Sink::start()` to create its publisher with
      `com::Publisher::create(node, name, log_service_options())`; verify
      `tests/logging/iceoryx2_logger_test.cpp` still passes and a new test reads the created
      service's static config (4/64/64/16384/4/safe overflow).
- [x] 2.2 Change `Iceoryx2LoggerConfig` defaults to `max_subscribers=4` and `history_size=4` and
      validate both against the capacity constants before the worker starts; verify the JSON
      round-trip test and a rejection test for `max_subscribers=16` (returns `InvalidArgument`, no
      worker started).
- [x] 2.3 Map an incompatible existing service at publisher creation to a startup diagnostic that
      names the required and existing capacities; verify a test that creates a service with a 2-sample
      buffer, then starts the logger and observes the diagnostic instead of a generic
      `ConnectionFailure`.
- [x] 2.4 Add `/// @brief`/`@param`/`@return` Doxygen to every touched public header and verify
      `doxygen` (or the project's header-doc check) reports no new warnings.

## 3. Other participants

- [x] 3.1 Open the daemon's subscriber with `log_service_options()` in
      `tools/nexigen-logd/main.cpp`; verify `tests/logging/log_daemon_test.cpp` observes the 16 384
      depth and that a publisher started after the daemon connects successfully.
- [x] 3.2 Open the recorder's log subscriber with `log_service_options()` in
      `tools/nexigen-recd/main.cpp`; verify the recorder build and an integration run with `--logs`
      receive frames while the daemon holds the service.
- [x] 3.3 Document in the logging article that the viewer (next change) must use the same options and
      that the first participant fixes the service shape.

## 4. Regression coverage

- [x] 4.1 Add a real-transport test with three publishers on `Nexigen/Log/nexigen`; verify all three
      create successfully and a single subscriber receives frames from all three.
- [x] 4.2 Add a burst test that publishes 16 384 frames before the subscriber drains; verify zero
      `ReceiveBufferFull` failures, every frame delivered, and no `lost_before` gap.
- [x] 4.3 Add an overflow test where the subscriber stops draining and the publisher sends more than
      16 384 frames; verify the publisher keeps sending, the oldest frames are evicted, and the next
      received frame reports the gap through `lost_before`.
- [x] 4.4 Add a config-compatibility test for a live service created with the old options; verify the
      diagnostic names both shapes and startup fails before any worker thread runs.

## 5. Integration and documentation

- [x] 5.1 Add the option table and memory formula to `docs/architecture.md` (and the logging article)
      and verify the Mermaid/text renders the same values as the constants.
- [x] 5.2 Run Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer
      (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) builds with
      `scripts/build.sh … -j 64`; verify 100% of logging and tool tests pass in all four.
- [x] 5.3 Run `graphify update .` after the code changes and verify the graph rebuild completes
      without errors.

## Workflow follow-up

- Archive the change after review and after the viewer change starts using `log_service_options()`.
