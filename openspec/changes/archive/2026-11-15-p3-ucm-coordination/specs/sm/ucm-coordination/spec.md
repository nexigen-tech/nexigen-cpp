# Spec Delta

## ADDED Requirements

### Requirement: UCM vocabulary

`nexigen::sm` SHALL expose `UpdateAction` (`Start`, `Complete`, `Cancel`),
`UpdateResult` (`Accepted`, `Rejected`, `Busy`, `InvalidGroup`, `InvalidState`,
`EmFailure`), and the fixed-size PODs `UpdateRequest` and `UpdateResponse`. All
four SHALL provide the full conversion set (`to_string`/`from_string`, ADL
`to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter`), and the
payloads SHALL remain trivially copyable `com::concepts::Message` types.

#### Scenario: Update action round-trips

- **WHEN** each `UpdateAction` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders it

#### Scenario: Update request round-trips

- **WHEN** an update request with a group, update id, action, and target passes through JSON and back
- **THEN** every field is preserved

### Requirement: Update start

`UpdateCoordinator` SHALL accept `Start` only when no update is active, the
Function Group exists, it is not `MachineFG`, it declares `Update`, and
`PlatformFG` is `Run`. It SHALL command the EM into `Update`, wait for
`Success`, record the previous state, and lock the group. A second `Start`
SHALL return `Busy`; an unsafe request SHALL return the matching rejection and
leave the group unchanged.

#### Scenario: Safe group enters Update

- **WHEN** UCM starts an update for a declared application group while PlatformFG is Run
- **THEN** the EM enters `Update`, the previous state is recorded, and the group is locked

#### Scenario: MachineFG cannot be updated

- **WHEN** UCM starts an update for `MachineFG`
- **THEN** the response is `Rejected` and the platform state is unchanged

#### Scenario: Concurrent update is busy

- **WHEN** UCM starts a second update while one is active
- **THEN** the response is `Busy`

### Requirement: Update completion and cancellation

`Complete` and `Cancel` SHALL require the active `update_id`. `Complete` SHALL
move the group to the request's target state (or the previous state when the
target is empty); `Cancel` SHALL restore the previous state. Both SHALL wait
for EM `Success`, update the SM state, and clear the lock. An EM failure SHALL
leave the update active so the operator can retry.

#### Scenario: Complete restores the target

- **WHEN** UCM completes an active update with target `Run`
- **THEN** the EM enters `Run` and the lock is cleared

#### Scenario: Cancel restores the previous state

- **WHEN** UCM cancels an active update
- **THEN** the group returns to the state captured at Start and the lock is cleared

#### Scenario: Wrong update id is rejected

- **WHEN** `Complete` carries an id different from the active one
- **THEN** the response is `Rejected` and the update stays active

### Requirement: Update lock blocks TriggerIn

While a group is locked by an update, the SM SHALL reject a TriggerIn
transition for that group with `TransitionResult::RejectedByUpdate` and SHALL
leave its state unchanged. Other groups SHALL remain controllable.

#### Scenario: Locked group rejects a trigger

- **WHEN** an application sends TriggerIn for a group that is updating
- **THEN** the SM returns `RejectedByUpdate` and does not switch the group

#### Scenario: Other groups still switch

- **WHEN** one group is updating and another receives a valid TriggerIn
- **THEN** the other group transitions normally
