# Proposal

## Why

25 enum headers hand-write identical serialization boilerplate — a `to_string`
switch, a `from_string` if-chain, and `to_json`/`from_json` bodies. A token-level
clone scan measured ~1.3k structurally duplicated tokens (each enum header
matches the others at 125–149 tokens per pair, e.g. `infrastructure_stop_reason.hpp`
vs `clock_log_level.hpp`/`settings_source.hpp`/`stopwatch_state.hpp`/`log_level.hpp`/
`rotation_schedule.hpp`).

The mandate machinery already collapsed the `std::formatter` family and the free
`to_string` pair (`core/serialization_mandate.hpp`), and a generic
`enum_to_json`/`enum_from_json`/`read_enum` pair already exists — but it sits in
`nexigen::runner::detail::sys_helpers.hpp` (a module-level detail header, an odd
home for a project-wide engine), and only hub uses it; the other 25 headers keep
the bodies inline. Every new enum costs ~50 copy-pasted lines.

## What Changes

- New `include/nexigen/core/enum_traits.hpp` (file-per-entity):
  `core::EnumTraits<E>` — a per-enum specialization holding
  `static constexpr std::array<Entry, N> entries` with `Entry{E value; std::string_view name}`;
  plus the constexpr engine `core::enum_name(E)` (fallback `"Unknown"` for
  out-of-range values, preserving today's `default:` arm) and
  `core::enum_value(std::string_view, E&)` (exact, case-sensitive match).
- Lift `enum_to_json`/`enum_from_json`/`read_enum` from `runner::detail` into
  `core::mandate` (beside `serialization_mandate.hpp`); update all existing
  call sites (hub `model.cpp` and friends) so exactly one engine exists.
- For each of the 25 enum headers: replace the switch/chain/json bodies with an
  `EnumTraits<X>` specialization plus one-line ADL glue (`to_string`,
  `from_string`, `to_json`, `from_json` must stay in the enum's own namespace —
  the associated-namespace constraint is documented and experimentally verified
  in `serialization_mandate.hpp`; existing per-type definitions keep outranking
  templates where a module wants a custom shape).
- Wire shape stays byte-identical: same vocabulary strings, same case, same
  "leave `out` unchanged on unknown name" parse semantics.

## Capabilities

Pure refactor — public API, JSON vocabulary and formatter behavior unchanged.
`skip_specs: true`.

## Impact

- Hot path: none. Enum serialization is init/diagnostics only (mandates 4/12);
  no producer, tick, sink or snapshot code is touched.
- Allocations: none added — the name table is `static constexpr`, lookup is a
  constexpr linear scan over a small array, same cost class as today's switch/if-chain.
- Removals: ~800–1000 lines of boilerplate across 25 headers; a new enum costs a
  table plus ~5 lines.
- Tests: existing vocabulary tests (`logging/enum_vocabulary_test.cpp`,
  `metrics/policy_enums_test.cpp`, app/chrono/runner/tracing settings tests) pin
  the wire format and must pass unmodified — that is the refactor's acceptance proof.
- Risk: the `runner::detail` lift must move every call site in the same commit
  (two engines would drift); `ErrorCode` in `core/result.hpp` migrates too —
  confirm its vocabulary contains no surprises before conversion.
