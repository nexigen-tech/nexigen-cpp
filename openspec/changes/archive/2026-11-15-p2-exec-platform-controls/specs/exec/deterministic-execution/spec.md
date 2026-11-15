# Spec Delta

## ADDED Requirements

### Requirement: Deterministic cycle clock

`nexigen::exec::DeterministicClient` SHALL provide a fixed-cycle clock built
from a non-zero `cycle_period_ns` and a seed. `wait_for_next_cycle()` SHALL
advance the cycle index and sleep until `start + index * period`;
`cycle_index()` and `cycle_time_ns()` SHALL expose the scheduled cycle. A late
wake-up SHALL advance the index by one, not emit a catch-up burst. The client
SHALL be allocation-free and single-threaded by contract.

#### Scenario: Cycle index advances once per wait

- **WHEN** `wait_for_next_cycle()` is called three times
- **THEN** `cycle_index()` reports three and the scheduled time matches the period

#### Scenario: Late cycle does not burst

- **WHEN** the caller wakes after more than one period
- **THEN** only one cycle is consumed and the next deadline is recomputed from the new index

### Requirement: Deterministic pseudo-random sequence

`DeterministicClient::random_u64()` SHALL use a splitmix64 generator seeded from
the configuration. Two clients with the same seed SHALL produce the same
sequence; a zero seed SHALL be accepted and mapped to a non-zero internal state.

#### Scenario: Same seed yields the same sequence

- **WHEN** two clients are created with the same seed and each draws five values
- **THEN** both sequences are identical

#### Scenario: Different seed diverges

- **WHEN** two clients use different seeds
- **THEN** their first drawn values differ

### Requirement: Deterministic configuration conversion set

`DeterministicConfig` SHALL carry `cycle_period_ns` and `seed` and SHALL provide
`to_string()`, ADL JSON, and `std::formatter`. `DeterministicClient` SHALL
provide a cold-path `to_string()` and `std::formatter` naming the period, seed,
and current cycle.

#### Scenario: Config round-trips

- **WHEN** a configuration passes through `to_json`, `from_json`, `to_string`, and `std::format`
- **THEN** the period and seed are preserved

#### Scenario: Client renders its cycle

- **WHEN** `to_string()` and `std::format` are evaluated after a cycle
- **THEN** the text names the period, seed, and cycle index
