# Proposal

## Why

`docs/header-compilation-audit.md` records 69 cold-path `to_json`/`from_json` definitions still living inline in public headers of compiled modules. The largest six groups are worth moving now because every consumer translation unit recompiles them for no benefit:

| Header | Inline JSON lines |
|---|---:|
| `metrics/metric_frame.hpp` | 35 |
| `metrics/json_file_metric_sink.hpp` | 30 |
| `metrics/iceoryx2_metric_sink_config.hpp` | 26 |
| `chrono/json_file_sink_config.hpp` | 19 |
| `chrono/console_sink.hpp` | 10 |
| `metrics/console_metric_sink.hpp` | 10 |

This is the follow-up the previous change scheduled: the rule already exists in `quality/code-structure` ("cold-path serialization of compiled modules is built once"), and the pattern is already used by `logging/console_logger_config`, `logging/logger_options`, `logging/log_frame`, `metrics/metric_daemon_config` and others.

## What Changes

- Move the `to_json`/`from_json` bodies of the six types above from their public headers into `src/<module>/<entity>.cpp`, leaving declarations in the headers.
- Keep the trivial free `to_string(const T&)` (one-line delegation to the member) inline in the header, matching the established pattern in `logging/logger_options.hpp`. The member `to_string()` also stays inline.
- No behavior, field-name, accept/reject, formatter or diagnostic change.

## Capabilities

No capability deltas: pure code placement with no observable behavior change (`.openspec.yaml` sets `skip_specs: true`).

## Impact

- **Code:** six headers lose their definition blocks; six new `src/<module>/<entity>.cpp` translation units appear (picked up by the existing `GLOB ... CONFIGURE_DEPENDS` source lists).
- **API / ABI:** header declarations unchanged. The static libraries gain *strong* `T` symbols where consumers previously emitted *weak* `W` copies, which is the expected consequence of compiling the body once; no declaration appears or disappears.
- **Performance / latency / memory:** none. All six are cold-path configuration/diagnostic serializers that the mandates keep off producer and worker hot paths; no allocation pattern, layout or `std::memory_order` changes.
- **Build:** slightly fewer tokens per consumer TU that includes these headers; no new dependency.
- **Out of scope:** the remaining 24 short enum pairs, which stay beside their `to_string`/`from_string` vocabulary.
