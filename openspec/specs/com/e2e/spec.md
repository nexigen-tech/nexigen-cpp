# com/e2e Specification

## Purpose

Protects service exchange on the local transport against corruption, repetition,
loss, reordering, and misdelivery, and delivers the check result to the
application so it can reach a safe state.

## Requirements

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

### Requirement: E2E envelope and checksum

An E2E message SHALL be a trivially copyable envelope carrying `data_id`,
`counter`, `length`, and `crc` beside the payload. Sealing SHALL compute the
checksum over the header fields and the payload bytes returned by the payload's
byte view, and checking SHALL recompute it and reject a mismatch as `Error`.

#### Scenario: Sealed message verifies

- **WHEN** a payload is sealed and immediately checked with the same profile and data id
- **THEN** the check returns `Ok`

#### Scenario: One flipped payload bit is an error

- **WHEN** a single bit of the sealed payload changes before the check
- **THEN** the check returns `Error` and does not expose the payload as valid

#### Scenario: Length mismatch is an error

- **WHEN** the declared length differs from the received payload size
- **THEN** the check returns `Error` before evaluating the counter

### Requirement: Sender counter progression

The sender SHALL increment the profile's counter for every sealed message,
wrapping at the profile's counter width, and SHALL NOT reuse a counter value for
two consecutive messages.

#### Scenario: Counters increase

- **WHEN** three messages are published in sequence
- **THEN** their counters increase by one each, modulo the profile width

#### Scenario: A new sender starts at one

- **WHEN** an E2E publisher is created
- **THEN** its first sealed message carries counter `1`

### Requirement: Receiver supervision

The receiver SHALL classify each message as `Ok`, `Repeated`, `WrongSequence`,
or `Error`, and SHALL expose per-endpoint counters for checked, failed,
repeated, and out-of-sequence messages. A repeated counter SHALL yield
`Repeated`; a counter gap above the configured maximum, or a counter that moves
backwards, SHALL yield `WrongSequence`; a data id mismatch SHALL yield `Error`.

#### Scenario: Duplicate delivery is reported as repeated

- **WHEN** two consecutive messages carry the same counter
- **THEN** the second is reported `Repeated` and is not delivered as fresh data

#### Scenario: A gap is reported as out of sequence

- **WHEN** a message arrives with a counter more than the configured maximum delta ahead of the last accepted one
- **THEN** it is reported `WrongSequence`

#### Scenario: A foreign data id is an error

- **WHEN** a message arrives with a data id different from the configured one
- **THEN** it is reported `Error`

#### Scenario: Counters are exposed

- **WHEN** the endpoint diagnostic is rendered after a mix of valid and invalid messages
- **THEN** it reports the checked, failed, repeated, and out-of-sequence totals

### Requirement: Zero-copy E2E ports

`E2ePublisher`, `E2eSubscriber`, `E2eClient`, and `E2eServer` SHALL wrap the
existing ports, keep the loan/write/send publication style, add no heap
allocation or additional payload copy, and return the check result next to the
sample.

#### Scenario: Publish stays allocation-free

- **WHEN** a sealed message is published under the project allocation counter
- **THEN** the allocation count is unchanged

#### Scenario: The application sees the result

- **WHEN** a subscriber receives a message
- **THEN** it gets the payload together with the E2E status and state, without a separate query

### Requirement: Method protection

`E2eClient` SHALL seal the request and check the response; `E2eServer` SHALL
check the request and seal the response, and SHALL pass the request's E2E status
to the handler so a detected fault can be propagated into the response.

#### Scenario: A call round-trips protected

- **WHEN** a client calls a method through the E2E wrappers
- **THEN** the server receives a valid request and the client receives a valid response

#### Scenario: A corrupted request reaches the handler as a failure

- **WHEN** the request fails its check on the server
- **THEN** the handler sees the failed status and the client receives a response that reflects it

### Requirement: E2E service configuration

A service entry SHALL accept an optional `e2e` section with `profile`,
`data_id`, and `max_delta_counter`. An absent section SHALL mean `None`; an
invalid profile or a non-positive data id SHALL be rejected when the manifest is
parsed.

#### Scenario: Section is parsed

- **WHEN** a service declares `P04` with a data id and a maximum delta
- **THEN** the endpoint uses those values and renders them in diagnostics

#### Scenario: Absent section keeps plain communication

- **WHEN** a service entry has no `e2e` section
- **THEN** the service publishes and receives the plain payload type as before

#### Scenario: Invalid profile is rejected

- **WHEN** the manifest names an unknown profile
- **THEN** the parse fails and the application does not start

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
