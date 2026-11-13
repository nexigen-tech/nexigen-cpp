# Tasks

## 1. Scaffolding

- [x] 1.1 Create `examples/integration/suite_2_apps/` and register the two executables: add a `CMakeLists.txt` with `nexigen_add_example(ex_generator ex_generator.cpp nexigen::app nexigen::com)` and `nexigen_add_example(ex_receiver ex_receiver.cpp nexigen::app nexigen::com)`, then add `add_subdirectory(integration)` to `examples/CMakeLists.txt`. Verify: `cmake --build <build-dir> --target ex_generator ex_receiver` produces both binaries.
- [x] 1.2 Create `examples/integration/suite_2_apps/data_sample.hpp` defining the fixed, trivially-copyable `DataSample` (a `std::uint64_t` sequence plus a fixed `char[64]` message) with Doxygen comments, and a `static_assert(std::is_trivially_copyable_v<DataSample>)`. Verify: a TU including the header compiles and the static assertion holds.

## 2. Generator

- [x] 2.1 Implement `ex_generator.cpp`: an `app::Infrastructure` policy whose `emplace_logger`/`emplace_metrics_sink`/`emplace_trace_sink` build `Iceoryx2LoggerConfig`, `Iceoryx2MetricSinkConfig`, and `Iceoryx2TraceSinkConfig` (service names per design D4/D5, app code `GENS`), and `emplace_chrono_sink` builds `chrono::NullSink`. Verify: it compiles and `Infrastructure::initialize` succeeds with a valid settings directory.
- [x] 2.2 Implement the generator loop: create a `com::Node`, open `Publisher<DataSample, tracing::TraceHeader>` on `Nexigen/Demo/DataStream`, and on each one-second tick run `NEXIGEN_TRACE_NEW_ROOT`, start a producer span, publish via `loan_uninit()` → `write_payload` → `user_header_mut() = make_trace_header(seq)` → `send()`, and log each publish. Verify: `ex_generator` runs its bounded smoke mode and exits 0 under CTest.
- [x] 2.3 Record generator metrics (a counter `samples_sent` and a gauge `last_sequence`) and log the per-sample trace id. Verify: a bounded run emits log records and a metric batch containing `samples_sent`.

## 3. Receiver

- [x] 3.1 Implement `ex_receiver.cpp` with the same iceoryx2-sink policy (app code `RECV`), a `com::Node`, and `Subscriber<DataSample, tracing::TraceHeader>` on `Nexigen/Demo/DataStream`. In the `Infrastructure::run()` tick loop, poll `receive()`; on a sample, copy the header's `trace_id`/`source_span_id` into `current_trace_context()` and open a consumer span (design D3). Verify: `ex_receiver` runs its bounded smoke mode and exits 0 under CTest.
- [x] 3.2 Record receiver metrics (a counter `samples_received`) and log each received sample including the inherited `trace_id`. Verify: a bounded run (with a generator publishing) emits logs whose `trace_id` matches the generator's.

## 4. Driver, config generation, and cleanup

- [x] 4.1 Write `run_suite_2_apps.sh`: build the example when needed; generate the demo root under `/tmp/opencode/nexigen-suite-2-apps` (default `NEXIGEN_DEMO_ROOT`) containing `suite.json` (receiver before generator, `apps[].cpus`, `dependencies`, `debug` level, EM `execution_manager_control_thread`/`logger_thread` on cores 2-3), the three daemon configs (absolute `log_directory`/`storage_directory`/`dump_directory`, `bg_cpu_affinity` on cores 4-5, `debug` storage level), and the per-app settings (`worker_thread` on cores 6-7 / 8-9 per design D7); then start the three daemons and `nexigen-exec start --config`. Verify: the script starts the daemons and `nexigen-exec status` reports both apps Running.
- [x] 4.2 Write `clean_suite_2_apps.sh` that stops the daemons and removes the demo root idempotently. Verify: running it twice exits 0 both times with the demo root absent.

## 5. Documentation and end-to-end verification

- [x] 5.1 Update the README Runnable Examples section to name `ex_generator` and `ex_receiver`, document `run_suite_2_apps.sh`/`clean_suite_2_apps.sh` and the directories they manage, and bump the catalogue count to nineteen. Verify: the documented commands match the shipped scripts and files.
- [x] 5.2 Verify end-to-end trace propagation: after a full scripted run, the log daemon's JSONL output contains the generator's publish record and the receiver's receive record with the same `trace_id`. Verify: grep the log output for a `trace_id` present in both a publish and a receive record.
- [x] 5.3 Verify daemon ingestion: the log daemon's JSONL files contain both applications' records, and the metrics daemon's Prometheus `/metrics` endpoint (and JSONL history) shows `samples_sent` and `samples_received`. Verify: inspect the generated log directory and `curl` the metrics endpoint.

## 6. Full build-mode matrix

- [x] 6.1 Configure and build Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) trees and run `ctest` in each, confirming the `ex_generator` and `ex_receiver` example tests pass with no sanitizer report. Verify: all four matrices report the example tests green.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Rebuild the knowledge graph (`graphify update .`) after the example lands.
