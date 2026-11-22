# Tasks

## 1. Core application identifier

- [x] 1.1 Add `include/nexigen/core/app_id.hpp` with the 16-byte `AppId` value, fallible construction, `size()`/`text()`, equality, and a `to_string()` member plus free function; verify `sizeof(AppId) == 16`, `std::is_trivially_copyable_v<AppId>`, and unit tests cover 0, 1, 16, and 17 bytes plus embedded NUL and invalid characters
- [x] 1.2 Add `std::formatter`, non-throwing `to_json`/`from_json`, and diagnostics; verify JSON round-trip, invalid/mistyped JSON leaves the destination unchanged, and the formatter output equals `to_string()`
- [x] 1.3 Document the identifier (format, hard limit, suite scope, attribution-not-authentication) in `GLOSSARY.md` and `docs/adr/0004-app-id-identity.md`; verify both files name `CN_LOOP_1` as the example and link the change
- [x] 1.4 Add Doxygen `/// @brief`, `@param`, `@return`, and `@note` to `app_id.hpp`; verify Doxygen builds without warnings for the new header

## 2. Manifest and suite-scoped uniqueness

- [x] 2.1 Add `AppSpec::app_id` to the manifest parser and serializer; verify `to_json`/`from_json` round-trips the raw text and a manifest without the field parses unchanged
- [x] 2.2 Extend manifest validation: absent `app_id` falls back to `app`, 17-byte or forbidden values fail with `InvalidArgument`, and duplicate resolved ids fail with `InvalidArgument`; verify unit tests cover each case and that nothing is created on failure
- [x] 2.3 Add a test proving the scope is the suite: two manifests that each declare `CN_LOOP_1` both validate and start, and no lock file, registry, or admission-time cross-suite check is created
- [x] 2.4 Update the suite-manifest reference in `docs/articles/exec.md` and one example manifest with `app_id`; verify the documented example loads with `nexigen-exec start --config` and the runtime example suite starts
- [x] 2.5 Add the identity-resolution flow to the Mermaid diagrams in `docs/architecture.md`; verify the diagram renders and names manifest → validation → argv → sink frames
- [x] 2.6 Add Doxygen to `app_spec.hpp` and `suite_manifest.hpp`; verify the new field, the default rule, and the duplicate failure are documented

## 3. Runtime plumbing

- [x] 3.1 Add `-ai/--app-id` to `CommandLineOptions`, parsing, usage text, `apply_to(Settings)`, and JSON; verify tests for `-ai X`, `-aiX`, `--app-id=X`, invalid/oversized values, and an omitted option leaving the id unset
- [x] 3.2 Add `Settings::app_id` with `to_string`, formatter, and JSON; verify an unset value renders empty and a set value reaches the diagnostic output
- [x] 3.3 Replace the `app_name` hook parameter with `app::TelemetryIdentity` in `InfrastructurePolicy`, `Infrastructure`, and the four built-in policies; make `Iceoryx2Policy` prefer the runtime id and fall back to `A..D`; verify policy tests cover runtime override, fallback, and 16-byte ids, and a negative-compile test rejects a tag outside the permitted charset
- [x] 3.4 Append `--app-id <id>` in `ExecutionManager::build_argv` next to `--app-name` and pass the resolved id through `SuiteLaunchPlan`; verify the child argv contains both flags and no `NEXIGEN_APP_ID` variable is set
- [x] 3.5 Update `docs/articles/app.md` with the new option, the settings field, the identity precedence, and a migration note for custom policies; verify the documented commands and snippet compile
- [x] 3.6 Add Doxygen to `command_line_options.hpp`, `app_settings.hpp`, `infrastructure.hpp`, and `iceoryx2_policy.hpp`; verify every new field and parameter is documented

## 4. Logging wire

- [x] 4.1 Widen `LogFrame::app_id` to `char[16]`, add `format_version = 2`, keep the frame at 256 bytes, and set the payload to 211 bytes; verify the `static_assert`s and unit tests for offsets, full-width ids, clipping, and version rejection
- [x] 4.2 Change `Iceoryx2LoggerConfig::app_id` to `core::AppId`, validate it before port creation, and update the sink copy; verify config JSON round-trip and rejection of empty/oversized/malformed ids
- [x] 4.3 Update `nexigen-logd` rendering and the recorder's log schema (`app_id` `char[16]`, payload offset 45); verify a `CN_LOOP_1` frame produces JSON with `"app":"CN_LOOP_1"` and a recorder round-trip decodes it
- [x] 4.4 Update `docs/articles/logging.md` with the new layout and payload capacity; verify the table matches the `static_assert` offsets
- [x] 4.5 Add Doxygen to `log_frame.hpp` and the logger config; verify the layout table and version contract are documented

## 5. Metrics wire

- [x] 5.1 Widen `MetricFrame::app_id` to `char[16]` and keep the record naturally aligned at offset 32; verify `static_assert`s, JSON round-trip, and a full-width id test
- [x] 5.2 Change `Iceoryx2MetricSinkConfig::app_id` to `core::AppId` and reject empty/oversized/malformed ids before creating a port; verify config JSON round-trip and validation tests
- [x] 5.3 Update `nexigen-metricsd` and the Prometheus `app` label, plus the recorder metric schema; verify a 16-byte id appears verbatim in `/metrics` and in an exported session
- [x] 5.4 Update `docs/articles/metrics.md` and the frame diagram; verify the documented offsets match the header
- [x] 5.5 Add Doxygen to `metric_frame.hpp` and the metric sink config; verify the widened field and alignment are documented

## 6. Tracing wire

- [x] 6.1 Widen `SpanFrame::app_id` to `char[16]` in the reserved tail, keep the frame at 384 bytes, shrink `reserved` to 30 bytes, and set `format_version = 2`; verify the offset `static_assert`s and tests for version rejection and a 16-byte id
- [x] 6.2 Change `Iceoryx2TraceSinkConfig::app_id` to `core::AppId` and reject empty/oversized/malformed ids; verify config JSON round-trip and validation tests
- [x] 6.3 Update `nexigen-traced` and the recorder span schema for the shifted offsets; verify a dump contains `CN_LOOP_1` and an exported session round-trips the frame
- [x] 6.4 Update `docs/articles/tracing.md` with the layout and version bump; verify the documented offsets match the header
- [x] 6.5 Add Doxygen to `span_frame.hpp` and the trace sink config; verify the layout table and version contract are documented

## 7. Events, transport header, recorder

- [x] 7.1 Widen `EventHeader::app_id` to `char[16]` and pin the header at 80 bytes; verify `static_assert(sizeof(EventHeader) == 80)` and event tests with a 16-byte id
- [x] 7.2 Widen `TransportHeader::app_id` to `char[16]` and pin the header at 40 bytes; verify sequence/loss tests still pass and JSON round-trips the 16-byte id
- [x] 7.3 Update the recorder `EventEnvelope` schema (`app_id` at offset 64, count 16) and channel naming; verify `event.<id>` channels and an export round-trip with a full-width id
- [x] 7.4 Verify `nexigen-recd`, `nexigen-logd`, `nexigen-metricsd`, and `nexigen-traced` agree on the same id for a suite with two applications sharing one executable; verify with an end-to-end recorder session
- [x] 7.5 Update `docs/articles/com.md` (or the event-system section) and `docs/articles/recorder.md` with the new header sizes and schemas; verify the tables match the headers
- [x] 7.6 Add Doxygen to `event_header.hpp`, `transport_header.hpp`, and the recorder schema declarations; verify all widened fields are documented

## 8. Full verification

- [x] 8.1 Build and run the unit tests under Debug and Release with `scripts/build.sh build-debug build-release-com` (default `-j 64`) and `ctest --test-dir <dir> --output-on-failure`; verify 100% pass
- [x] 8.2 Build and run the sanitizer trees with `scripts/build.sh build-asan-com build-tsan-com` and `ctest --test-dir <dir> --output-on-failure`; verify no ASan/TSan reports
- [x] 8.3 Build the LTO and latency trees with `scripts/build.sh build-verify-lto build-verify-lat`; verify the `SpanFrame` 400 ns median gate and all Release latency gates pass
- [x] 8.4 Run the suite/example end-to-end scenario (two manifest entries, one executable, ids `CN_LOOP_1` and `CN_LOOP_2`) and verify the daemons, recorder, and hub attribute each stream to the right id
- [x] 8.5 Grep the tree for remaining `app_id[4]`, `char[4]`, and four-byte validation text; verify no stale producer, consumer, schema, or doc remains
- [x] 8.6 Run `graphify update .` and verify the graph rebuild completes without errors

## Workflow follow-up

- Archive the change after review and a clean full verification run.
- Verify the archived delta specs and the ADR links.
