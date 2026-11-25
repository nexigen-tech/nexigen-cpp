# Tasks

## 1. Build scaffolding and shared GUI dependencies

- [x] 1.1 Create `tools/nexigen-canview/` with `backend/`, `model/`, `canopen/`, `ui/`, `tests/`, `CMakeLists.txt`, and `namespace nexigen::canview`; verify `cmake -S . -B build -DNEXIGEN_BUILD_CANVIEW=ON -DNEXIGEN_ENABLE_COM=ON` configures and the empty targets build
- [x] 1.2 Hoist ImGui and GLFW FetchContent blocks from `tools/nexigen-logview/CMakeLists.txt` into top-level `nexigen_imgui`/`nexigen_glfw` targets and link `nexigen-logview` to them; verify `NEXIGEN_LOGVIEW_GUI=ON` builds and `ctest -L logview` is unchanged
- [x] 1.3 Add a pinned `nexigen_implot` target compatible with the pinned ImGui docking revision; verify a minimal heatmap sample compiles and the ImPlot source pin is recorded in CMake
- [x] 1.4 Add `NEXIGEN_BUILD_CANVIEW` (default OFF), `NEXIGEN_CANVIEW_GUI` (default ON), Linux-only and COM-required guards, install rules, and test labels; verify disabling the option leaves the tree unchanged and enabling it exposes the targets
- [x] 1.5 Add `nexigen_canview_model`, `nexigen_canview_com`, `nexigen-canview`, and `nexigen_canview_tests` targets with the documented link boundaries; verify headless targets do not link OpenGL/GLFW and the GUI target does

## 2. Record model and bounded queues

- [x] 2.1 Implement `CanViewChannel`, `ViewKind`, `TimeColumnMode`, `TimestampMode`, `DeltaMode`, and `OverwriteKey` with `to_string`, `std::formatter`, and non-throwing JSON; verify round-trip and formatting unit tests
- [x] 2.2 Implement `CanRecordKind`, `GapReason`, record-meta packing, and record flags; verify every enumerator formats, serializes, and round-trips without exceptions
- [x] 2.3 Implement `CanRecordRing` with the 126 B/slot parallel-array layout, `alignas(64)` arrays, mmap allocation, absolute `seq`, push, find, eviction, and clear; verify layout `static_assert`s, capacity tests, and eviction tests
- [x] 2.4 Implement `CanIngressRecord` (128 B, `alignas(64)`), `StatusEvent`, `AnchorEvent`, and the three SPSC queues; verify size/alignment `static_assert`s and single-producer/single-consumer ordering tests
- [x] 2.5 Implement sequence bookkeeping: forward gap -> `lost_before`, equal/backward -> duplicate/reorder, controller-restart/bus-off -> reset, and oversized-jump -> `SequenceReset`; verify tests for every branch
- [x] 2.6 Implement viewer-local `validate_for_view()` for kind, DLC, FD legal lengths, identifier range, and timestamp/sequence presence; verify remote DLC 0..8, FD 9/13/63 rejection, and payload bound tests
- [x] 2.7 Implement the budget ledger and `capacity_for_budget`: 164 B/slot worst case, 1 048 576 at 256 MiB, 2 097 152 at 384 MiB, and the 16 MiB degraded plan; verify unit tests and an allocation-failure injection test

## 3. Capture thread

- [x] 3.1 Implement `CaptureThread` with one `com::Node`, one `EventConsumerSet<16>`, eight `rx` and eight `status` consumers, queue producers, and thread naming/priority; verify a virtual-bus test with two channels receives frames and statuses
- [x] 3.2 Enforce the 8 live-channel limit, channel-name/id binding checks, and profile validation before service creation; verify a ninth channel is rejected and a mismatched `channel_id` is dropped and counted
- [x] 3.3 Implement loss handling from hardware sequences, `com::LossInfo`, ingress overflow, status `dropped_count`, and status `sequence_gap_count` fallback; verify one gap record per loss class and no double counting
- [x] 3.4 Implement the per-channel UTC anchor estimator from `EventHeader.ingest_timestamp_ns` and `TimeBaseClock`, anchor epochs, and the anchor queue; verify UTC offset/drift tests with a simulated clock and no-timebase tests
- [x] 3.5 Implement `Start`/`Stop`/`Restart`, per-channel failure isolation, continue-with-available-channels, and the none-started state; verify tests for one failed channel, all failed, and restart recovery
- [x] 3.6 Verify capture stop on SIGINT/SIGTERM/SIGHUP and window-close flag with a bounded wait timeout; verify a transport test exits cleanly and leaves the history readable
- [x] 3.7 Verify the capture callback performs no allocation, formatting, validation, or blocking by code inspection and a hot-path allocation guard test

## 4. Views, filtering, find, and cloning

- [x] 4.1 Implement `FilterCriteria` for channels, ID/mask, node bindings, kinds, origins, errors-only, BusIdle, gaps, and text/hex; verify each criterion and combination with model tests
- [x] 4.2 Implement the circular `uint32_t` visible index with append, eviction, compaction, and 20 000-record rebuild chunks; verify ordering, progress, and eviction tests
- [x] 4.3 Implement `CanView` state for Trace/Overwrite, selection, bookmarks, find, scroll, auto-scroll, pause, T0, time settings, and export selection; verify per-view isolation tests
- [x] 4.4 Implement `All`/`Channel`/`Node` view creation, 8 LIVE/16 PAUSED limits, scope rules, orphaned node views, and recovery; verify limit, scope, and orphan tests
- [x] 4.5 Implement LIVE and PAUSED clone with independent copy flags, 1 MiB snapshot clipping, and PAUSED isolation; verify selection/bookmark/T0 clipping and snapshot immutability tests
- [x] 4.6 Implement focus routing and left-panel shortcuts as focused-view filter edits; verify changing a checkbox does not alter subscriptions and only affects the focused view
- [x] 4.7 Implement view persistence for LIVE views only, including kind, channel/node identity, filter, table mode, time settings, and title; verify round-trip, missing binding, and over-limit load tests

## 5. Time presentation and Overwrite

- [x] 5.1 Implement the UI anchor table, epoch lookup, UTC presentation, tooltip data, and explicit unavailable state; verify column modes, anchor jump, epoch eviction, and no-timebase tests
- [x] 5.2 Implement `Driver`/`Since session start`/`Relative T0`, HW delta modes, signed arithmetic, cross-channel unavailability, and the shared-timebase switch; verify per-ID outlier, cross-channel, T0, and shared-mode tests
- [x] 5.3 Implement the keyed Overwrite table with open addressing, LRU eviction, lifetime count, 1 s rate buckets, 32-period statistics, and peak-to-peak jitter; verify key grouping, count, rate, stats, and eviction tests
- [x] 5.4 Implement byte-change fade with UI monotonic time, DLC shrink handling, and key-based selection/export; verify fade, DLC shrink, selection persistence, Ctrl+A, and latest-record export tests
- [x] 5.5 Implement gap/reset handling for Overwrite statistics, error/overload row rules, and `Next gap` semantics; verify a channel gap resets only that channel's rows and an ingress overflow resets all rows

## 6. CANopen EDS, bindings, and routing

- [x] 6.1 Implement the CiA 306 EDS/DCF parser with sections, comments, `$NODEID` expressions, `SubNumber`, `CompactSubObj`, size/line/object limits, and line diagnostics; verify valid and malformed fixtures
- [x] 6.2 Implement immutable `EdsDictionary` object lookup, data types, access, default values, PDO mapping defaults, COB-ID entries, and hash caching; verify lookup, type, and cache-reload tests
- [x] 6.3 Implement the 128-slot `CanOpenRegistry` with `(channel_id, node_id)` uniqueness, aliases, colours, NMT state, heartbeat timeout, and binding limits; verify bind, replace, duplicate, and limit tests
- [x] 6.4 Implement per-channel 2 048-entry COB-ID routing for defaults and custom COB-IDs, 11-bit-only rules, conflict handling, and disabled COB-IDs; verify default, custom, conflict, and extended-frame tests
- [x] 6.5 Implement binding add/remove reindex of `co_node_index` in 20 000-record chunks with progress; verify old frames become associated after binding and are cleared after removal

## 7. CANopen decoding and dynamic remapping

- [x] 7.1 Implement NMT command, boot-up, heartbeat, state transitions, and stale detection; verify command broadcast, heartbeat, boot-up, and timeout tests
- [x] 7.2 Implement EMCY decoding with error code, register, manufacturer data, and common-code names; verify EMCY and SYNC-not-EMCY tests
- [x] 7.3 Implement expedited and segmented SDO request/response decoding, 4 KiB assembly, timeouts, abort codes, and oversized handling; verify each transfer shape and isolation between nodes
- [x] 7.4 Implement PDO mapping lookup and bit-exact unpacking for signed/unsigned/REAL/string/unknown objects and length mismatch; verify byte-aligned, bit-packed, unknown, and partial tests
- [x] 7.5 Implement passive dynamic remapping: apply mapping/COB-ID changes only after a successful SDO response, allocate `co_map_epoch`, retain 8–16 snapshots, and mark evicted epochs stale; verify write success, abort, epoch history, and COB-ID future-only tests
- [x] 7.6 Implement node colour/criticality modes and Inspector CANopen presentation; verify node colour, Criticality override, and decoded-detail tests
- [x] 7.7 Verify offline CANopen fallback uses exported `co_decoded` when EDS is absent and re-decodes when the hash matches; verify missing/matching/mismatched-hash fixtures

## 8. Heatmap and bit activity

- [x] 8.1 Implement the bounded `HeatmapWindow` cache with at most 2 048 columns and per-channel/per-ID-bin aggregates; verify column cap, aggregation, and rebuild tests
- [x] 8.2 Implement measured BusIdle load distribution, frame bit-time estimation for classic/FD/BRS, error/overload markers, and missing-bitrate `n/a`; verify measured, estimated, and missing-bitrate tests
- [x] 8.3 Implement `UTC (est.)`/`HW lanes`/`Shared HW` axes, ID-bin mode for one selected channel, and the follow/step controls; verify axis switching and shared-timebase cases
- [x] 8.4 Implement click-to-record mapping through a sparse time index, focused-view pause, and Trace scroll; verify click on a bucket selects the expected record
- [x] 8.5 Implement bounded bit activity over 4 096 frames or 10 s, 64x8/8x8 matrices, transition counts, frequencies, and sparse-window handling; verify classic, FD, and sparse tests

## 9. Session JSONL, CSV/TXT, and offline replay

- [x] 9.1 Implement `CanViewSessionHeader`, typed frame/gap/status records, canonical field names, hex payload, optional UTC fields, and JSONL v1 serialization; verify header and record round-trip tests
- [x] 9.2 Implement JSONL import for v1 and legacy `nexigen-canmon --json`, local sequence renumbering, synthetic channel descriptors, malformed-line counting, line limits, and partial last line; verify fixtures for each case
- [x] 9.3 Implement CSV and TXT exporters with correct quoting, header row, fixed columns, and missing-value placeholders; verify comma/quote/newline and placeholder tests
- [x] 9.4 Implement the 4 096-slot async export writer with one active export, progress, cancel, `.partial` + rename, eviction-before-enqueue reporting, and second-export rejection; verify each lifecycle case
- [x] 9.5 Implement offline reconstruction of history, filters, Overwrite rows, CANopen associations, heatmap, bit activity, and time presentation without COM; verify offline replay and live-status-unavailable tests
- [x] 9.6 Implement mixed-boot detection from the session header and warning; verify one-token and mixed-token fixtures

## 10. CLI, configuration, headless, and diagnostics

- [x] 10.1 Implement the CLI parser for config, channel/channel-id selection, views, EDS, time/delta/columns, budgets, filters, offline/headless/JSON, mock/soak, UI test modes, and scale; verify help exit 0 and invalid argument exit 2
- [x] 10.2 Implement config load/save with CLI precedence, XDG path resolution, atomic write, corrupted-file defaults without rewrite, unknown-field tolerance, >8 enabled rejection, and >32 profile rejection; verify each case
- [x] 10.3 Implement headless text and JSONL output, `--max-records`, offline open, and empty-input exit codes; verify headless fixtures and exit codes
- [x] 10.4 Implement deterministic mock and soak generators at configurable rates; verify `--headless --soak 60` sustains 50 000 frames/s with zero ingress overflow and stable memory
- [x] 10.5 Implement Status/diagnostic counters for channels, ingress, history, loss breakdown, overwrite, EDS, reindex, export/import, ECO, and memory; verify a status snapshot test

## 11. GUI shell and interaction

- [x] 11.1 Implement the ImGui/GLFW window, theme, embedded font, 1 s splash, UI scale, docking, and layout restore; verify `--ui-smoke` and `--ui-splash-test` under Xvfb
- [x] 11.2 Implement the toolbar, Channels & CANopen panel, Minimap, Status, and configure-channels/restart-capture flows; verify scripted panel and empty-config tests
- [x] 11.3 Implement `Can:` dock windows with Trace columns, optional HW/UTC columns, virtualization, auto-scroll, hotkeys, and per-view controls; verify scripted actions/input tests
- [x] 11.4 Implement Overwrite rows, statistics columns, byte fade, key selection, `Save selected`, and all-frames action; verify scripted Overwrite tests
- [x] 11.5 Implement the Inspector Frame/CANopen/Bit Activity tabs and copy actions; verify focused-record, gap, CANopen, and bit-matrix tests
- [x] 11.6 Implement the ImPlot bus-load heatmap panel with axis/step controls, measured/estimated markers, and click-to-record; verify a scripted heatmap click test
- [x] 11.7 Implement clone dialogs, LIVE/PAUSED limits, node-view orphan banner, and per-mode selection display; verify scripted view/focus tests
- [x] 11.8 Implement the scripted UI drivers `--ui-smoke`, `--ui-actions-test`, `--ui-input-test`, `--ui-view-test`, `--ui-focus-test`, `--ui-splash-test`, and `--ui-scale`; verify all pass under `xvfb-run -a`

## 12. Resources, ECO, and lifecycle integration

- [x] 12.1 Integrate `ResourceGuard` with CPU/storm thresholds, ECO 15 FPS, 250 ms recompute, hysteresis, and immediate input wake; verify simulated-sample and UI-mode tests
- [x] 12.2 Enforce the memory ledger, allocation-failure behaviour for ring/view/snapshot/overwrite/heatmap/EDS, and the 16 MiB degraded mode; verify failure-injection tests
- [x] 12.3 Verify thread priority/affinity validation and the nice-19 export path; verify invalid affinity is rejected before capture and valid specs are applied
- [x] 12.4 Run an end-to-end 5 000 frames/s steady test and a 50 000 frames/s burst test; verify bounded UI work, no hot-path allocation, and visible overflow only when the queue genuinely fills
- [x] 12.5 Verify graceful shutdown with active capture and active export, including partial-file removal, config save, and exit code 0; verify SIGTERM and window-close tests

## 13. Integration, acceptance, and documentation

- [x] 13.1 Add the acceptance scripts for help/exit codes, 8-channel limit, 32-profile offline, 1M/2M capacity, EDS TPDO/SDO/EMCY, per-ID outlier, heatmap click, UTC estimate, and JSONL round-trip; verify the scripts pass
- [x] 13.2 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), TSan (`-DNEXIGEN_ENABLE_TSAN=ON`), and ASan (`-DNEXIGEN_ENABLE_ASAN=ON`) using `scripts/build.sh -j 64`; verify all four trees pass their CTest labels
- [x] 13.3 Add Doxygen comments (`/// @brief`, `@param`, `@return`, `@tparam`) to every new public and internal tool header that requires them; verify the documentation build has no new warnings for `tools/nexigen-canview/`
- [x] 13.4 Update `docs/architecture.md` Mermaid diagrams with the CAN viewer data path, capture/lifecycle, and CANopen/heatmap flows; verify the Mermaid syntax and the documented build commands
- [x] 13.5 Run `graphify update .` after the code lands and verify `graphify-out/` reflects the new tool without graph build errors
- [x] 13.6 Run the full `ctest` suite for the affected labels (`tools;canview`, `tools;logview`, `tools;canmon`) and verify no regression in the existing CLI or log viewer
- [x] 13.7 Verify no public header under `include/nexigen/` changed, no `tx` service is created, and the existing `nexigen-canmon` CLI keeps its flags and exit codes

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived capability specs and the final acceptance results.
