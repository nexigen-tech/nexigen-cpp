# Spec Delta

## ADDED Requirements

### Requirement: Requester identity in StateCommand

`StateCommand` SHALL carry a fixed-size `requester_id` alongside the Function
Group and target state. Existing senders MAY leave it empty. The command SHALL
remain a trivially copyable `com::concepts::Message`, and its conversion set
SHALL render the requester.

#### Scenario: Command round-trips the requester

- **WHEN** a command with a requester id, group, and state is rendered and sent
- **THEN** the receiver reads the same requester id without serialization

### Requirement: Function-Group-port authorization

A suite manifest MAY declare `FunctionGroupPortMapping` entries, each pairing a
`requester_id` with a `function_group`. The mapping SHALL provide `to_string()`,
ADL JSON, and `std::formatter`. Validation SHALL reject unsafe ids and a
mapping to an undeclared group.

When at least one mapping is declared, `StateClient` SHALL accept a `SetState`
only when a mapping matches both the command's `requester_id` and its Function
Group; otherwise it SHALL reject the command with
`StateResult::AccessDenied` and leave the group unchanged. An empty mapping list
SHALL preserve the previous accept-all behavior.

#### Scenario: Authorized requester switches a group

- **WHEN** the mapping lists `(sm, ADAS)` and the command carries `requester_id=sm` for `ADAS`
- **THEN** the EM executes the transition

#### Scenario: Unknown requester is rejected

- **WHEN** the mapping is non-empty and the command carries an unlisted requester
- **THEN** the EM acknowledges `AccessDenied` and the group state does not change

#### Scenario: Empty mapping keeps compatibility

- **WHEN** the manifest declares no port mapping
- **THEN** a command without a requester id is processed as before

### Requirement: State Manager propagates the requester

The SM SHALL copy the `TriggerRequest::requester_id` into the pending
transition and then into the `StateCommand` sent to the EM. The SM's internal
MachineFG lifecycle client SHALL use its configured application name, or `sm`
when no name is configured, as the requester.

#### Scenario: Trigger requester reaches the EM

- **WHEN** a TriggerIn request with `requester_id=phm` is accepted
- **THEN** the eventual `StateCommand` carries `phm`

#### Scenario: Internal MachineFG request names the SM

- **WHEN** the SM commands `MachineFG:Verify` during startup
- **THEN** the command carries the SM application name or `sm`
