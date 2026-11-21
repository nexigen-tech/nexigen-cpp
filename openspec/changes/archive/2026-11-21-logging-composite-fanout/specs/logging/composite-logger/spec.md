# Spec Delta

## MODIFIED Requirements

### Requirement: Virtual-free fan-out composition

`CompositeLogger<Loggers...>` SHALL accept zero or more types satisfying
`concepts::Logger`, store them in the shared non-polymorphic
`nexigen::core::sink_pack::Fanout<Loggers...>` member pack without virtual functions or
runtime dispatch, and on each accepted variadic record invoke the corresponding
`log`/convenience method on every member via C++23 fold expressions, forwarding the
packed format arguments unchanged. Index-based and type-based member access, ordered
iteration and the static pack size SHALL come from that shared pack. The composite SHALL
itself satisfy `concepts::Logger`, including for a nested `CompositeLogger`, and its
record methods SHALL return `void`. `flush()` SHALL visit every member without
short-circuiting and SHALL return the first member failure.

#### Scenario: Every sink receives every accepted record

- **WHEN** a `CompositeLogger<ConsoleLogger, FileLogger>` at level `Info` is called with `warning("retry {}", 3)`
- **THEN** both sub-loggers receive a record whose rendered message is `retry 3` and each applies its own level filter and sink behavior

#### Scenario: No virtual dispatch exists

- **WHEN** the composite's hot path is compiled in Release
- **THEN** no virtual table lookup appears in the generated code for forwarding (all calls are statically resolved)

#### Scenario: Shared non-polymorphic pack supplies member plumbing

- **WHEN** the composite's member access, ordered iteration or static `size` is used
- **THEN** it resolves through `core::sink_pack::Fanout`, the same pack used by the metrics/tracing/chrono composites, with no virtual table or runtime dispatch added

#### Scenario: Composite of composites works

- **WHEN** a `CompositeLogger` is nested inside another `CompositeLogger`
- **THEN** the outer type still satisfies `concepts::Logger` and fan-out reaches all leaves

#### Scenario: Flush keeps the first failure

- **WHEN** `flush()` visits a member that fails and then a member that succeeds
- **THEN** every member is still flushed and the composite returns the first member's failure
