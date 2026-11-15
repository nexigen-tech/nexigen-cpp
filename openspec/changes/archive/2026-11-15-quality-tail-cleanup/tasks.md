# Tasks

- [x] Make the real-daemon log e2e publish-retry robust and verify the full suite.
- [x] Re-verify the `[::1]` hub loopback case; keep the CLI regression test.
- [x] Migrate `CompositeLogger` to `core::sink_pack::Fanout` with behavior parity.
- [x] Decompose `run_suite_supervisor` into runtime init, control install, start/report, and loop.
- [x] Verify `run_instance_supervisor` is already decomposed into pipe/fork/child/handshake/finalize stages (prior refactor).
- [x] Extract HttpServer transport parsing (`http_transport.*`) and SSE formatting (`http_sse.*`); routing stays in the `Impl` orchestration shell.
- [x] Run Debug/Release/TSAN/ASAN and gate-OFF; run gates and graphify.
