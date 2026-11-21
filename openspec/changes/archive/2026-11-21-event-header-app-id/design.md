# Design

## Context

`EventHeader` is the user header of an event channel and the only metadata a raw transport consumer
sees. It is currently exactly 64 bytes and has no spare space. `TransportHeader` already carries a
four-byte application code for log, metric, and trace frames, but events use `EventHeader` instead.

## Decisions

### D1. Add `char app_id[4]` at offset 64

The header becomes 72 bytes including the alignment padding. Four ASCII bytes match
`TransportHeader`, `LogFrame`, `MetricFrame`, and `SpanFrame`, so every stream uses one application
identity shape. Existing field offsets stay unchanged.

### D2. The producer stamps the code

`EventProducer::create()` and `EventChannel::producer()` get an overload taking a
`std::string_view app_id`. The producer copies at most four bytes and zero-fills the rest. The
existing overloads keep an empty code, so no existing call site changes.

### D3. The recorder prefers the header code

`nexigen-recd` names an event channel `event.<app_id>` when `app_id_text()` is non-empty and
`event.<service>` otherwise. The schema declares `app_id` at offset 64 and the payload at offset 72,
matching `sizeof(EventHeader)`.

## Risks / Trade-offs

- **Wire-format break**: old recordings and old producers are not compatible; all components are
  rebuilt together.
- **72-byte header**: slightly larger event envelope; event publish remains one shared-memory copy.
