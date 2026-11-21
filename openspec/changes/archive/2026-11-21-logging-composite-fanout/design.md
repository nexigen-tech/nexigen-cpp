# Design

## Context

`nexigen::core::sink_pack::Fanout<Sinks...>` (`include/nexigen/core/sink_pack.hpp`) is the
single implementation of the compile-time member pack used by the chrono, metrics and
tracing composite sinks. It owns tuple storage, `find_index<T>()`/`exactly_one<T>()`,
`get<Index>()`/`get<T>()`, ordered const/non-const `for_each`, the `size` constant,
default/injection construction and the first-failure `combine` helper.

`CompositeLogger<Loggers...>` needs the same member plumbing but has a different policy
surface: a per-composite relaxed level cell, tree-wide `set_level` propagation,
per-member `initialize` through `get<...>()`, and a diagnostics format that includes the
composite level. The `dedup-sink-plumbing` change deliberately deferred it for that
reason.

The inheritance itself is already present in the header; what remains is residual
duplication and a capability-spec wording that predates the shared base.

## Decisions

### D1. Inherit the shared non-polymorphic pack

`CompositeLogger` publicly inherits `core::sink_pack::Fanout<Loggers...>`. The base has no
virtual functions, no virtual destructor and no runtime dispatch: it is tuple storage plus
`constexpr` lookup/iteration, so the `concepts::Logger` hot path still resolves every
member call statically. This matches `CompositeMetricSink`, `CompositeTraceSink` and
`CompositeSink` and keeps one implementation of index/type access and ordered iteration.

The derived class no longer redeclares `size`; the inherited public `Fanout::size` is
exposed as `CompositeLogger<...>::size` exactly as before.

### D2. Keep the level tree in the logging module

`Fanout` is policy-free. The composite's own `detail::LevelControl level_` stays a member
of `CompositeLogger`, because `level()` snapshots and tree-wide `set_level` propagation
are logging semantics, not pack plumbing. `set_level` still writes its own cell first and
then folds over `for_each` in pack order; `log()` still short-circuits on the composite
cell before touching members. No atomics, memory orders or cache-line layout change: the
relaxed `LevelControl` cell and each member's own relaxed cell are untouched.

### D3. Keep default-only construction and `initialize` orchestration

Unlike the observability sink composites, `CompositeLogger` does not inherit the pack's
member-injection constructor. Logging sinks are constructed by the application and then
configured/initialized individually through `get<...>()` (see the composite example and
tests). Keeping the default constructor plus the `is_default_constructible` gate preserves
that contract and avoids expanding the public construction surface in a deduplication
change.

### D4. Use the shared first-failure combination for `flush`

`flush()` becomes:

```cpp
core::Status result{};
this->for_each([&result](auto& lg) noexcept {
    result = core::sink_pack::combine(result, lg.flush());
});
return result;
```

`combine` keeps the accumulated failure and otherwise takes the next status, so the
behavior is identical to the previous `have_first` loop: every member is flushed, a later
OK never masks an earlier failure, and an empty pack returns OK. The code now matches the
other three composites verbatim in shape.

### D5. Remove residual hand-written scaffolding

- Delete `static constexpr std::size_t size = sizeof...(Loggers);` — it shadowed the base
  constant with the same value.
- Delete `using Base = ...;` and `using Base::get;` — public inheritance already exposes
  both `get` overloads and no derived `get` hides them.
- Drop the now-unused `<tuple>` include; the header no longer names `std::tuple` directly.
- Update the file/class comments to name the shared pack instead of the historical
  hand-written tuple.

## Risks / Trade-offs

- **`size` moves to the base** → source-compatible: `CompositeLogger<...>::size` still
  names a public `static constexpr std::size_t`; tests and diagnostics use it unchanged.
- **`flush` rewrite** → the shared `combine` has exactly the old first-failure semantics;
  the existing `/dev/full` + OK-member scenario pins it.
- **Spec wording change** → the old "without a base class" sentence was meant to forbid a
  polymorphic base; the new wording states the actual invariant (non-polymorphic pack, no
  virtual functions or runtime dispatch) without loosening the hot-path mandate.
- **No behavior change** → no new runtime risk; the remaining risk is purely build/format
  regressions, covered by the verification matrix.
