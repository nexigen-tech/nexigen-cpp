# Proposal

## Why

The AUTOSAR roadmap is closed, but four quality tails remain: a flaky
real-daemon log test, a hub CLI IPv6 warning that is now covered but still
tracked as a known failure, and three structural refactors
(`CompositeLogger`, `HttpServer::Impl`, and the two supervisor loops).

## What Changes

- Make the real-daemon log e2e publish until the message is observed instead of
  relying on a fixed startup sleep; keep the per-PID service isolation.
- Re-verify the `[::1]` loopback case and keep the existing hub CLI regression
  test as the guard.
- Migrate `CompositeLogger` onto `core::sink_pack::Fanout` while preserving
  level control, member access, flush-first-error, and diagnostics.
- Decompose `run_suite_supervisor` and `run_instance_supervisor` into named
  stages without behavior changes.
- Split `hub::HttpServer::Impl` into transport, routing, and SSE helpers.

## Impact

- **Performance:** no hot-path changes; refactors preserve the existing
  allocation and latency contracts.
- **Compatibility:** no public API changes. Behavior is preserved; the log test
  becomes timing-robust.
- **Specs:** pure quality/refactor work, archived with `skip_specs`.
