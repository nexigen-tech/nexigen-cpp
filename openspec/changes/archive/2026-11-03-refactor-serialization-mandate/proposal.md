# Proposal

## Why

The 2026-11-03 analysis (code-smell §5, P3-28) flagged the serialization
mandate pattern (member `to_string` + free `to_string` + `std::formatter`
+ ADL `to_json`/`from_json`) repeated verbatim on dozens of types: 55
delegate-to-`to_string` formatter specializations alone, plus a per-enum
`to_json`/`from_json` pair keyed to the shared `enum_to_json` helper.
The mandate itself is the project's contract — the COST of carrying it on
every new type is not. Codebase already ships 103 `std::formatter`
specializations; a new enum currently needs five boilerplate blocks before
its first use site compiles.

## What Changes

- **core/** — new internal header `nexigen/core/serialization_mandate.hpp`:
  (1) a `MandateToString<T>` concept backed by unqualified-lookup-then-ADL
  `to_string(T)`, (2) ONE constrained partial
  `std::formatter<T> : formatter<string_view>` implementing the whole
  delegate family, (3) a generic
  `nexigen::to_json/from_json(json, E)` pair for enums constrained the same
  way, (4) a CRTP base `MandateString<T>` exporting the free
  `friend std::string to_string(const T&)` forwarding to the member.
- **sweep/** — the per-type `std::formatter` specializations whose body is
  exactly "delegate to to_string" are deleted (generic picks them up);
  per-enum `to_json`/`from_json` one-liners deleted outright (generic
  covers them through ADL + `from_string`); types with composite/manual
  formatting bodies stop delegating through the base and either move to
  `Serialization-mandate` (if the semantics already equal `to_string`) or
  keep their existing EXPLICIT specializer where the render contract is
  genuinely different. Free `to_string` companions become CRTP-base
  exports where the member/free pair was verbatim duplication.
- **tests/** — no test bodies change: the public formatting surface
  (per-enum/formatter round trips, `std::format`, JSON round trips) is
  already pinned by the serialization spec's test matrix; new types keep
  compiling with the mandate implemented only via their member.

## Capabilities

### New Capabilities

(none — the surface contract
`core/serialization` "Universal cold-path to_string contract", "Universal
Enum std::formatter specialization", "Universal Enum JSON serialization"
stays exactly as written; implementation costs reduce.)

### Modified Capabilities

(none — no requirement text changes; the spec mandates OUTCOMES, and the
generic machinery implements the same outcomes. Frozen contract noted in
design.)

## Impact

- Code: new `include/nexigen/core/serialization_mandate.hpp`; deletions of
  ~40+ formatter specializations and ~15 enum JSON one-liner pairs across
  `core/`, `logging/`, `metrics/`, `tracing/`, `chrono/`, `app/`,
  `runner/`, `hub/`, `diagnostics/` headers; a few remaining explicit
  cases are annotated as exceptions.
- Tests: none removed (the existing serialization mandate gauges keep
  passing); a couple of static_asserts may move into the mandate header.
- Performance/latency/allocation: zero hot-path delta — formatting is
  cold path; the generic formatter produces the same `formatter<string_view>
  ::format(to_string(v))` call chain the deleted specializations used; no
  new allocations beyond the existing per-call `std::string` from
  `to_string` (mandate 9's documented cold-path diagnostic cost).
- Deferred explicitly: none (this closes the last open P3 analysis item;
  hub dashboard follows as its own change).
