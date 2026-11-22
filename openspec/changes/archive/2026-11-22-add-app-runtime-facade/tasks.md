# Tasks

## 1. SIGHUP stop reason

- [x] 1.1 Add `InfrastructureStopReason::Sighup` append-only, extend `EnumTraits`, and verify `to_string`/`from_string`/JSON/`std::format` round-trip `sighup`
- [x] 1.2 Add `SIGHUP` to the blocked mask and `signalfd` dispatch, and verify a `SIGINT`/`SIGTERM`/`SIGHUP` mask probe plus a live `kill -HUP` stop reason
- [x] 1.3 Log `shutdown reason: sighup (1)` and verify the final log record names `sighup`
- [x] 1.4 Add the exit-code helper mapping `Requested`/`Sigint`/`Sigterm`/`Sighup` to 0, `TimeBaseLost` to 75, and `None` to 1; verify each mapping in a unit test
- [x] 1.5 Update `openspec/specs/app/application`-driven tests and `docs/articles/app.md` with the new stop reason; verify the docs list all six reasons

## 2. App runtime core

- [x] 2.1 Move the local example time base into a reusable `app::detail::EmbeddedTimeBase` in the app library and verify the existing app examples still build
- [x] 2.2 Add `TimeBaseMode` (`External`/`Embedded`) with `to_string`/`from_string`, non-throwing JSON, and `std::formatter`; verify each representation round-trips `external` and `embedded`
- [x] 2.3 Add `App<Policy>::create` with external/embedded time-base selection, node ownership, `Infrastructure` initialization, help-as-success-with-null, and the main-thread/one-App documentation; verify create succeeds in both modes and a second App returns `Conflict`
- [x] 2.4 Add the relaxed identity-only bootstrap for a missing settings directory (`--app-name`, `--app-id`, `--log-level`, `--help`) while preserving the strict parser/file loader when a directory is supplied; verify both paths load the same identity and log level
- [x] 2.5 Require `Settings::app_id` and verify a missing `--app-id` returns `InvalidArgument` before `initialize`, naming `--app-id`
- [x] 2.6 Add `counter`/`gauge`/`histogram` one-line accessors over the meter and verify each creates a provider-owned instrument or records a `core::SettingsDiagnostic`
- [x] 2.7 Add the diagnostics gate to `run()` and verify any failed instrument/port prevents the loop and returns `InvalidArgument` naming the declaration
- [x] 2.8 Add the `SIGPIPE` save/ignore/restore guard around the App lifetime and verify a write to a closed pipe returns `EPIPE`, the process survives, and the previous disposition is restored after `shutdown`
- [x] 2.9 Add `App::run()` with `EventConsumerSet<1>` and a 100 ms timeout, `shutdown()` idempotence, destructor fallback, and `to_string`/formatter diagnostics; verify idle stop observation and double-shutdown safety

## 3. Event ports

- [x] 3.1 Add `subscribe<T>(service, callback)` with one input channel/consumer stored behind type erasure; verify the callback receives the payload and event header on the `run()` thread
- [x] 3.2 Add `publish<T>(service)` returning a borrowed `com::EventProducer<T>*`; verify the published header carries the App `app_id` and the configured event code
- [x] 3.3 Reject a second subscription as a diagnostic error in the first slice; verify `run()` refuses and names the second declaration
- [x] 3.4 Verify `SIGHUP`, `SIGTERM`, and `SIGINT` all stop a running App through `run()` and drain telemetry with exit code 0
- [x] 3.5 Add cold-path Doxygen for the new App, TimeBaseMode, diagnostics and port declarations; verify Doxygen builds without warnings for the new headers
- [x] 3.6 Add the App lifecycle (create -> instruments/ports -> run -> shutdown) and the external/embedded time-base split to the Mermaid diagrams in `docs/architecture.md`; verify the diagrams render and name the new stop reasons

## 4. Quickstart example

- [x] 4.1 Add `examples/app/ex_app_quickstart.cpp` using `App<Policy>`, `Embedded`, one input and a doubled output, with a `main` body at most 30 non-comment/non-blank lines; verify the line count
- [x] 4.2 Add the CMake target and CTest smoke test, gated on `NEXIGEN_ENABLE_COM`; verify the standalone run needs only `--app-id` and no chronod/EM
- [x] 4.3 Add a recorder round-trip test/recipe for the quickstart log, metric, trace, input, and output services; verify the session attributes records to the example `app_id`
- [x] 4.4 Document the quickstart invocation in `docs/articles/app.md` and the 20–30 line recipe; verify the documented command runs
- [x] 4.5 Add the `App`/runtime term to `GLOSSARY.md` and `GLOSSARY_RUS.md` with its ownership and stop semantics; verify both entries agree

## 5. Full verification

- [x] 5.1 Build and run the unit tests under Debug and Release COM with `scripts/build.sh build-debug build-release-com` (default `-j 64`) and `ctest --test-dir <dir> --output-on-failure`; verify 100% pass
- [x] 5.2 Build and run the sanitizer trees with `scripts/build.sh build-asan-com build-tsan-com` and `ctest --test-dir <dir> --output-on-failure`; verify no ASan/TSan reports
- [x] 5.3 Build the LTO and latency trees with `scripts/build.sh build-verify-lto build-verify-lat`; verify no regression in the existing latency gates
- [x] 5.4 Run the quickstart end to end with `--app-id`, `nexigen-recd`, and a signal sender; verify telemetry and both event directions are recorded under the same identity
- [x] 5.5 Run `graphify update .` and verify the graph rebuild completes without errors

## Workflow follow-up

- Archive the change after review and a clean full verification run.
