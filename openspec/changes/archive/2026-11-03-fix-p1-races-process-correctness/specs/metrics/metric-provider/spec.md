# metrics/metric-provider delta spec

## ADDED Requirements

### Requirement: Meter publication is serialized with harvesting

`get_meter` SHALL perform its lookup, creation, and registry publication under
the provider's harvest mutex — the same mutex the harvester holds while
resetting pools — so concurrent `get_meter` calls, a running harvester, and
conflicting-version lookup racing creation are linearized at the mutex. The
call stays cold-path; instrument hot paths SHALL NOT touch the mutex.

#### Scenario: get_meter races the harvester

- **WHEN** one thread calls `get_meter` on a new identity while the harvester
  thread is resetting pools under the harvest mutex (TSan build)
- **THEN** TSan reports no data race, and the harvester's sweep observes either
  no reference to the not-yet-published meter or a fully constructed meter

#### Scenario: Concurrent identical get_meter hands out one meter

- **WHEN** two threads call `get_meter` concurrently with the same
  name+version identity
- **THEN** registry capacity is not double-consumed: both calls return a
  handle to the single constructed meter, and a concurrent
  conflicting-version call still reports `InvalidArgument`
