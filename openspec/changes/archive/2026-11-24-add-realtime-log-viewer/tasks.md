# Tasks

## 1. Scaffolding and dependencies

- [x] 1.1 Add the `NEXIGEN_BUILD_LOGVIEW` CMake option (Linux-only, requires `NEXIGEN_ENABLE_COM`),
      the `tools/nexigen-logview/` target skeleton, and links to `nexigen_logging_com`, `nexigen_com`,
      `nexigen_chrono`, `nexigen_core`; verify configure/build with the option ON and that a COM-off
      configure leaves the target out.
- [x] 1.2 Pin Dear ImGui (docking branch commit), GLFW 3.4, and the OFL-licensed JetBrains Mono asset
      with FetchContent hashes; verify a clean configure resolves them and the build host does not need
      system `glfw3`.
- [x] 1.3 Add a headless `nexigen_logview_tests` target for the model layer (ring, filter, time,
      colors, import/export, resource guard) that links no OpenGL; verify it builds and runs without a
      display.
- [x] 1.4 Add CLI parsing for `--config`, `--service`, `--offline`, `--memory-mb`, `--headless`,
      `--help`; verify the `--help` and invalid-argument tests.

## 2. Stage A — capture and history (headless)

- [x] 2.1 Implement the receive thread: `com::Node`, `Subscriber<LogFrame, TransportHeader>`,
      `EventListener` on `<service>Event`, `WaitSet` drain with an interval fallback, and
      `SpscRingBuffer<IngressRecord, 65536>`; verify with a mock producer and a real-transport test that
      frames arrive and the thread idles without spinning.
- [x] 2.2 Implement `RecordRing`: capacity from `--memory-mb` (power of two, default 524 288 at 256 MB),
      one `mmap` allocation, absolute numbering, overwrite-oldest eviction, and the side arrays from the
      design; verify unit tests for capacity derivation, wrap-around, eviction counts, and allocation
      failure.
- [x] 2.3 Implement the fixed application registry (256 slots), per-application counters/last stamp,
      `prev_same_app_ns` at ingest, and the 1024-bucket minimap counters; verify unit tests for a new
      app, a repeated app, more than 16-byte ids, and eviction decrements.
- [x] 2.4 Implement gap records for transport `lost_before`, ingress overflow, and a non-zero publisher
      `dropped_counter`; verify a test that injects each loss and asserts one correctly attributed gap
      record and counter.
- [x] 2.5 Add a `--headless` dump path (one line per ingested/gap record, bounded) so the backend is
      testable without GL; verify a mock 50 000-record run dumps them and exits 0.
- [x] 2.6 Run the headless model and transport tests under ThreadSanitizer and AddressSanitizer; verify
      zero reports and no leaks.

## 3. Stage B — UI shell, table, filtering, colors

- [x] 3.1 Create the ImGui + GLFW + OpenGL3 window with the docking branch, the dark theme, and the
      embedded JetBrains Mono; verify a smoke test that opens, renders one frame, and closes under
      Xvfb.
- [x] 3.2 Implement the virtualized table with the required columns, `ImGuiListClipper`, selection
      styling, and the context column; verify a 500 000-row synthetic model renders only the visible
      rows and meets the per-frame budget.
- [x] 3.3 Implement the sources and levels panels, the combined filter, and the incremental visible
      index with 20 000-record chunks and progress; verify unit tests for append, rebuild, clear, and
      eviction during rebuild.
- [x] 3.4 Implement find (Ctrl+F, Enter/Shift+Enter), selection (click/Shift/Ctrl/Ctrl+A), and
      bookmarks (F2, next/prev); verify tests for visible-order ranges, selection persistence across a
      filter change, and bookmark navigation.
- [x] 3.5 Implement level and application colors, the two color modes, `ColorEdit4` overrides, Reset,
      and JSON persistence; verify round-trip, defaults, a new app receiving a palette color, and the
      criticality branch.
- [x] 3.6 Implement the toolbar (pause/live + new count, clear, save/open, reset, mode badge, counters,
      rate), the message inspector with copy and truncation hint, and return-to-live; verify the UI
      smoke test and a paused-state test.
- [x] 3.7 Add `/// @brief`/`@param`/`@return` Doxygen to every touched public header and document the
      key bindings; verify no new header-doc warnings.

## 4. Stage C — time, export, import, ECO

- [x] 4.1 Implement the three timestamp modes, the three delta modes, adaptive formatting, anomaly
      highlighting, and the T0 anchor; verify unit tests for each mode, negative deltas, and anchor
      replacement.
- [x] 4.2 Implement the minimap rendering and Error/Fatal/bookmark marks; verify with a synthetic
      history containing known positions.
- [x] 4.3 Implement export through the 4096-slot `ExportRecord` queue and the writer thread with
      progress and cancel; verify JSONL escaping (quote, backslash, newline), CSV quoting, TXT output,
      and an export round-trip.
- [x] 4.4 Implement import for the viewer's JSONL, `nexigen-logd` JSONL, and plain text in bounded
      chunks with malformed-line counting and boot-token warnings; verify fixtures for each format and
      a mixed-boot warning.
- [x] 4.5 Implement the resource guard: process CPU sampling, entry/exit hysteresis, the 100k msg/s
      storm trigger, 15 FPS event waiting, 250 ms recomputation, and the badge tooltip; verify with a
      simulated load harness and a test clock.
- [x] 4.6 Apply the thread specs (low-priority capture, lower-priority export, affinity) and reject an
      invalid specification at startup; verify an affinity test on a two-CPU mask and the rejection
      path.
- [x] 4.7 Wire `--offline <file>` so no COM node is created; verify a file opens and renders with COM
      unavailable and the live state reads "unavailable".

## 5. Integration, documentation, release

- [x] 5.1 Run Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer
      (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) with
      `scripts/build.sh … -j 64`; verify 100% of the model, transport, and tool tests pass in all four.
- [x] 5.2 Verify the memory budget at 256 MB and 384 MB: reported capacity, model footprint, and RSS
      after a full wrap stay within the configured cap.
- [x] 5.3 Run a 50 000 msg/s mock soak (10 minutes) and verify the UI stays interactive, losses are
      counted, and ECO enters and leaves according to the thresholds.
- [x] 5.4 Update `docs/architecture.md` with a Mermaid diagram of the two-stage pipeline and the
      service contracts, and add a `docs/articles/log-viewer.md` usage page; verify the Mermaid renders
      and the commands in the article run as written.
- [x] 5.5 Add the binary to the install/package rules; verify `cmake --install` places `nexigen-logview`
      and the font license.
- [x] 5.6 Run `graphify update .` after the code changes and verify the graph rebuild completes without
      errors.

## Workflow follow-up

- Implement through `/opsx-apply` after `fix-log-subscriber-capacity` has landed.
- Archive the change after the review requirements are satisfied.
