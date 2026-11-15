# Spec Delta

## MODIFIED Requirements

### Requirement: E2E profile vocabulary

`nexigen::com` SHALL expose an `E2eProfile` enum with `None`, `P04`, `P05`,
`P06`, and `P07`, with the full conversion set (`to_string`/`from_string`, ADL
`to_json`/`from_json`, and `std::formatter`). `P04` and `P07` SHALL use CRC-32
(`0xF4ACFB13`); `P05` and `P06` SHALL use CRC-16 (`0x1021`). `None` SHALL select
plain communication: the application uses the ordinary ports and no E2E
envelope is created.

#### Scenario: Profile names round-trip

- **WHEN** each `E2eProfile` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders it

#### Scenario: Profile selects the checksum width

- **WHEN** a message is sealed with `P04` and another with `P06`
- **THEN** the first carries a 32-bit checksum and the second a 16-bit checksum

#### Scenario: None disables protection

- **WHEN** a service is configured with `None`
- **THEN** the application uses the plain `Publisher`/`Subscriber` or `Client`/`Server` ports, so messages carry no E2E envelope at all

## ADDED Requirements

### Requirement: Wrappers require a protection profile

Creating `E2ePublisher`, `E2eSubscriber`, `E2eClient`, or `E2eServer` SHALL fail
with `InvalidArgument` when the configured profile is `None`, before any
transport port is opened. A wrapper therefore never emits or accepts an
envelope without a checksum.

#### Scenario: Wrapper creation with None is rejected

- **WHEN** any E2E wrapper is created with `profile = None`
- **THEN** creation returns `InvalidArgument` and no transport port or service is opened

#### Scenario: Protected profiles are accepted

- **WHEN** a wrapper is created with `P04` or `P06`
- **THEN** creation succeeds and the port is opened as before
