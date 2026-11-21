# Proposal

## Why

`CompositeLogger` was the fourth structural copy of the variadic fan-out scaffolding
(tuple storage, `find_index`/`exactly_one`, `get<Index>`/`get<T>`, `for_each`, `size`).
The `dedup-sink-plumbing` change extracted that scaffolding into
`nexigen::core::sink_pack::Fanout` for the metrics/tracing/chrono composites and deferred
logging because its stateful `set_level` tree and `initialize` orchestration needed a
dedicated review. The pack inheritance has since landed, but the header still carried
residual hand-written pieces (a shadowed `size`, an unnecessary `Base` alias and
`using Base::get`, and a bespoke first-failure `flush` fold), and the capability spec
still says members are stored "without a base class" — which is no longer literally
true and hides the shared non-polymorphic pack contract.

Closing this item makes `CompositeLogger` a first-class consumer of the one shared pack,
leaving logging-specific policy (level propagation, member initialization, diagnostics)
in the module, and records the contract in the spec.

## What Changes

- Keep `CompositeLogger<Loggers...>` inheriting `nexigen::core::sink_pack::Fanout<Loggers...>`;
  delete the residual duplicates: the shadowing `static constexpr size`, the `Base` alias
  with `using Base::get`, and the hand-written first-failure `flush` fold.
- Express `flush()` through `core::sink_pack::combine`, matching the metrics/tracing/chrono
  composites: every member is still visited and the first failure wins.
- Preserve the public contract exactly: `set_level`/`level`, `initialize`-then-log
  orchestration through `get<...>()`, `flush`, `stop`-free record surface, `to_string`,
  concept conformance, default-only construction and deleted copy/move.
- Reword the `logging/composite-logger` "Virtual-free fan-out composition" requirement:
  members are stored in the shared non-polymorphic `sink_pack::Fanout` pack (no virtual
  functions, no runtime dispatch), and member access/iteration/size come from it.

**BREAKING**: none.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logging/composite-logger`: the fan-out requirement now names the shared
  `core::sink_pack::Fanout` member pack and documents first-failure `flush` combination.

## Impact

- **Code**: `include/nexigen/logging/composite_logger.hpp` only (≈ −10 net lines of
  duplicated scaffolding; documentation brought in line with the actual base).
- **API**: unchanged. `CompositeLogger::size`, `get<Index>()`/`get<T>()`, construction,
  deleted copy/move, level surface, record methods, `flush()` and `to_string()` keep their
  existing signatures and semantics (the static `size` now resolves through the public base).
- **Performance / latency / allocation impact**: zero. `sink_pack::Fanout` is compile-time
  only; record methods keep the same relaxed level load, short-circuit and ordered
  `std::apply` comma-fold codegen. `flush()` is the existing control-plane barrier and
  stays allocation-free; `to_string()` remains cold-path.
- **Tests**: existing `nexigen_logging_tests` composite, level, producer, stress and example
  coverage is expected to pass unmodified; no new test scenarios are required because the
  behavior is unchanged and already pinned.
- **Docs**: Doxygen comments in the touched header point at the shared pack; no
  architecture diagram change (the composition topology is unchanged).
