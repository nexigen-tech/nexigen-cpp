# Design

## Decisions

- **Flaky log e2e:** publish the probe record in a bounded retry loop and poll
  the log directory after each attempt. The daemon has no readiness signal, so a
  single early publish can be lost during service discovery; retrying is the
  correct cold-path fix.
- **Hub IPv6:** `classify_listen_host` already strips the bracket form and
  `warn_if_non_loopback` treats `::1` as loopback. The existing
  `nexigen_hub_cli_tests` case is the regression guard; no production change is
  needed if it stays green.
- **CompositeLogger:** inherit `core::sink_pack::Fanout<Loggers...>` for member
  storage, `get<>`, and ordered fan-out; keep the composite's level cell,
  `log`, convenience methods, first-error `flush`, and diagnostics.
- **Supervisors:** extract child setup, handshake/report, and the supervision
  loop into named functions/classes; preserve ordering and failure codes.
- **HttpServer:** keep `HttpServer` public; move helper types and free
  transport/SSE/routing functions into internal headers/translation units and
  leave `Impl` as the orchestration shell.

## Risks

- **Refactor-only regressions** are covered by the existing unit/CLI tests;
  changes are mechanical and behavior-preserving.
- **HttpServer split** is the largest risk; it is done in small extractions with
  the existing HTTP/SSE tests as the gate.
