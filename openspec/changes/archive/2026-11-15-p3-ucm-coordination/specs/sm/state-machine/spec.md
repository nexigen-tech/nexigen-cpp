# Spec Delta

## MODIFIED Requirements

### Requirement: Transition result vocabulary

`nexigen::sm` SHALL expose a `TransitionResult` scoped enum with the values `Success`, `RejectedByPriority`, `RejectedByUpdate`, `InvalidGraph`, and `Timeout`, with the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json`, and `std::formatter`).

#### Scenario: TransitionResult round-trips

- **WHEN** each `TransitionResult` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name
