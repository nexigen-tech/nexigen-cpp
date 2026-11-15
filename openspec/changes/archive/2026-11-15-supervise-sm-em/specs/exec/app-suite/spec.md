# Spec Delta

## ADDED Requirements

### Requirement: Execution Manager health heartbeat

The suite control host SHALL emit a heartbeat checkpoint with event id `1` on
`Nexigen/EM/Health` once per control poll, so a PHM manifest can supervise the
Execution Manager exactly like any other process.

#### Scenario: The EM heartbeat reaches PHM

- **WHEN** the suite supervisor runs its control poll
- **THEN** an event with id `1` is published on `Nexigen/EM/Health`

#### Scenario: A missing heartbeat expires the EM

- **WHEN** a PHM manifest supervises `Nexigen/EM/Health` and the suite stops
  polling
- **THEN** the EM entry becomes `Expired` after the configured tolerance, and
  the watchdog feed gate stops feeding

#### Scenario: A deployment without the channel is unaffected

- **WHEN** the PHM manifest does not list `Nexigen/EM/Health`
- **THEN** the suite supervisor behaves exactly as before
