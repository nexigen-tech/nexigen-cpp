# Design

## Context

`MetricIdentity` carries `meter_name`, `meter_version`, and `metric_name` as fixed-size
`char[max_name_length + 1]` arrays; `Meter::create_*` routes through `create_instrument`, which
copies the identity into the pre-allocated slot. `MetricRecord` carries the identity plus a
kind-tagged union. The JSON-lines sink writes each record manually, and the hub renders the metrics
stream client-side. No unit exists anywhere in that chain.

## Goals / Non-Goals

**Goals:**

- Attach an optional free-form unit to an instrument at creation, stored in the fixed-size identity.
- Serialize the unit through the JSON diagnostics and the JSON-lines sink, and render it in the hub.

**Non-Goals:**

- No unit normalization, conversion, or validation beyond the existing name-length bound.
- No structured unit enum (a free-form string is sufficient and stays flexible).

## Decisions

### D1 — Unit lives in `MetricIdentity` as a fixed-size string

Add `char unit[max_name_length + 1]{}` to `MetricIdentity`, reusing `max_name_length` so no new
constant or validation is needed.

- *Rationale:* the unit is a property of the metric alongside its name; the identity already
  round-trips and is copied verbatim into the registry slot.
- *Alternative considered:* a separate `max_unit_length`. Rejected as unnecessary — units are short
  and the extra 64 bytes per instrument slot is negligible.

### D2 — Optional trailing `unit` parameter on `Meter::create_*`

Add `std::string_view unit = {}` as the last argument of the six creation methods, forwarded to
`create_instrument`, which copies it only when non-empty.

- *Rationale:* a defaulted parameter keeps every existing call site compiling unchanged; an empty
  unit means "no unit" and serializes as nothing.
- *Alternative considered:* a separate `create_counter_with_unit`. Rejected as API duplication.

### D3 — Serialize and render only non-empty units

- `MetricIdentity` `to_json`/`from_json` carry `unit` (empty string when absent).
- The JSON-lines sink appends `,"unit":"<escaped>"` only when non-empty, to avoid bloat.
- The hub formatter appends ` <unit>` to the value when present.

- *Rationale:* keeps the existing JSONL lines unchanged for instruments without a unit.

## Risks / Trade-offs

- [Registry size] Each slot grows by 64 bytes. → Mitigation: configurable capacities already bound
  total memory; the growth is a small fixed per-slot cost.
- [Key drift in the hub] The client reads `record.unit`; if absent, it renders without a unit. →
  Mitigation: the formatter guards on the field's presence.

## Migration Plan

Not applicable — an additive, backward-compatible API extension with no persisted format change
beyond an optional field.

## Open Questions

None.
