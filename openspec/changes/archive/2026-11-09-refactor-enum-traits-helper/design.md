# Design

## Context

See proposal.md - Why. Constraints that shape the approach:

- ADL: an enum type's associated namespace set is its *immediate* namespace only
  (verified experimentally when `serialization_mandate.hpp` was written), so
  `to_json`/`from_json`/`to_string`/`from_string` must remain *declared* in each
  enum's own header; only their *bodies* can move to core.
- `core::` headers must not pull `<nlohmann/json.hpp>` into hot-path consumers;
  `serialization_mandate.hpp` already includes it, and `enum_traits.hpp` sits in
  the same cold-serialization layer.
- Non-concurrent change: all new data is `static constexpr` immutable tables; no
  atomics, no shared mutable state, so the alignas/memory-order mandate does not
  apply.

## Goals / Non-Goals

**Goals:**
- One engine (`core::EnumTraits<E>` + `core::enum_name`/`enum_value` + the lifted
  json glue) serving all enums.
- Every migrated enum header keeps its exact vocabulary strings, parse
  semantics, and the four ADL entry points.
- Compile-time only cost: tables and lookups are constexpr.

**Non-Goals:**
- No changes to struct/serialization bodies (`*_format.cpp` stay as they are).
- No new public vocabulary, renaming of enumerators, or case changes.
- Not migrating per-type custom shapes where a module deliberately outranks the
  generic (documented escape hatch in `serialization_mandate.hpp`).

## Decisions

1. **Traits-table, not reflection/macros.** `EnumTraits<E>` is specialized in the
   enum's own header next to the type (keeps table next to enumerators, reviewable
   in diffs). Rejected: codegen step (build complexity), macro DSL (fails
   `check_naming.py` readability), compiler reflection (not in C++23).
2. **Engine lives in `core::enum_traits.hpp`, json glue in `nexigen::core::mandate`.**
   Mirrors the existing split (mandate helpers in a dedicated nested namespace to
   avoid a second bare `detail` candidate in `using namespace nexigen::core` scopes).
3. **Lift-and-delete for `runner::detail` helpers.** No forwarding aliases left
   behind: two engines drift. `sys_helpers.hpp` keeps only runner-specific
   helpers; call sites (`src/hub/model.cpp`, any `read_enum` user) switch to
   `core::mandate::` in the same commit.
4. **Lookup strategy: constexpr linear scan** over `entries` (max table size in
   repo is 7 values — `LogLevel`); asymptotics equal today's switch, and GCC/Clang
   vectorize the `string_view` compare loop at -O3. Rejected: sorted table + binary
   search (unnecessary for N<=16, hurts table readability).
5. **Out-of-range fallback preserved**: `enum_name` returns `"Unknown"` when the
   value is absent from the table (matches the current `default:` arm);
   `enum_value` returns `false` and leaves the output untouched (matches today's
   `from_string`).

## Risks / Trade-offs

- **Wording drift** on any enum's vocabulary → mitigated by byte-identical
  assertions in existing vocabulary/settings tests, which run unmodified.
- **`ErrorCode` special case**: it appears in ~every error path; migrate last in
  the core group and diff the produced names against the old switch before
  deleting it.
- **ADL regression**: if a glue function is accidentally dropped into `core::`
  only, `nlohmann` lookup silently fails for consumers; the formatter + tests
  that call unqualified `to_string(...)` inside module namespaces catch this.
